#pragma once
// Level-dependent processors: gate, compressor, limiter, automatic level, de-esser,
// plosive suppressor, hum notch bank and the voice/noise-floor detector.
#include "basics.h"

namespace ms {
namespace dsp {

// Tracks the broadband level and a slowly rising noise floor; flags voice activity.
class VoiceDetector {
 public:
  void prepare(double fs) {
    fs_ = fs;
    msCoef_ = TimeCoef(20.0, fs);
    chunk_ = std::max(1, (int)(fs * 0.005));
    hangChunks_ = (int)(0.30 / 0.005);
    reset();
  }
  void reset() {
    ms_ = 0.f;
    floorDb_ = -70.f;
    levelDb_ = -120.f;
    count_ = 0;
    hang_ = 0;
    startup_ = 400;  // first 2 s: let the floor settle fast
    voice_ = false;
  }
  void process(const float* x, int n) {
    for (int i = 0; i < n; ++i) {
      ms_ = x[i] * x[i] + (ms_ - x[i] * x[i]) * msCoef_;
      if (++count_ >= chunk_) {
        count_ = 0;
        update();
      }
    }
  }
  bool voice() const { return voice_; }
  float noiseFloorDb() const { return floorDb_; }
  float levelDb() const { return levelDb_; }

 private:
  void update() {
    const float dt = 0.005f;
    levelDb_ = PowToDb(ms_);
    const float rise = startup_ > 0 ? 12.f : (levelDb_ - floorDb_ > 20.f ? 0.75f : 1.5f);  // dB/s
    if (startup_ > 0) --startup_;
    if (levelDb_ < floorDb_)
      floorDb_ += (levelDb_ - floorDb_) * 0.05f;  // falls within ~100 ms, ignores single dips
    else
      floorDb_ += std::min(levelDb_ - floorDb_, rise * dt);
    floorDb_ = Clamp(floorDb_, -110.f, -10.f);
    if (levelDb_ > floorDb_ + 12.f && levelDb_ > -62.f) {
      voice_ = true;
      hang_ = hangChunks_;
    } else if (hang_ > 0) {
      --hang_;
    } else {
      voice_ = false;
    }
  }
  double fs_ = 48000;
  float ms_ = 0.f, floorDb_ = -70.f, levelDb_ = -120.f, msCoef_ = 0.f;
  int chunk_ = 240, count_ = 0, hang_ = 0, hangChunks_ = 60, startup_ = 400;
  bool voice_ = false;
};

// Noise gate with hysteresis and hold; closed state attenuates by `range` instead of muting.
class Gate {
 public:
  void prepare(double fs) {
    fs_ = fs;
    env_.set(0.5, 25.0, fs);
    reset();
  }
  void reset() {
    env_.reset();
    gain_ = 1.f;
    open_ = true;
    hold_ = 0;
  }
  void setParams(bool on, float thresholdDb, float rangeDb, float attackMs, float holdMs, float releaseMs) {
    on_ = on;
    setThreshold(thresholdDb);
    closed_ = DbToLin(-rangeDb);
    att_ = TimeCoef(attackMs, fs_);
    rel_ = TimeCoef(releaseMs, fs_);
    holdSamples_ = (int)(holdMs * 0.001 * fs_);
  }
  void setThreshold(float thresholdDb) {
    openLin_ = DbToLin(thresholdDb);
    closeLin_ = DbToLin(thresholdDb - 4.f);
  }
  inline float process(float x) {
    const float e = env_.process(fabsf(x));
    if (e > openLin_) {
      open_ = true;
      hold_ = holdSamples_;
    } else if (hold_ > 0) {
      --hold_;
    } else if (e < closeLin_) {
      open_ = false;
    }
    const float target = (!on_ || open_) ? 1.f : closed_;
    gain_ = target + (gain_ - target) * (target > gain_ ? att_ : rel_);
    return x * gain_;
  }
  float gain() const { return gain_; }

 private:
  double fs_ = 48000;
  Envelope env_;
  bool on_ = false, open_ = true;
  float openLin_ = 0.003f, closeLin_ = 0.002f, closed_ = 0.1f, att_ = 0.f, rel_ = 0.f, gain_ = 1.f;
  int hold_ = 0, holdSamples_ = 0;
};

// Feed-forward log-domain compressor with soft knee and a smooth decoupled peak detector.
class Compressor {
 public:
  void prepare(double fs) {
    fs_ = fs;
    reset();
  }
  void reset() {
    y1_ = yl_ = 0.f;
    gainDb_ = 0.f;
  }
  void setParams(bool on, float thrDb, float ratio, float attackMs, float releaseMs, float kneeDb,
                 float makeupDb, bool autoMakeup) {
    on_ = on;
    thr_ = thrDb;
    slope_ = 1.f / std::max(ratio, 1.f) - 1.f;  // <= 0
    knee_ = std::max(kneeDb, 0.f);
    att_ = TimeCoef(attackMs, fs_);
    rel_ = TimeCoef(releaseMs, fs_);
    float m = makeupDb;
    if (autoMakeup) m += Clamp(-0.5f * thrDb * (1.f - 1.f / std::max(ratio, 1.f)), 0.f, 15.f);
    makeup_ = on ? m : 0.f;
  }
  inline float process(float x) {
    if (!on_) return x;
    const float a = fabsf(x);
    const float xdB = a > 1e-6f ? 20.f * log10f(a) : -120.f;
    const float over = xdB - thr_;
    float red;  // gain reduction in dB, >= 0
    if (2.f * over < -knee_)
      red = 0.f;
    else if (knee_ > 0.f && 2.f * fabsf(over) <= knee_) {
      const float t = over + knee_ * 0.5f;
      red = -slope_ * t * t / (2.f * knee_);
    } else
      red = -slope_ * over;
    y1_ = std::max(red, rel_ * y1_ + (1.f - rel_) * red);
    yl_ = att_ * yl_ + (1.f - att_) * y1_;
    gainDb_ = -yl_;
    return x * DbToLin(gainDb_ + makeup_);
  }
  float gainDb() const { return gainDb_; }

 private:
  double fs_ = 48000;
  bool on_ = false;
  float thr_ = -20.f, slope_ = -0.66f, knee_ = 6.f, att_ = 0.f, rel_ = 0.f, makeup_ = 0.f;
  float y1_ = 0.f, yl_ = 0.f, gainDb_ = 0.f;
};

// Look-ahead brickwall limiter: sliding minimum + moving average guarantee the ceiling.
class Limiter {
 public:
  void prepare(double fs) {
    fs_ = fs;
    w_ = std::max(8, (int)(fs * 0.0015));
    delay_.assign(w_, 0.f);
    hbuf_.assign(w_, 1.f);
    dqVal_.assign(w_ + 1, 1.f);
    dqIdx_.assign(w_ + 1, 0u);
    relCoef_ = TimeCoef(60.0, fs);
    reset();
  }
  void reset() {
    std::fill(delay_.begin(), delay_.end(), 0.f);
    std::fill(hbuf_.begin(), hbuf_.end(), 1.f);
    pos_ = 0;
    t_ = 0;
    head_ = tail_ = 0;
    sum_ = (double)w_;
    env_ = 1.f;
    minGain_ = 1.f;
    resum_ = 0;
  }
  void setParams(bool on, float ceilingDb) {
    on_ = on;
    ceil_ = DbToLin(ceilingDb);
  }
  int latency() const { return w_ - 1; }

  inline float process(float x) {
    const float a = fabsf(x);
    const float r = (on_ && a > ceil_) ? ceil_ / a : 1.f;
    // sliding minimum of r over the last w samples (monotonic deque)
    const int cap = w_ + 1;
    while (head_ != tail_) {
      int back = (tail_ + cap - 1) % cap;
      if (dqVal_[back] >= r)
        tail_ = back;
      else
        break;
    }
    dqVal_[tail_] = r;
    dqIdx_[tail_] = t_;
    tail_ = (tail_ + 1) % cap;
    while ((unsigned)(t_ - dqIdx_[head_]) >= (unsigned)w_) head_ = (head_ + 1) % cap;
    const float h = dqVal_[head_];
    // moving average of h over the last w samples
    sum_ += h - hbuf_[pos_];
    hbuf_[pos_] = h;
    if (++resum_ >= 65536) {
      resum_ = 0;
      double s = 0;
      for (float v : hbuf_) s += v;
      sum_ = s;
    }
    const float s = (float)(sum_ / w_);
    env_ = s < env_ ? s : s + (env_ - s) * relCoef_;
    if (env_ < minGain_) minGain_ = env_;
    // delay line of w-1 samples: the slot we overwrite holds x[t-w+1] after the write
    delay_[pos_] = x;
    const int rd = (pos_ + 1) % w_;
    float y = delay_[rd] * env_;
    pos_ = rd;
    ++t_;
    if (on_) y = Clamp(y, -ceil_, ceil_);
    return y;
  }
  // Smallest gain since the last call (for meters).
  float takeMinGain() {
    float g = minGain_;
    minGain_ = 1.f;
    return g;
  }

 private:
  double fs_ = 48000;
  bool on_ = true;
  int w_ = 72, pos_ = 0, head_ = 0, tail_ = 0, resum_ = 0;
  unsigned t_ = 0;
  float ceil_ = 0.89f, relCoef_ = 0.f, env_ = 1.f, minGain_ = 1.f;
  double sum_ = 0;
  std::vector<float> delay_, hbuf_, dqVal_;
  std::vector<unsigned> dqIdx_;
};

// Automatic level: rides the gain slowly towards a loudness target, only while voice is present.
class Agc {
 public:
  void prepare(double fs) {
    fs_ = fs;
    // ITU-R BS.1770 K-weighting approximated with two biquads
    shelf_.design(Biquad::kHighShelf, fs, 1681.97, 3.9998, 0.7071752);
    hp_.design(Biquad::kHighPass, fs, 38.13547, 0.0, 0.5003270);
    msCoef_ = TimeCoef(400.0, fs);
    reset();
  }
  void reset() {
    shelf_.reset();
    hp_.reset();
    ms_ = 0.f;
    gainDb_ = 0.f;
    curLin_ = 1.f;
  }
  void setParams(bool on, float targetDb, float maxGainDb, float speed) {
    on_ = on;
    target_ = targetDb;
    maxGain_ = maxGainDb;
    rate_ = 1.f + 9.f * Clamp(speed, 0.f, 1.f);  // dB per second
  }
  void process(float* x, int n, bool voice) {
    for (int i = 0; i < n; ++i) {
      const float k = shelf_.process(hp_.process(x[i]));
      ms_ = k * k + (ms_ - k * k) * msCoef_;
    }
    const float dt = (float)(n / fs_);
    if (on_) {
      const float loud = -0.691f + PowToDb(ms_);
      if (voice && loud > -65.f) {
        const float err = target_ - (loud + gainDb_);
        const float step = rate_ * dt;
        gainDb_ += Clamp(err, -2.f * step, step);
      }
      gainDb_ = Clamp(gainDb_, -12.f, maxGain_);
    } else {
      gainDb_ += (0.f - gainDb_) * std::min(1.f, dt * 20.f);
    }
    const float target = DbToLin(gainDb_);
    if (n > 0) {
      const float inc = (target - curLin_) / n;
      for (int i = 0; i < n; ++i) {
        curLin_ += inc;
        x[i] *= curLin_;
      }
    }
    curLin_ = target;
  }
  float gainDb() const { return gainDb_; }

 private:
  double fs_ = 48000;
  bool on_ = false;
  Biquad shelf_, hp_;
  float ms_ = 0.f, msCoef_ = 0.f, gainDb_ = 0.f, curLin_ = 1.f;
  float target_ = -18.f, maxGain_ = 12.f, rate_ = 4.f;
};

// Split-band de-esser: when the sibilance band dominates, the upper band is turned down.
class DeEsser {
 public:
  void prepare(double fs) {
    fs_ = fs;
    envS_.set(0.5, 30.0, fs);
    envF_.set(0.5, 30.0, fs);
    att_ = TimeCoef(1.0, fs);
    rel_ = TimeCoef(45.0, fs);
    absThr_ = DbToLin(-50.f);
    design();
    reset();
  }
  void reset() {
    bp_.reset();
    hp_.reset();
    envS_.reset();
    envF_.reset();
    gain_ = 1.f;
    minGain_ = 1.f;
  }
  void setParams(bool on, float freq, float amount) {
    const bool redesign = freq != freq_;
    on_ = on;
    freq_ = freq;
    relThrDb_ = -6.f - 12.f * amount;
    maxRedDb_ = 3.f + 9.f * amount;
    if (redesign) design();
  }
  inline float process(float x) {
    const float s = envS_.process(fabsf(bp_.process(x)));
    const float f = envF_.process(fabsf(x));
    float target = 1.f;
    if (on_ && s > absThr_ && f > 1e-9f) {
      const float over = LinToDb(s / f) - relThrDb_;
      if (over > 0.f) target = DbToLin(-std::min(over * 1.5f, maxRedDb_));
    }
    gain_ = target + (gain_ - target) * (target < gain_ ? att_ : rel_);
    if (gain_ < minGain_) minGain_ = gain_;
    const float hi = hp_.process(x);
    return (x - hi) + hi * gain_;
  }
  float takeMinGain() {
    float g = minGain_;
    minGain_ = 1.f;
    return g;
  }

 private:
  void design() {
    bp_.design(Biquad::kBandPass, fs_, freq_, 0.0, 1.0);
    hp_.design(Biquad::kHighPass, fs_, freq_ * 0.55, 0.0, 0.7071);
  }
  double fs_ = 48000;
  bool on_ = false;
  float freq_ = 6500.f, relThrDb_ = -10.f, maxRedDb_ = 6.f, absThr_ = 0.003f;
  Biquad bp_, hp_;
  Envelope envS_, envF_;
  float att_ = 0.f, rel_ = 0.f, gain_ = 1.f, minGain_ = 1.f;
};

// "Digital pop filter": ducks the sub-bass band during plosive bursts (p, b, t).
// Linkwitz-Riley 4th order split at 110 Hz: low + high is an all-pass, so with the
// gain at 1 the voice keeps its spectrum, and the low band can really be pulled down.
class PopFilter {
 public:
  void prepare(double fs) {
    fs_ = fs;
    for (int i = 0; i < 2; ++i) {
      lp_[i].design(Biquad::kLowPass, fs, 110.0, 0.0, 0.70710678);
      hp_[i].design(Biquad::kHighPass, fs, 110.0, 0.0, 0.70710678);
    }
    envL_.set(0.3, 40.0, fs);
    envH_.set(0.3, 40.0, fs);
    att_ = TimeCoef(0.5, fs);
    rel_ = TimeCoef(70.0, fs);
    absThr_ = DbToLin(-32.f);
    reset();
  }
  void reset() {
    for (int i = 0; i < 2; ++i) {
      lp_[i].reset();
      hp_[i].reset();
    }
    envL_.reset();
    envH_.reset();
    gain_ = 1.f;
    minGain_ = 1.f;
  }
  void setParams(bool on, float amount) {
    on_ = on;
    ratio_ = 6.f - 3.f * amount;  // low band may be this many times louder than the rest
    floor_ = DbToLin(-(6.f + 18.f * amount));
  }
  inline float process(float x) {
    const float lo = lp_[1].process(lp_[0].process(x));
    const float hi = hp_[1].process(hp_[0].process(x));
    const float el = envL_.process(fabsf(lo));
    const float eh = envH_.process(fabsf(hi));
    if (!on_) return x;
    float target = 1.f;
    if (el > absThr_ && el > ratio_ * eh) {
      const float r = ratio_ * eh / el;
      target = std::max(floor_, r * r);
    }
    gain_ = target + (gain_ - target) * (target < gain_ ? att_ : rel_);
    if (gain_ < minGain_) minGain_ = gain_;
    return hi + lo * gain_;
  }
  float takeMinGain() {
    float g = minGain_;
    minGain_ = 1.f;
    return g;
  }

 private:
  double fs_ = 48000;
  bool on_ = false;
  Biquad lp_[2], hp_[2];
  Envelope envL_, envH_;
  float ratio_ = 4.5f, floor_ = 0.1f, absThr_ = 0.025f, att_ = 0.f, rel_ = 0.f, gain_ = 1.f, minGain_ = 1.f;
};

// Narrow notches at the mains frequency and its harmonics.
class HumFilter {
 public:
  static constexpr int kMax = 8;
  void prepare(double fs) {
    fs_ = fs;
    design();
  }
  void reset() {
    for (auto& b : notch_) b.reset();
  }
  void setParams(bool on, float freq, int harmonics) {
    if (on == on_ && freq == freq_ && harmonics == count_) return;
    on_ = on;
    freq_ = freq;
    count_ = harmonics;
    design();
  }
  inline float process(float x) {
    if (!on_) return x;
    for (int i = 0; i < active_; ++i) x = notch_[i].process(x);
    return x;
  }

 private:
  void design() {
    active_ = 0;
    for (int h = 1; h <= count_ && h <= kMax; ++h) {
      const double f = freq_ * h;
      if (f > fs_ * 0.45) break;
      notch_[active_].design(Biquad::kNotch, fs_, f, 0.0, std::min(60.0, f / 3.0));
      ++active_;
    }
  }
  double fs_ = 48000;
  bool on_ = false;
  float freq_ = 50.f;
  int count_ = 4, active_ = 0;
  Biquad notch_[kMax];
};

}  // namespace dsp
}  // namespace ms
