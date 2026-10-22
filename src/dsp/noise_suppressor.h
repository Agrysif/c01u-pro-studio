#pragma once
// Spectral noise suppression without neural networks.
//  * STFT with sqrt-Hann windows, 75% overlap (perfect reconstruction when gains are 1)
//  * noise PSD: speech-presence-probability tracker (Gerkmann & Hendriks, 2012), capped by
//    a 5-second minimum-statistics floor so sustained vowels or sung notes are not learned
//    as noise (the SPP tracker alone absorbs any steady tone within ~1.5 s)
//  * gain: decision-directed a-priori SNR + Wiener rule, spectral floor,
//    frequency smoothing and asymmetric time smoothing against "musical noise"
// Constant latency of fftSize samples whether enabled or not, so toggling never clicks.
#include "basics.h"

namespace ms {
namespace dsp {

class NoiseSuppressor {
 public:
  void prepare(double fs) {
    fs_ = fs;
    n_ = fs <= 25000 ? 256 : fs <= 50000 ? 512 : fs <= 100000 ? 1024 : 2048;
    hop_ = n_ / 4;
    bins_ = n_ / 2 + 1;
    fft_.init(n_);
    win_.resize(n_);
    for (int i = 0; i < n_; ++i) win_[i] = (float)sqrt(0.5 - 0.5 * cos(2.0 * kPi * i / n_));
    inFifo_.assign(n_, 0.f);
    outFifo_.assign(hop_, 0.f);
    accum_.assign(n_, 0.f);
    re_.assign(n_, 0.f);
    im_.assign(n_, 0.f);
    pow_.assign(bins_, 0.f);
    noise_.assign(bins_, 0.f);
    pbar_.assign(bins_, 0.f);
    raw_.assign(bins_, 1.f);
    smooth_.assign(bins_, 1.f);
    clean_.assign(bins_, 0.f);
    psd_.assign(bins_, 0.f);
    minCur_.assign(bins_, 0.f);
    for (auto& m : subMin_) m.assign(bins_, 0.f);

    const double hopSec = (double)hop_ / fs;
    alphaPsd_ = (float)exp(-hopSec / 0.076);
    subLen_ = std::max(1, (int)(0.625 / hopSec));  // 8 sub-windows = 5 s
    alphaNoise_ = (float)exp(-hopSec / 0.0358);  // 0.8 per 8 ms frame in the paper
    alphaP_ = (float)exp(-hopSec / 0.0759);      // 0.9 per 8 ms frame
    alphaDD_ = (float)exp(-hopSec / 0.396);      // 0.98 per 8 ms frame
    gainAtt_ = (float)exp(-hopSec / 0.003);
    gainRel_ = (float)exp(-hopSec / 0.070);
    initFrames_ = (int)(0.25 / hopSec);
    reset();
  }

  void reset() {
    std::fill(inFifo_.begin(), inFifo_.end(), 0.f);
    std::fill(outFifo_.begin(), outFifo_.end(), 0.f);
    std::fill(accum_.begin(), accum_.end(), 0.f);
    std::fill(noise_.begin(), noise_.end(), 0.f);
    std::fill(pbar_.begin(), pbar_.end(), 0.f);
    std::fill(raw_.begin(), raw_.end(), 1.f);
    std::fill(smooth_.begin(), smooth_.end(), 1.f);
    std::fill(clean_.begin(), clean_.end(), 0.f);
    rover_ = n_ - hop_;
    frames_ = 0;
    subPos_ = 0;
    reductionDb_ = 0.f;
  }

  // Forget the learned noise and learn it again quickly (wizard "measure silence").
  void relearn() { frames_ = 0; }

  void setParams(bool on, float reductionDb, float sensitivity) {
    on_ = on;
    floor_ = DbToLin(-Clamp(reductionDb, 0.f, 60.f));
    overSub_ = 0.8f + 1.7f * Clamp(sensitivity, 0.f, 1.f);
  }

  int latency() const { return n_; }
  float reductionDb() const { return reductionDb_; }

  void process(float* x, int count) {
    const int lat = n_ - hop_;
    for (int i = 0; i < count; ++i) {
      inFifo_[rover_] = x[i];
      x[i] = outFifo_[rover_ - lat];
      if (++rover_ >= n_) {
        rover_ = lat;
        processFrame();
      }
    }
  }

 private:
  void processFrame() {
    const int n = n_, bins = bins_;
    for (int k = 0; k < n; ++k) {
      re_[k] = inFifo_[k] * win_[k];
      im_[k] = 0.f;
    }
    fft_.transform(re_.data(), im_.data(), false);

    // speech presence probability with a fixed a-priori SNR of 15 dB under H1
    const float xiH1 = 31.6227766f;
    const float pFac = 1.f + xiH1;
    const float eFac = xiH1 / (1.f + xiH1);
    const bool learning = frames_ < initFrames_;

    const bool subDone = !learning && ++subPos_ >= subLen_;
    if (subDone) subPos_ = 0;
    for (int k = 0; k < bins; ++k) {
      const float P = re_[k] * re_[k] + im_[k] * im_[k] + 1e-20f;
      pow_[k] = P;
      if (learning) {
        noise_[k] = frames_ == 0 ? P : noise_[k] + (P - noise_[k]) / (float)(frames_ + 1);
        if (frames_ == initFrames_ - 1) {
          psd_[k] = minCur_[k] = noise_[k];
          for (auto& m : subMin_) m[k] = noise_[k];
        }
      } else {
        const float g = P / noise_[k];
        float p = 1.f / (1.f + pFac * expf(-std::min(g * eFac, 80.f)));
        pbar_[k] = alphaP_ * pbar_[k] + (1.f - alphaP_) * p;
        if (pbar_[k] > 0.99f) p = std::min(p, 0.99f);  // stagnation guard
        const float est = (1.f - p) * P + p * noise_[k];
        noise_[k] = alphaNoise_ * noise_[k] + (1.f - alphaNoise_) * est;
        // long-term minimum of the smoothed periodogram
        psd_[k] = alphaPsd_ * psd_[k] + (1.f - alphaPsd_) * P;
        if (psd_[k] < minCur_[k]) minCur_[k] = psd_[k];
        float m = minCur_[k];
        for (int u = 0; u < kSubWindows; ++u) m = std::min(m, subMin_[u][k]);
        noise_[k] = std::min(noise_[k], m * kMinBias);
        if (subDone) {
          subMin_[subIdx_][k] = minCur_[k];
          minCur_[k] = psd_[k];
        }
      }
      const float lam = noise_[k] * overSub_ + 1e-20f;
      const float gamma = P / lam;
      float xi = alphaDD_ * clean_[k] / lam + (1.f - alphaDD_) * std::max(gamma - 1.f, 0.f);
      xi = std::max(xi, 1e-4f);
      const float w = xi / (1.f + xi);
      raw_[k] = w;
      clean_[k] = w * w * P;
    }
    if (subDone) subIdx_ = (subIdx_ + 1) % kSubWindows;

    double sumP = 0.0, sumPG = 0.0;
    for (int k = 0; k < bins; ++k) {
      const float l = raw_[k > 0 ? k - 1 : 0];
      const float r = raw_[k < bins - 1 ? k + 1 : bins - 1];
      float g = 0.25f * l + 0.5f * raw_[k] + 0.25f * r;
      g = on_ ? std::max(g, floor_) : 1.f;
      float& s = smooth_[k];
      s = g + (s - g) * (g > s ? gainAtt_ : gainRel_);
      sumP += pow_[k];
      sumPG += pow_[k] * s * s;
      re_[k] *= s;
      im_[k] *= s;
      if (k > 0 && k < n / 2) {
        re_[n - k] *= s;
        im_[n - k] *= s;
      }
    }
    reductionDb_ = sumP > 1e-12 ? (float)(10.0 * log10(sumPG / sumP + 1e-12)) : 0.f;

    fft_.transform(re_.data(), im_.data(), true);
    for (int k = 0; k < n; ++k) accum_[k] += re_[k] * win_[k] * 0.5f;  // Hann^2 sums to 2 at 75%
    for (int k = 0; k < hop_; ++k) outFifo_[k] = accum_[k];
    memmove(accum_.data(), accum_.data() + hop_, sizeof(float) * (n - hop_));
    std::fill(accum_.begin() + (n - hop_), accum_.end(), 0.f);
    memmove(inFifo_.data(), inFifo_.data() + hop_, sizeof(float) * (n - hop_));
    if (frames_ < 0x7fffffff) ++frames_;
  }

  static constexpr int kSubWindows = 8;
  static constexpr float kMinBias = 4.0f;  // minimum of a smoothed periodogram underestimates the mean

  double fs_ = 48000;
  int n_ = 512, hop_ = 128, bins_ = 257;
  int rover_ = 0, frames_ = 0, initFrames_ = 90;
  int subLen_ = 234, subPos_ = 0, subIdx_ = 0;
  float alphaPsd_ = 0.96f;
  std::vector<float> psd_, minCur_, subMin_[kSubWindows];
  bool on_ = true;
  float floor_ = 0.125f, overSub_ = 1.65f;
  float alphaNoise_ = 0.9f, alphaP_ = 0.9f, alphaDD_ = 0.98f, gainAtt_ = 0.4f, gainRel_ = 0.96f;
  float reductionDb_ = 0.f;
  Fft fft_;
  std::vector<float> win_, inFifo_, outFifo_, accum_, re_, im_;
  std::vector<float> pow_, noise_, pbar_, raw_, smooth_, clean_;
};

}  // namespace dsp
}  // namespace ms
