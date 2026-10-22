#pragma once
// Processing parameters shared by the APO (audiodg.exe), the control app and params.bin.
// Plain-old-data with a fixed layout: never reorder fields, only append before `reserved`.
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace ms {

constexpr uint32_t kParamsMagic   = 0x50553143;  // "C1UP"
constexpr uint32_t kParamsVersion = 1;
constexpr int      kMaxEqBands    = 10;

enum EqType : int32_t {
  kEqPeak = 0,
  kEqLowShelf = 1,
  kEqHighShelf = 2,
  kEqLowPass = 3,
  kEqHighPass = 4,
  kEqNotch = 5,
  kEqBandPass = 6,
};

enum FxType : int32_t {
  kFxNone = 0,
  kFxRobot = 1,
  kFxRadio = 2,
  kFxTelephone = 3,
  kFxPitch = 4,
  kFxEcho = 5,
  kFxReverb = 6,
  kFxCount = 7,
};

#pragma pack(push, 4)
struct EqBand {
  int32_t on;
  int32_t type;
  float freq;
  float gainDb;
  float q;
};

struct Params {
  uint32_t magic;
  uint32_t version;
  uint32_t size;

  int32_t bypass;        // 1 = untouched signal (A/B compare)
  int32_t mute;
  float inputGainDb;     // -20 .. +30
  float outputGainDb;    // -20 .. +20

  int32_t hpfOn;         // rumble / low cut
  float hpfFreq;         // 20 .. 300 Hz
  int32_t hpfOrder;      // 2 or 4

  int32_t humOn;         // mains hum notch
  float humFreq;         // 50 or 60
  int32_t humHarmonics;  // 1 .. 8

  int32_t nsOn;          // spectral noise suppression
  float nsReductionDb;   // 3 .. 40 (max attenuation of noise)
  float nsSensitivity;   // 0 .. 1

  int32_t popOn;         // plosive ("digital pop filter")
  float popAmount;       // 0 .. 1

  int32_t gateOn;        // noise gate / expander
  int32_t gateAuto;      // threshold follows the noise floor
  float gateThresholdDb; // -80 .. -10
  float gateRangeDb;     // 3 .. 80
  float gateAttackMs;
  float gateHoldMs;
  float gateReleaseMs;

  int32_t eqOn;
  float warmth;          // -1 .. 1 (low shelf macro)
  float clarity;         // -1 .. 1 (presence macro)
  float air;             // -1 .. 1 (high shelf macro)
  EqBand eq[kMaxEqBands];

  int32_t deessOn;
  float deessFreq;       // 3000 .. 10000
  float deessAmount;     // 0 .. 1

  int32_t agcOn;         // automatic level
  float agcTargetDb;     // -30 .. -10 (LUFS-like)
  float agcMaxGainDb;    // 0 .. 30
  float agcSpeed;        // 0 .. 1

  int32_t compOn;
  float compThresholdDb; // -60 .. 0
  float compRatio;       // 1 .. 20
  float compAttackMs;
  float compReleaseMs;
  float compKneeDb;
  float compMakeupDb;
  int32_t compAutoMakeup;

  int32_t fxType;        // FxType
  float fxMix;           // 0 .. 1
  float fxParam;         // effect specific

  int32_t limOn;
  float limCeilingDb;    // -12 .. 0

  uint32_t reserved[32];
};
#pragma pack(pop)

static_assert(sizeof(EqBand) == 20, "EqBand layout");

inline float ClampF(float v, float lo, float hi) {
  if (!(v == v)) return lo;  // NaN
  return v < lo ? lo : (v > hi ? hi : v);
}
inline int32_t ClampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int32_t Bool01(int32_t v) { return v ? 1 : 0; }

inline void DefaultEq(EqBand* eq) {
  static const struct { int32_t type; float f; float q; } kBands[kMaxEqBands] = {
      {kEqLowShelf, 100.f, 0.7f}, {kEqPeak, 200.f, 1.0f},   {kEqPeak, 400.f, 1.0f},
      {kEqPeak, 800.f, 1.0f},     {kEqPeak, 1600.f, 1.0f},  {kEqPeak, 3200.f, 1.0f},
      {kEqPeak, 5000.f, 1.0f},    {kEqPeak, 8000.f, 1.0f},  {kEqHighShelf, 12000.f, 0.7f},
      {kEqPeak, 2500.f, 1.0f},
  };
  for (int i = 0; i < kMaxEqBands; ++i) {
    eq[i].on = i < 9 ? 1 : 0;
    eq[i].type = kBands[i].type;
    eq[i].freq = kBands[i].f;
    eq[i].gainDb = 0.f;
    eq[i].q = kBands[i].q;
  }
}

inline void DefaultParams(Params* p) {
  memset(p, 0, sizeof(*p));
  p->magic = kParamsMagic;
  p->version = kParamsVersion;
  p->size = sizeof(Params);
  p->inputGainDb = 0.f;
  p->outputGainDb = 0.f;
  p->hpfOn = 1; p->hpfFreq = 80.f; p->hpfOrder = 2;
  p->humOn = 0; p->humFreq = 50.f; p->humHarmonics = 4;
  p->nsOn = 1; p->nsReductionDb = 18.f; p->nsSensitivity = 0.5f;
  p->popOn = 1; p->popAmount = 0.5f;
  p->gateOn = 0; p->gateAuto = 1; p->gateThresholdDb = -50.f; p->gateRangeDb = 18.f;
  p->gateAttackMs = 2.f; p->gateHoldMs = 150.f; p->gateReleaseMs = 180.f;
  p->eqOn = 1; p->warmth = 0.f; p->clarity = 0.f; p->air = 0.f;
  DefaultEq(p->eq);
  p->deessOn = 1; p->deessFreq = 6500.f; p->deessAmount = 0.35f;
  p->agcOn = 0; p->agcTargetDb = -18.f; p->agcMaxGainDb = 12.f; p->agcSpeed = 0.4f;
  p->compOn = 1; p->compThresholdDb = -22.f; p->compRatio = 3.f; p->compAttackMs = 5.f;
  p->compReleaseMs = 120.f; p->compKneeDb = 6.f; p->compMakeupDb = 0.f; p->compAutoMakeup = 1;
  p->fxType = kFxNone; p->fxMix = 0.5f; p->fxParam = 0.5f;
  p->limOn = 1; p->limCeilingDb = -1.f;
}

// Brings any (possibly corrupted or foreign) parameter block into safe ranges.
inline void SanitizeParams(Params* p) {
  p->magic = kParamsMagic;
  p->version = kParamsVersion;
  p->size = sizeof(Params);
  p->bypass = Bool01(p->bypass);
  p->mute = Bool01(p->mute);
  p->inputGainDb = ClampF(p->inputGainDb, -20.f, 30.f);
  p->outputGainDb = ClampF(p->outputGainDb, -20.f, 20.f);
  p->hpfOn = Bool01(p->hpfOn);
  p->hpfFreq = ClampF(p->hpfFreq, 20.f, 300.f);
  p->hpfOrder = p->hpfOrder >= 4 ? 4 : 2;
  p->humOn = Bool01(p->humOn);
  p->humFreq = p->humFreq > 55.f ? 60.f : 50.f;
  p->humHarmonics = ClampI(p->humHarmonics, 1, 8);
  p->nsOn = Bool01(p->nsOn);
  p->nsReductionDb = ClampF(p->nsReductionDb, 3.f, 40.f);
  p->nsSensitivity = ClampF(p->nsSensitivity, 0.f, 1.f);
  p->popOn = Bool01(p->popOn);
  p->popAmount = ClampF(p->popAmount, 0.f, 1.f);
  p->gateOn = Bool01(p->gateOn);
  p->gateAuto = Bool01(p->gateAuto);
  p->gateThresholdDb = ClampF(p->gateThresholdDb, -80.f, -10.f);
  p->gateRangeDb = ClampF(p->gateRangeDb, 3.f, 80.f);
  p->gateAttackMs = ClampF(p->gateAttackMs, 0.1f, 50.f);
  p->gateHoldMs = ClampF(p->gateHoldMs, 0.f, 1000.f);
  p->gateReleaseMs = ClampF(p->gateReleaseMs, 10.f, 2000.f);
  p->eqOn = Bool01(p->eqOn);
  p->warmth = ClampF(p->warmth, -1.f, 1.f);
  p->clarity = ClampF(p->clarity, -1.f, 1.f);
  p->air = ClampF(p->air, -1.f, 1.f);
  for (int i = 0; i < kMaxEqBands; ++i) {
    EqBand& b = p->eq[i];
    b.on = Bool01(b.on);
    b.type = ClampI(b.type, kEqPeak, kEqBandPass);
    b.freq = ClampF(b.freq, 20.f, 20000.f);
    b.gainDb = ClampF(b.gainDb, -24.f, 24.f);
    b.q = ClampF(b.q, 0.1f, 18.f);
  }
  p->deessOn = Bool01(p->deessOn);
  p->deessFreq = ClampF(p->deessFreq, 3000.f, 10000.f);
  p->deessAmount = ClampF(p->deessAmount, 0.f, 1.f);
  p->agcOn = Bool01(p->agcOn);
  p->agcTargetDb = ClampF(p->agcTargetDb, -30.f, -10.f);
  p->agcMaxGainDb = ClampF(p->agcMaxGainDb, 0.f, 30.f);
  p->agcSpeed = ClampF(p->agcSpeed, 0.f, 1.f);
  p->compOn = Bool01(p->compOn);
  p->compThresholdDb = ClampF(p->compThresholdDb, -60.f, 0.f);
  p->compRatio = ClampF(p->compRatio, 1.f, 20.f);
  p->compAttackMs = ClampF(p->compAttackMs, 0.1f, 200.f);
  p->compReleaseMs = ClampF(p->compReleaseMs, 10.f, 2000.f);
  p->compKneeDb = ClampF(p->compKneeDb, 0.f, 24.f);
  p->compMakeupDb = ClampF(p->compMakeupDb, -12.f, 24.f);
  p->compAutoMakeup = Bool01(p->compAutoMakeup);
  p->fxType = ClampI(p->fxType, kFxNone, kFxCount - 1);
  p->fxMix = ClampF(p->fxMix, 0.f, 1.f);
  p->fxParam = ClampF(p->fxParam, 0.f, 1.f);
  p->limOn = Bool01(p->limOn);
  p->limCeilingDb = ClampF(p->limCeilingDb, -12.f, 0.f);
}

}  // namespace ms
