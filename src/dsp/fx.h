#pragma once
// Just-for-fun voice effects: robot, radio, telephone, pitch shift, echo, room reverb.
#include "basics.h"
#include "../shared/params.h"

namespace ms {
namespace dsp {

class VoiceFx {
 public:
  void prepare(double fs) {
    fs_ = fs;
    const double r = fs / 44100.0;
    static const int kComb[4] = {1116, 1188, 1277, 1356};
    static const int kAllPass[2] = {556, 441};
    for (int i = 0; i < 4; ++i) {
      combLen_[i] = std::max(16, (int)(kComb[i] * r));
      comb_[i].assign(combLen_[i], 0.f);
    }
    for (int i = 0; i < 2; ++i) {
      apLen_[i] = std::max(16, (int)(kAllPass[i] * r));
      ap_[i].assign(apLen_[i], 0.f);
    }
    delay_.prepare((int)(fs * 0.7));
    pitchWin_ = (float)(fs * 0.040);
    current_ = -1;
    configure();
  }

  void setParams(int type, float mix, float param) {
    type_ = type;
    mix_ = mix;
    param_ = param;
    configure();
  }

  void process(float* x, int n) {
    if (type_ == kFxNone || mix_ <= 0.f) {
      current_ = type_;
      return;
    }
    if (current_ != type_) resetState();
    const float dry = 1.f - mix_, wet = mix_;
    for (int i = 0; i < n; ++i) {
      const float in = x[i];
      float y = in;
      switch (type_) {
        case kFxRobot: y = robot(in); break;
        case kFxRadio: y = radio(in); break;
        case kFxTelephone: y = telephone(in); break;
        case kFxPitch: y = pitch(in); break;
        case kFxEcho: y = echo(in); break;
        case kFxReverb: y = reverb(in); break;
        default: break;
      }
      x[i] = in * dry + y * wet;
    }
  }

 private:
  void configure() {
    switch (type_) {
      case kFxRobot:
        ringInc_ = (float)(2.0 * kPi * (30.0 + 90.0 * param_) / fs_);
        combDelay_ = (float)(fs_ * (0.004 + 0.008 * param_));
        break;
      case kFxRadio:
        bp1_.design(Biquad::kHighPass, fs_, 450.0, 0.0, 0.7071);
        bp2_.design(Biquad::kLowPass, fs_, 2800.0, 0.0, 0.7071);
        bp3_.design(Biquad::kLowPass, fs_, 3200.0, 0.0, 0.7071);
        drive_ = 2.f + 10.f * param_;
        driveNorm_ = 1.f / tanhf(drive_);
        break;
      case kFxTelephone:
        bp1_.design(Biquad::kHighPass, fs_, 300.0, 0.0, 0.7071);
        bp2_.design(Biquad::kHighPass, fs_, 300.0, 0.0, 0.7071);
        bp3_.design(Biquad::kLowPass, fs_, 3400.0, 0.0, 0.7071);
        bp4_.design(Biquad::kLowPass, fs_, 3400.0, 0.0, 0.7071);
        drive_ = 1.5f + 2.f * param_;
        driveNorm_ = 1.f / tanhf(drive_);
        break;
      case kFxPitch: {
        const float semis = (param_ - 0.5f) * 24.f;
        pitchRatio_ = powf(2.f, semis / 12.f);
        break;
      }
      case kFxEcho:
        echoDelay_ = (float)(fs_ * (0.12 + 0.48 * param_));
        echoLp_.design(Biquad::kLowPass, fs_, 4000.0, 0.0, 0.7071);
        break;
      case kFxReverb:
        room_ = 0.70f + 0.27f * param_;
        damp_ = 0.35f;
        break;
      default:
        break;
    }
  }

  void resetState() {
    current_ = type_;
    delay_.reset();
    for (auto& c : comb_) std::fill(c.begin(), c.end(), 0.f);
    for (auto& a : ap_) std::fill(a.begin(), a.end(), 0.f);
    for (int i = 0; i < 4; ++i) { combIdx_[i] = 0; combStore_[i] = 0.f; }
    apIdx_[0] = apIdx_[1] = 0;
    bp1_.reset(); bp2_.reset(); bp3_.reset(); bp4_.reset(); echoLp_.reset();
    ringPhase_ = 0.f;
    pitchPhase_ = 0.f;
  }

  float robot(float x) {
    ringPhase_ += ringInc_;
    if (ringPhase_ > 2.f * (float)kPi) ringPhase_ -= 2.f * (float)kPi;
    const float m = x * sinf(ringPhase_) * 1.4f;
    const float fb = delay_.read(combDelay_);
    const float y = m + fb * 0.55f;
    delay_.push(y);
    return y * 0.7f;
  }
  float radio(float x) {
    float y = bp2_.process(bp1_.process(x));
    y = tanhf(y * drive_) * driveNorm_ * 0.6f;
    return bp3_.process(y);
  }
  float telephone(float x) {
    float y = bp4_.process(bp3_.process(bp2_.process(bp1_.process(x))));
    return tanhf(y * drive_) * driveNorm_ * 0.8f;
  }
  float pitch(float x) {
    delay_.push(x);
    const float w = pitchWin_;
    pitchPhase_ += (1.f - pitchRatio_) / w;
    if (pitchPhase_ >= 1.f) pitchPhase_ -= 1.f;
    if (pitchPhase_ < 0.f) pitchPhase_ += 1.f;
    float p2 = pitchPhase_ + 0.5f;
    if (p2 >= 1.f) p2 -= 1.f;
    const float s1 = sinf((float)kPi * pitchPhase_);
    const float s2 = sinf((float)kPi * p2);
    return delay_.read(pitchPhase_ * w) * s1 * s1 + delay_.read(p2 * w) * s2 * s2;
  }
  float echo(float x) {
    const float d = delay_.read(echoDelay_);
    delay_.push(x + echoLp_.process(d) * 0.45f);
    return x + d;
  }
  float reverb(float x) {
    const float in = x * 0.06f;
    float out = 0.f;
    for (int i = 0; i < 4; ++i) {
      std::vector<float>& b = comb_[i];
      const float y = b[combIdx_[i]];
      combStore_[i] = y * (1.f - damp_) + combStore_[i] * damp_;
      b[combIdx_[i]] = in + combStore_[i] * room_;
      if (++combIdx_[i] >= combLen_[i]) combIdx_[i] = 0;
      out += y;
    }
    for (int i = 0; i < 2; ++i) {
      std::vector<float>& b = ap_[i];
      const float bo = b[apIdx_[i]];
      const float y = -out + bo;
      b[apIdx_[i]] = out + bo * 0.5f;
      if (++apIdx_[i] >= apLen_[i]) apIdx_[i] = 0;
      out = y;
    }
    return x + out * 2.5f;
  }

  double fs_ = 48000;
  int type_ = kFxNone, current_ = -1;
  float mix_ = 0.5f, param_ = 0.5f;
  DelayLine delay_;
  Biquad bp1_, bp2_, bp3_, bp4_, echoLp_;
  float ringPhase_ = 0.f, ringInc_ = 0.f, combDelay_ = 400.f;
  float drive_ = 3.f, driveNorm_ = 1.f;
  float pitchRatio_ = 1.f, pitchPhase_ = 0.f, pitchWin_ = 1920.f;
  float echoDelay_ = 12000.f;
  float room_ = 0.84f, damp_ = 0.35f;
  std::vector<float> comb_[4], ap_[2];
  int combLen_[4] = {}, combIdx_[4] = {}, apLen_[2] = {}, apIdx_[2] = {};
  float combStore_[4] = {};
};

}  // namespace dsp
}  // namespace ms
