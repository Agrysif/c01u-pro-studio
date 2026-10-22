#pragma once
// The complete voice chain. Mono in place; callers downmix/upmix channels.
//
//  in gain -> DC/low cut -> hum notch -> noise suppression -> pop filter -> gate
//  -> EQ (+ warmth/clarity/air) -> de-esser -> auto level -> compressor -> fun FX
//  -> out gain -> limiter -> mute
#include "basics.h"
#include "dynamics.h"
#include "fx.h"
#include "noise_suppressor.h"
#include "../shared/params.h"

namespace ms {
namespace dsp {

struct ChainStats {
  float inPeak = 0.f, inMs = 0.f;     // linear peak, mean square of the last block
  float outPeak = 0.f, outMs = 0.f;
  float gateGain = 1.f, compGainDb = 0.f, limGain = 1.f, agcGainDb = 0.f;
  float deessGain = 1.f, popGain = 1.f, nsReductionDb = 0.f;
  float noiseFloorDb = -100.f;   // after noise suppression (drives the gate)
  float inputFloorDb = -100.f;   // of the raw microphone signal (shown to the user)
  bool voice = false;
};

class Chain {
 public:
  void prepare(double fs) {
    fs_ = fs;
    dc_.prepare(fs, 10.0);
    ns_.prepare(fs);
    vad_.prepare(fs);
    inFloor_.prepare(fs);
    gate_.prepare(fs);
    comp_.prepare(fs);
    lim_.prepare(fs);
    agc_.prepare(fs);
    deess_.prepare(fs);
    pop_.prepare(fs);
    hum_.prepare(fs);
    fx_.prepare(fs);
    prepared_ = true;
    applyParams();
    reset();
  }

  void reset() {
    dc_.reset();
    for (auto& b : hpf_) b.reset();
    for (auto& b : eq_) b.reset();
    for (auto& b : tone_) b.reset();
    ns_.reset();
    vad_.reset();
    inFloor_.reset();
    gate_.reset();
    comp_.reset();
    lim_.reset();
    agc_.reset();
    deess_.reset();
    pop_.reset();
    hum_.reset();
    muteGain_ = p_.mute ? 0.f : 1.f;
  }

  // Cheap enough for the real-time thread: only recomputes coefficients.
  void setParams(const Params& p) {
    p_ = p;
    if (prepared_) applyParams();
  }
  const Params& params() const { return p_; }

  void relearnNoise() { ns_.relearn(); }

  int latency() const { return ns_.latency() + lim_.latency(); }
  double sampleRate() const { return fs_; }
  const ChainStats& stats() const { return st_; }

  void process(float* x, int n) {
    if (n <= 0) return;
    float pk = 0.f, ms = 0.f;
    for (int i = 0; i < n; ++i) {
      const float a = fabsf(x[i]);
      if (a > pk) pk = a;
      ms += x[i] * x[i];
    }
    st_.inPeak = pk;
    st_.inMs = ms / n;
    inFloor_.process(x, n);
    st_.inputFloorDb = inFloor_.noiseFloorDb();

    if (p_.bypass) {
      st_.outPeak = st_.inPeak;
      st_.outMs = st_.inMs;
      st_.gateGain = st_.limGain = st_.deessGain = st_.popGain = 1.f;
      st_.compGainDb = st_.agcGainDb = st_.nsReductionDb = 0.f;
      return;
    }

    // 1. input gain, DC blocker, low cut, hum notches
    for (int i = 0; i < n; ++i) {
      float v = dc_.process(x[i] * inGain_);
      for (int k = 0; k < hpfCount_; ++k) v = hpf_[k].process(v);
      x[i] = hum_.process(v);
    }
    // 2. noise suppression (block based, constant latency)
    ns_.process(x, n);
    // 3. voice activity + noise floor (drives the auto gate and the auto level)
    vad_.process(x, n);
    if (p_.gateAuto) gate_.setThreshold(Clamp(vad_.noiseFloorDb() + 8.f, -75.f, -25.f));
    // 4. pop filter, gate, EQ, de-esser
    for (int i = 0; i < n; ++i) {
      float v = pop_.process(x[i]);
      v = gate_.process(v);
      if (p_.eqOn) {
        for (int k = 0; k < eqCount_; ++k) v = eq_[eqIndex_[k]].process(v);
        for (int k = 0; k < 3; ++k)
          if (toneOn_[k]) v = tone_[k].process(v);
      }
      x[i] = deess_.process(v);
    }
    // 5. automatic level
    agc_.process(x, n, vad_.voice());
    // 6. compressor, FX, output gain, limiter, mute
    for (int i = 0; i < n; ++i) x[i] = comp_.process(x[i]);
    fx_.process(x, n);
    const float target = p_.mute ? 0.f : 1.f;
    const float muteStep = 1.f / (float)(fs_ * 0.01);
    pk = 0.f;
    ms = 0.f;
    for (int i = 0; i < n; ++i) {
      float v = lim_.process(x[i] * outGain_);
      if (muteGain_ != target) {
        muteGain_ = target > muteGain_ ? std::min(target, muteGain_ + muteStep) : std::max(target, muteGain_ - muteStep);
      }
      v *= muteGain_;
      x[i] = v;
      const float a = fabsf(v);
      if (a > pk) pk = a;
      ms += v * v;
    }
    st_.outPeak = pk;
    st_.outMs = ms / n;
    st_.gateGain = gate_.gain();
    st_.compGainDb = comp_.gainDb();
    st_.limGain = lim_.takeMinGain();
    st_.agcGainDb = agc_.gainDb();
    st_.deessGain = deess_.takeMinGain();
    st_.popGain = pop_.takeMinGain();
    st_.nsReductionDb = p_.nsOn ? ns_.reductionDb() : 0.f;
    st_.noiseFloorDb = vad_.noiseFloorDb();
    st_.voice = vad_.voice();
  }

 private:
  void applyParams() {
    const Params& p = p_;
    inGain_ = DbToLin(p.inputGainDb);
    outGain_ = DbToLin(p.outputGainDb);

    hpfCount_ = 0;
    if (p.hpfOn) {
      if (p.hpfOrder >= 4) {
        hpf_[0].design(Biquad::kHighPass, fs_, p.hpfFreq, 0.0, 0.54119610);
        hpf_[1].design(Biquad::kHighPass, fs_, p.hpfFreq, 0.0, 1.3065630);
        hpfCount_ = 2;
      } else {
        hpf_[0].design(Biquad::kHighPass, fs_, p.hpfFreq, 0.0, 0.70710678);
        hpfCount_ = 1;
      }
    }
    hum_.setParams(p.humOn != 0, p.humFreq, p.humHarmonics);
    ns_.setParams(p.nsOn != 0, p.nsReductionDb, p.nsSensitivity);
    pop_.setParams(p.popOn != 0, p.popAmount);
    gate_.setParams(p.gateOn != 0, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs);

    eqCount_ = 0;
    for (int i = 0; i < kMaxEqBands; ++i) {
      const EqBand& b = p.eq[i];
      if (!b.on) continue;
      // bands at 0 dB that do not depend on gain are skipped (peak/shelves)
      const bool gainType = b.type == kEqPeak || b.type == kEqLowShelf || b.type == kEqHighShelf;
      if (gainType && fabsf(b.gainDb) < 0.05f) continue;
      eq_[i].design(b.type, fs_, b.freq, b.gainDb, b.q);
      eqIndex_[eqCount_++] = i;
    }
    toneOn_[0] = fabsf(p.warmth) > 0.01f;
    toneOn_[1] = fabsf(p.clarity) > 0.01f;
    toneOn_[2] = fabsf(p.air) > 0.01f;
    tone_[0].design(Biquad::kLowShelf, fs_, 220.0, 6.0 * p.warmth, 0.7);
    tone_[1].design(Biquad::kPeak, fs_, 3200.0, 5.0 * p.clarity, 0.8);
    tone_[2].design(Biquad::kHighShelf, fs_, 10000.0, 6.0 * p.air, 0.7);

    deess_.setParams(p.deessOn != 0, p.deessFreq, p.deessAmount);
    agc_.setParams(p.agcOn != 0, p.agcTargetDb, p.agcMaxGainDb, p.agcSpeed);
    comp_.setParams(p.compOn != 0, p.compThresholdDb, p.compRatio, p.compAttackMs, p.compReleaseMs,
                    p.compKneeDb, p.compMakeupDb, p.compAutoMakeup != 0);
    fx_.setParams(p.fxType, p.fxMix, p.fxParam);
    lim_.setParams(p.limOn != 0, p.limCeilingDb);
  }

  double fs_ = 48000;
  bool prepared_ = false;
  Params p_ = MakeDefault();
  ChainStats st_;
  float inGain_ = 1.f, outGain_ = 1.f, muteGain_ = 1.f;
  DcBlocker dc_;
  Biquad hpf_[2];
  int hpfCount_ = 0;
  HumFilter hum_;
  NoiseSuppressor ns_;
  VoiceDetector vad_, inFloor_;
  PopFilter pop_;
  Gate gate_;
  Biquad eq_[kMaxEqBands];
  int eqIndex_[kMaxEqBands] = {};
  int eqCount_ = 0;
  Biquad tone_[3];
  bool toneOn_[3] = {};
  DeEsser deess_;
  Agc agc_;
  Compressor comp_;
  VoiceFx fx_;
  Limiter lim_;

  static Params MakeDefault() {
    Params p;
    DefaultParams(&p);
    return p;
  }
};

}  // namespace dsp
}  // namespace ms
