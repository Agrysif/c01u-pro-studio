// Offline checks for the DSP chain. Build: see build.ps1 (target "tests").
// Usage: dsp_test.exe                 -> run all checks
//        dsp_test.exe in.wav out.wav  -> process a 16-bit/float WAV with default parameters
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include "../src/dsp/chain.h"

using namespace ms;
using namespace ms::dsp;

static int g_failed = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (cond) {                                  \
      printf("  ok   ");                         \
    } else {                                     \
      printf("  FAIL ");                         \
      ++g_failed;                                \
    }                                            \
    printf(__VA_ARGS__);                         \
    printf("\n");                                \
  } while (0)

static const double kFs = 48000.0;

static float RmsDb(const std::vector<float>& v, size_t from, size_t to) {
  double s = 0;
  to = std::min(to, v.size());
  for (size_t i = from; i < to; ++i) s += (double)v[i] * v[i];
  return (float)(10.0 * log10(s / std::max<size_t>(1, to - from) + 1e-20));
}
static float PeakDb(const std::vector<float>& v) {
  float p = 0;
  for (float x : v) p = std::max(p, fabsf(x));
  return LinToDb(p);
}
static bool AllFinite(const std::vector<float>& v) {
  for (float x : v)
    if (!std::isfinite(x)) return false;
  return true;
}

// Harmonic "voiced" sound: f0 with vibrato and 1/h roll-off up to 4 kHz. With `syllables`
// the level follows a ~2.5 Hz syllable rhythm with short gaps, like running speech.
static void AddVoice(std::vector<float>& v, size_t from, size_t len, float f0, float levelDb, bool syllables = false) {
  std::vector<float> tmp(len, 0.f);
  double phase = 0;
  for (size_t i = 0; i < len; ++i) {
    const double t = (double)i / kFs;
    const double f = f0 * (1.0 + 0.03 * sin(2 * kPi * 5.0 * t) + 0.08 * sin(2 * kPi * 0.7 * t));
    phase += 2 * kPi * f / kFs;
    float s = 0;
    for (int h = 1; h * f0 < 4000; ++h) s += (float)sin(h * phase) / h;
    if (syllables) s *= (float)pow(std::max(0.0, sin(2 * kPi * 2.5 * t)), 0.5);
    tmp[i] = s;
  }
  float pk = 0;
  for (float x : tmp) pk = std::max(pk, fabsf(x));
  const float g = DbToLin(levelDb) / pk;
  for (size_t i = 0; i < len && from + i < v.size(); ++i) {
    float e = std::min(1.f, std::min(i, len - i) / (float)(kFs * 0.02));
    v[from + i] += tmp[i] * g * e;
  }
}
static void AddNoise(std::vector<float>& v, float levelDb, unsigned seed = 1) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> nd(0.f, 1.f);
  const float g = DbToLin(levelDb);
  for (auto& x : v) x += nd(rng) * g;
}

static void ProcessBlocks(Chain& c, std::vector<float>& v, int block = 480) {
  for (size_t i = 0; i < v.size(); i += block) c.process(v.data() + i, (int)std::min<size_t>(block, v.size() - i));
}

static Params AllOff() {
  Params p;
  DefaultParams(&p);
  p.hpfOn = p.humOn = p.nsOn = p.popOn = p.gateOn = p.eqOn = p.deessOn = p.agcOn = p.compOn = p.limOn = 0;
  p.gateAuto = 0;
  p.fxType = kFxNone;
  return p;
}

static void TestNsReconstruction() {
  printf("noise suppressor: perfect reconstruction when disabled\n");
  NoiseSuppressor ns;
  ns.prepare(kFs);
  ns.setParams(false, 20, 0.5f);
  std::vector<float> in(kFs), out;
  AddNoise(in, -20);
  out = in;
  for (size_t i = 0; i < out.size(); i += 441) ns.process(out.data() + i, (int)std::min<size_t>(441, out.size() - i));
  const int lat = ns.latency();
  double err = 0;
  for (size_t i = lat + 2048; i < in.size(); ++i) err = std::max(err, (double)fabsf(out[i] - in[i - lat]));
  CHECK(err < 1e-4, "max reconstruction error %.2e (latency %d samples = %.1f ms)", err, lat, lat * 1000.0 / kFs);
}

static void TestNsReduction() {
  printf("noise suppressor: removes hiss, keeps voice\n");
  const size_t n = (size_t)(kFs * 4);
  std::vector<float> clean(n, 0.f), noisy;
  AddVoice(clean, (size_t)(kFs * 2.0), (size_t)(kFs * 1.5), 140.f, -14.f, true);
  noisy = clean;
  AddNoise(noisy, -50.f, 7);
  Params p = AllOff();
  p.nsOn = 1;
  p.nsReductionDb = 20;
  Chain c;
  c.prepare(kFs);
  c.setParams(p);
  std::vector<float> out = noisy;
  ProcessBlocks(c, out);
  const int lat = c.latency();
  const float noiseIn = RmsDb(noisy, (size_t)(kFs * 1.0), (size_t)(kFs * 1.9));
  const float noiseOut = RmsDb(out, (size_t)(kFs * 1.0) + lat, (size_t)(kFs * 1.9) + lat);
  const float voiceIn = RmsDb(noisy, (size_t)(kFs * 2.3), (size_t)(kFs * 3.2));
  const float voiceOut = RmsDb(out, (size_t)(kFs * 2.3) + lat, (size_t)(kFs * 3.2) + lat);
  CHECK(noiseIn - noiseOut > 12.f, "noise %.1f dB -> %.1f dB (reduction %.1f dB)", noiseIn, noiseOut, noiseIn - noiseOut);
  CHECK(fabsf(voiceIn - voiceOut) < 2.f, "voice %.1f dB -> %.1f dB", voiceIn, voiceOut);
  // residual error against the clean voice
  double e = 0, s = 0;
  for (size_t i = (size_t)(kFs * 2.3); i < (size_t)(kFs * 3.2); ++i) {
    const double d = out[i + lat] - clean[i];
    e += d * d;
    s += (double)clean[i] * clean[i];
  }
  const double snrIn = RmsDb(clean, (size_t)(kFs * 2.3), (size_t)(kFs * 3.2)) - (-50.0);
  CHECK(10 * log10(s / e) > 15.0, "voice-to-error ratio %.1f dB (input SNR %.1f dB)", 10 * log10(s / e), snrIn);

  // long noise only: the minimum-statistics cap must not weaken suppression
  {
    std::vector<float> nz((size_t)(kFs * 8), 0.f);
    AddNoise(nz, -50.f, 21);
    // +15 dB noise step at 8 s, then 8 more seconds
    std::vector<float> step((size_t)(kFs * 8), 0.f);
    AddNoise(step, -35.f, 22);
    nz.insert(nz.end(), step.begin(), step.end());
    std::vector<float> o = nz;
    Chain c3;
    c3.prepare(kFs);
    c3.setParams(p);
    ProcessBlocks(c3, o);
    const float r1 = RmsDb(nz, (size_t)(kFs * 6), (size_t)(kFs * 8)) - RmsDb(o, (size_t)(kFs * 6) + lat, (size_t)(kFs * 8) + lat);
    const float r2 = RmsDb(nz, (size_t)(kFs * 14), (size_t)(kFs * 16)) - RmsDb(o, (size_t)(kFs * 14) + lat, (size_t)(kFs * 16) + lat);
    CHECK(r1 > 17.f, "steady noise, 6-8 s: reduction %.1f dB", r1);
    CHECK(r2 > 14.f, "noise +15 dB step, 6-8 s after the step: reduction %.1f dB", r2);
  }

  // a sustained note (singing) must not be eaten by the noise tracker
  std::vector<float> note(n, 0.f);
  AddVoice(note, (size_t)(kFs * 1.0), (size_t)(kFs * 2.5), 220.f, -14.f, false);
  AddNoise(note, -50.f, 8);
  std::vector<float> nout = note;
  Chain c2;
  c2.prepare(kFs);
  c2.setParams(p);
  ProcessBlocks(c2, nout);
  const float a = RmsDb(note, (size_t)(kFs * 3.0), (size_t)(kFs * 3.4));
  const float b = RmsDb(nout, (size_t)(kFs * 3.0) + lat, (size_t)(kFs * 3.4) + lat);
  CHECK(fabsf(a - b) < 1.5f, "sustained note after 2 s: %.1f dB -> %.1f dB", a, b);
}

static void TestLimiter() {
  printf("limiter: ceiling is never exceeded\n");
  Limiter lim;
  lim.prepare(kFs);
  lim.setParams(true, -1.f);
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> ud(-4.f, 4.f);
  float peak = 0;
  for (int i = 0; i < (int)kFs * 2; ++i) {
    float x = (i % 1000 < 3) ? ud(rng) : ud(rng) * 0.05f;
    peak = std::max(peak, fabsf(lim.process(x)));
  }
  CHECK(peak <= DbToLin(-1.f) + 1e-6f, "output peak %.3f dBFS (ceiling -1 dBFS)", LinToDb(peak));
  // transparency below the ceiling
  Limiter l2;
  l2.prepare(kFs);
  l2.setParams(true, -1.f);
  std::vector<float> in(4800), out(4800);
  for (int i = 0; i < 4800; ++i) in[i] = 0.5f * sinf((float)(2 * kPi * 440 * i / kFs));
  for (int i = 0; i < 4800; ++i) out[i] = l2.process(in[i]);
  const int lat = l2.latency();
  float err = 0;
  for (int i = lat + 10; i < 4800; ++i) err = std::max(err, fabsf(out[i] - in[i - lat]));
  CHECK(err < 1e-5f, "transparent below ceiling (err %.1e, latency %d)", err, lat);
}

static void TestDeEsser() {
  printf("de-esser: reacts to sibilance, not to voiced sound\n");
  DeEsser d;
  d.prepare(kFs);
  d.setParams(true, 6500.f, 0.5f);
  // voiced
  std::vector<float> v((size_t)kFs, 0.f);
  AddVoice(v, 0, v.size(), 150.f, -12.f);
  float minG = 1.f;
  for (float x : v) d.process(x);
  minG = d.takeMinGain();
  CHECK(LinToDb(minG) > -1.f, "voiced: min gain %.1f dB", LinToDb(minG));
  // sibilant: noise high-passed at 4 kHz
  std::vector<float> s((size_t)kFs, 0.f);
  AddNoise(s, -18.f, 11);
  Biquad hp1, hp2;
  hp1.design(Biquad::kHighPass, kFs, 4000, 0, 0.707);
  hp2.design(Biquad::kHighPass, kFs, 4000, 0, 0.707);
  for (auto& x : s) x = hp2.process(hp1.process(x));
  for (float x : s) d.process(x);
  minG = d.takeMinGain();
  CHECK(LinToDb(minG) < -3.f, "sibilant: min gain %.1f dB", LinToDb(minG));
}

static void TestPopFilter() {
  printf("pop filter: ducks plosive bursts, leaves a deep voice alone\n");
  PopFilter pf;
  pf.prepare(kFs);
  pf.setParams(true, 0.6f);
  std::vector<float> v((size_t)kFs, 0.f);
  AddVoice(v, 0, v.size(), 100.f, -12.f);  // deep male voice
  for (float x : v) pf.process(x);
  float g = pf.takeMinGain();
  CHECK(LinToDb(g) > -2.f, "deep voice: min gain %.1f dB", LinToDb(g));
  // plosive: strong 40 Hz thump decaying in 60 ms plus a little voice
  std::vector<float> p((size_t)(kFs * 0.3), 0.f);
  AddVoice(p, 0, p.size(), 140.f, -26.f);
  for (size_t i = 0; i < p.size(); ++i) p[i] += 0.7f * expf(-(float)i / (float)(kFs * 0.06)) * sinf((float)(2 * kPi * 40 * i / kFs));
  for (float x : p) pf.process(x);
  g = pf.takeMinGain();
  CHECK(LinToDb(g) < -6.f, "plosive: min gain %.1f dB", LinToDb(g));
}

static void TestGateAndComp() {
  printf("gate and compressor\n");
  Params p = AllOff();
  p.gateOn = 1;
  p.gateAuto = 0;
  p.gateThresholdDb = -45;
  p.gateRangeDb = 20;
  Chain c;
  c.prepare(kFs);
  c.setParams(p);
  std::vector<float> v((size_t)kFs * 2, 0.f);
  AddNoise(v, -60.f, 5);
  std::vector<float> o = v;
  ProcessBlocks(c, o);
  const float red = RmsDb(v, (size_t)kFs, (size_t)kFs * 2) - RmsDb(o, (size_t)kFs, (size_t)kFs * 2);
  CHECK(fabsf(red - 20.f) < 1.5f, "closed gate attenuation %.1f dB (range 20)", red);

  Compressor comp;
  comp.prepare(kFs);
  comp.setParams(true, -20.f, 4.f, 5.f, 100.f, 0.f, 0.f, false);
  std::vector<float> s((size_t)kFs, 0.f);
  for (size_t i = 0; i < s.size(); ++i) s[i] = DbToLin(-6.f) * sinf((float)(2 * kPi * 1000 * i / kFs));
  for (auto& x : s) x = comp.process(x);
  float pk = 0;
  for (size_t i = s.size() / 2; i < s.size(); ++i) pk = std::max(pk, fabsf(s[i]));
  CHECK(fabsf(LinToDb(pk) - (-16.5f)) < 1.5f, "compressed -6 dBFS sine peak: %.1f dBFS (expected ~-16.5)", LinToDb(pk));
}

static void TestFullChainAndFx() {
  printf("full chain + every effect stays finite and under the ceiling\n");
  for (int fx = kFxNone; fx < kFxCount; ++fx) {
    Params p;
    DefaultParams(&p);
    p.agcOn = 1;
    p.gateOn = 1;
    p.humOn = 1;
    p.inputGainDb = 12;
    p.fxType = fx;
    p.fxMix = 0.8f;
    Chain c;
    c.prepare(kFs);
    c.setParams(p);
    std::vector<float> v((size_t)kFs * 3, 0.f);
    AddNoise(v, -48.f, 9);
    AddVoice(v, (size_t)(kFs * 0.5), (size_t)(kFs * 2), 120.f, -3.f);
    ProcessBlocks(c, v, 441);
    CHECK(AllFinite(v) && PeakDb(v) <= -1.f + 1e-3f, "fx %d: peak %.2f dBFS", fx, PeakDb(v));
  }
}

static void TestSampleRates() {
  printf("other sample rates\n");
  const double rates[] = {16000, 44100, 96000, 192000};
  for (double fs : rates) {
    Params p;
    DefaultParams(&p);
    Chain c;
    c.prepare(fs);
    c.setParams(p);
    std::vector<float> v((size_t)fs, 0.f);
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.f, 0.1f);
    for (auto& x : v) x = nd(rng);
    ProcessBlocks(c, v, (int)(fs / 100));
    CHECK(AllFinite(v), "%.0f Hz: finite, latency %.1f ms", fs, c.latency() * 1000.0 / fs);
  }
}

static void TestPerformance() {
  printf("performance (everything on, 60 s of audio)\n");
  Params p;
  DefaultParams(&p);
  p.agcOn = p.gateOn = p.humOn = 1;
  p.fxType = kFxReverb;
  Chain c;
  c.prepare(kFs);
  c.setParams(p);
  std::vector<float> v((size_t)kFs * 60, 0.f);
  AddNoise(v, -45.f, 2);
  AddVoice(v, (size_t)kFs * 5, (size_t)kFs * 40, 130.f, -10.f);
  auto t0 = std::chrono::high_resolution_clock::now();
  ProcessBlocks(c, v, 480);
  auto t1 = std::chrono::high_resolution_clock::now();
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  CHECK(sec < 3.0, "60 s processed in %.3f s -> %.2f%% of one CPU core", sec, sec / 60.0 * 100.0);
}

// ---------------------------------------------------------------- WAV helpers
static bool ReadWav(const char* path, std::vector<float>& mono, int& rate) {
  FILE* f = nullptr;
  if (fopen_s(&f, path, "rb") || !f) return false;
  std::vector<uint8_t> d;
  fseek(f, 0, SEEK_END);
  d.resize(ftell(f));
  fseek(f, 0, SEEK_SET);
  fread(d.data(), 1, d.size(), f);
  fclose(f);
  if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) return false;
  int fmt = 0, ch = 0, bits = 0;
  size_t pos = 12;
  while (pos + 8 <= d.size()) {
    uint32_t sz = *(uint32_t*)(d.data() + pos + 4);
    const uint8_t* body = d.data() + pos + 8;
    if (!memcmp(d.data() + pos, "fmt ", 4)) {
      fmt = *(uint16_t*)body;
      ch = *(uint16_t*)(body + 2);
      rate = *(uint32_t*)(body + 4);
      bits = *(uint16_t*)(body + 14);
      if (fmt == 0xFFFE) fmt = *(uint16_t*)(body + 24);
    } else if (!memcmp(d.data() + pos, "data", 4)) {
      sz = (uint32_t)std::min<size_t>(sz, d.size() - pos - 8);
      const int bps = bits / 8;
      const size_t frames = sz / (bps * ch);
      mono.assign(frames, 0.f);
      for (size_t i = 0; i < frames; ++i) {
        float s = 0;
        for (int c = 0; c < ch; ++c) {
          const uint8_t* q = body + (i * ch + c) * bps;
          if (fmt == 3 && bits == 32) s += *(const float*)q;
          else if (bits == 16) s += *(const int16_t*)q / 32768.f;
          else if (bits == 24) s += ((int32_t)((q[0] << 8) | (q[1] << 16) | (q[2] << 24)) >> 8) / 8388608.f;
          else if (bits == 32) s += *(const int32_t*)q / 2147483648.f;
        }
        mono[i] = s / ch;
      }
      return true;
    }
    pos += 8 + sz + (sz & 1);
  }
  return false;
}

static bool WriteWav(const char* path, const std::vector<float>& mono, int rate) {
  FILE* f = nullptr;
  if (fopen_s(&f, path, "wb") || !f) return false;
  const uint32_t dataBytes = (uint32_t)mono.size() * 2;
  struct {
    char riff[4]; uint32_t size; char wave[4]; char fmt[4]; uint32_t fmtSize;
    uint16_t format, channels; uint32_t rate, byteRate; uint16_t align, bits; char data[4]; uint32_t dataSize;
  } h = {{'R', 'I', 'F', 'F'}, 36 + dataBytes, {'W', 'A', 'V', 'E'}, {'f', 'm', 't', ' '}, 16, 1, 1,
         (uint32_t)rate, (uint32_t)rate * 2, 2, 16, {'d', 'a', 't', 'a'}, dataBytes};
  fwrite(&h, sizeof(h), 1, f);
  for (float x : mono) {
    int16_t s = (int16_t)lrintf(Clamp(x, -1.f, 1.f) * 32767.f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
  return true;
}

int main(int argc, char** argv) {
  if (argc >= 3) {
    std::vector<float> v;
    int rate = 0;
    if (!ReadWav(argv[1], v, rate)) {
      printf("cannot read %s\n", argv[1]);
      return 2;
    }
    Params p;
    DefaultParams(&p);
    Chain c;
    c.prepare(rate);
    c.setParams(p);
    ProcessBlocks(c, v, rate / 100);
    WriteWav(argv[2], v, rate);
    printf("processed %zu samples at %d Hz\n", v.size(), rate);
    return 0;
  }
  TestNsReconstruction();
  TestNsReduction();
  TestLimiter();
  TestDeEsser();
  TestPopFilter();
  TestGateAndComp();
  TestFullChainAndFx();
  TestSampleRates();
  TestPerformance();
  printf(g_failed ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_failed);
  return g_failed ? 1 : 0;
}
