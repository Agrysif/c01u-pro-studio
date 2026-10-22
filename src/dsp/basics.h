#pragma once
// Small DSP building blocks. Header-only so the APO, the app and the tests share one copy.
// Rule for everything in src/dsp: memory is allocated only in prepare(), never in process().
#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace ms {
namespace dsp {

constexpr double kPi = 3.14159265358979323846;

inline float DbToLin(float db) { return expf(db * 0.11512925465f); }  // ln(10)/20
inline float LinToDb(float lin) { return lin > 1e-10f ? 20.f * log10f(lin) : -200.f; }
inline float PowToDb(float p) { return p > 1e-20f ? 10.f * log10f(p) : -200.f; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Coefficient c of a one-pole smoother y = x + (y - x) * c with time constant `ms`.
inline float TimeCoef(double ms, double fs) {
  if (ms <= 0.0 || fs <= 0.0) return 0.f;
  return (float)exp(-1000.0 / (ms * fs));
}

// Envelope follower with separate attack and release.
struct Envelope {
  float att = 0.f, rel = 0.f, value = 0.f;
  void set(double attackMs, double releaseMs, double fs) {
    att = TimeCoef(attackMs, fs);
    rel = TimeCoef(releaseMs, fs);
  }
  inline float process(float x) {
    value = x + (value - x) * (x > value ? att : rel);
    return value;
  }
  void reset(float v = 0.f) { value = v; }
};

// Transposed direct form II biquad, coefficients after RBJ "Audio EQ Cookbook".
struct Biquad {
  float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
  float z1 = 0.f, z2 = 0.f;

  enum Type { kPeak = 0, kLowShelf, kHighShelf, kLowPass, kHighPass, kNotch, kBandPass, kAllPass };

  inline float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void reset() { z1 = z2 = 0.f; }
  void setIdentity() { b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; }

  void design(int type, double fs, double f0, double gainDb, double q) {
    if (fs <= 0) { setIdentity(); return; }
    f0 = std::min(std::max(f0, 5.0), fs * 0.49);
    q = std::max(q, 0.05);
    const double A = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double cw = cos(w0), sw = sin(w0);
    const double alpha = sw / (2.0 * q);
    const double sA = sqrt(A);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (type) {
      case kPeak:
        B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
        A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
        break;
      case kLowShelf:
        B0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * alpha);
        B1 = 2 * A * ((A - 1) - (A + 1) * cw);
        B2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * alpha);
        A0 = (A + 1) + (A - 1) * cw + 2 * sA * alpha;
        A1 = -2 * ((A - 1) + (A + 1) * cw);
        A2 = (A + 1) + (A - 1) * cw - 2 * sA * alpha;
        break;
      case kHighShelf:
        B0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * alpha);
        B1 = -2 * A * ((A - 1) + (A + 1) * cw);
        B2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * alpha);
        A0 = (A + 1) - (A - 1) * cw + 2 * sA * alpha;
        A1 = 2 * ((A - 1) - (A + 1) * cw);
        A2 = (A + 1) - (A - 1) * cw - 2 * sA * alpha;
        break;
      case kLowPass:
        B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2;
        A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
        break;
      case kHighPass:
        B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2;
        A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
        break;
      case kNotch:
        B0 = 1; B1 = -2 * cw; B2 = 1;
        A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
        break;
      case kBandPass:  // constant 0 dB peak gain
        B0 = alpha; B1 = 0; B2 = -alpha;
        A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
        break;
      case kAllPass:
        B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha;
        A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
        break;
      default:
        setIdentity();
        return;
    }
    b0 = (float)(B0 / A0); b1 = (float)(B1 / A0); b2 = (float)(B2 / A0);
    a1 = (float)(A1 / A0); a2 = (float)(A2 / A0);
  }
};

// One-pole DC blocker.
struct DcBlocker {
  float r = 0.995f, x1 = 0.f, y1 = 0.f;
  void prepare(double fs, double cutoffHz = 10.0) { r = (float)exp(-2.0 * kPi * cutoffHz / fs); }
  inline float process(float x) {
    float y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }
  void reset() { x1 = y1 = 0.f; }
};

// Radix-2 complex FFT, in place. Inverse is scaled by 1/n.
class Fft {
 public:
  void init(int n) {
    n_ = n;
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    cos_.resize(n / 2);
    sin_.resize(n / 2);
    for (int i = 0; i < n / 2; ++i) {
      double a = -2.0 * kPi * i / n;
      cos_[i] = (float)cos(a);
      sin_[i] = (float)sin(a);
    }
    rev_.resize(n);
    for (int i = 0; i < n; ++i) {
      int r = 0;
      for (int b = 0; b < bits; ++b)
        if (i & (1 << b)) r |= 1 << (bits - 1 - b);
      rev_[i] = r;
    }
  }
  int size() const { return n_; }

  void transform(float* re, float* im, bool inverse) const {
    const int n = n_;
    for (int i = 0; i < n; ++i) {
      int j = rev_[i];
      if (j > i) {
        std::swap(re[i], re[j]);
        std::swap(im[i], im[j]);
      }
    }
    for (int len = 2; len <= n; len <<= 1) {
      const int half = len >> 1;
      const int step = n / len;
      for (int i = 0; i < n; i += len) {
        for (int k = 0; k < half; ++k) {
          const float wr = cos_[k * step];
          const float wi = inverse ? -sin_[k * step] : sin_[k * step];
          const int a = i + k, b = a + half;
          const float tr = re[b] * wr - im[b] * wi;
          const float ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }
    if (inverse) {
      const float s = 1.f / n;
      for (int i = 0; i < n; ++i) {
        re[i] *= s;
        im[i] *= s;
      }
    }
  }

 private:
  int n_ = 0;
  std::vector<float> cos_, sin_;
  std::vector<int> rev_;
};

// Fractional delay line (linear interpolation), power-of-two ring.
class DelayLine {
 public:
  void prepare(int maxSamples) {
    int n = 1;
    while (n < maxSamples + 4) n <<= 1;
    buf_.assign(n, 0.f);
    mask_ = n - 1;
    pos_ = 0;
  }
  void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); }
  inline void push(float x) {
    pos_ = (pos_ + 1) & mask_;
    buf_[pos_] = x;
  }
  // delay in samples (0 = newest)
  inline float read(float delay) const {
    int d = (int)delay;
    float frac = delay - d;
    float a = buf_[(pos_ - d) & mask_];
    float b = buf_[(pos_ - d - 1) & mask_];
    return a + (b - a) * frac;
  }
  inline float readInt(int delay) const { return buf_[(pos_ - delay) & mask_]; }
  int capacity() const { return mask_ + 1; }

 private:
  std::vector<float> buf_;
  int mask_ = 0;
  int pos_ = 0;
};

}  // namespace dsp
}  // namespace ms
