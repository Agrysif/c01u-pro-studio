#include "settings.h"

#include <math.h>
#include <stddef.h>

#include "common.h"

namespace app {

namespace {

enum Kind { kB, kF, kI };
struct Field {
  const char* name;
  Kind kind;
  size_t offset;
};

#define PF(n, k) {#n, k, offsetof(ms::Params, n)}
const Field kFields[] = {
    PF(bypass, kB),          PF(mute, kB),
    PF(inputGainDb, kF),     PF(outputGainDb, kF),
    PF(hpfOn, kB),           PF(hpfFreq, kF),          PF(hpfOrder, kI),
    PF(humOn, kB),           PF(humFreq, kF),          PF(humHarmonics, kI),
    PF(nsOn, kB),            PF(nsReductionDb, kF),    PF(nsSensitivity, kF),
    PF(popOn, kB),           PF(popAmount, kF),
    PF(gateOn, kB),          PF(gateAuto, kB),         PF(gateThresholdDb, kF), PF(gateRangeDb, kF),
    PF(gateAttackMs, kF),    PF(gateHoldMs, kF),       PF(gateReleaseMs, kF),
    PF(eqOn, kB),            PF(warmth, kF),           PF(clarity, kF),         PF(air, kF),
    PF(deessOn, kB),         PF(deessFreq, kF),        PF(deessAmount, kF),
    PF(agcOn, kB),           PF(agcTargetDb, kF),      PF(agcMaxGainDb, kF),    PF(agcSpeed, kF),
    PF(compOn, kB),          PF(compThresholdDb, kF),  PF(compRatio, kF),       PF(compAttackMs, kF),
    PF(compReleaseMs, kF),   PF(compKneeDb, kF),       PF(compMakeupDb, kF),    PF(compAutoMakeup, kB),
    PF(fxType, kI),          PF(fxMix, kF),            PF(fxParam, kF),
    PF(limOn, kB),           PF(limCeilingDb, kF),
};
#undef PF

float Round3(float v) { return (float)(llround((double)v * 1000.0) / 1000.0); }

}  // namespace

Json ParamsToJson(const ms::Params& p) {
  Json j = Json::Object();
  const char* base = (const char*)&p;
  for (const Field& f : kFields) {
    switch (f.kind) {
      case kB: j.set(f.name, *(const int32_t*)(base + f.offset) != 0); break;
      case kI: j.set(f.name, (int)*(const int32_t*)(base + f.offset)); break;
      case kF: j.set(f.name, Round3(*(const float*)(base + f.offset))); break;
    }
  }
  Json eq = Json::Array();
  for (int i = 0; i < ms::kMaxEqBands; ++i) {
    Json b = Json::Object();
    b.set("on", p.eq[i].on != 0);
    b.set("type", (int)p.eq[i].type);
    b.set("freq", Round3(p.eq[i].freq));
    b.set("gain", Round3(p.eq[i].gainDb));
    b.set("q", Round3(p.eq[i].q));
    eq.push(b);
  }
  j.set("eq", eq);
  return j;
}

void ParamsFromJson(const Json& j, ms::Params* p) {
  if (!j.isObject()) return;
  char* base = (char*)p;
  for (const Field& f : kFields) {
    const Json& v = j[f.name];
    if (v.isNull()) continue;
    switch (f.kind) {
      case kB: *(int32_t*)(base + f.offset) = v.asBool() ? 1 : 0; break;
      case kI: *(int32_t*)(base + f.offset) = v.asInt(); break;
      case kF: *(float*)(base + f.offset) = (float)v.asNum(); break;
    }
  }
  const Json& eq = j["eq"];
  if (eq.isArray()) {
    for (size_t i = 0; i < eq.size() && i < (size_t)ms::kMaxEqBands; ++i) {
      const Json& b = eq.at(i);
      if (!b.isObject()) continue;
      ms::EqBand& d = p->eq[i];
      if (b.has("on")) d.on = b["on"].asBool() ? 1 : 0;
      if (b.has("type")) d.type = b["type"].asInt();
      if (b.has("freq")) d.freq = (float)b["freq"].asNum();
      if (b.has("gain")) d.gainDb = (float)b["gain"].asNum();
      if (b.has("q")) d.q = (float)b["q"].asNum();
    }
  }
  ms::SanitizeParams(p);
}

Json SettingsToJson(const AppSettings& s, bool includeParams) {
  Json j = Json::Object();
  j.set("language", s.language);
  j.set("theme", s.theme);
  j.set("micId", ToUtf8(s.micId));
  j.set("monitorOn", s.monitorOn);
  j.set("monitorDevice", ToUtf8(s.monitorDevice));
  j.set("monitorVolume", s.monitorVolume);
  j.set("monitorRaw", s.monitorRaw);
  j.set("routeOn", s.routeOn);
  j.set("routeDevice", ToUtf8(s.routeDevice));
  j.set("hotkeyOn", s.hotkeyOn);
  j.set("hotkeyMods", s.hotkeyMods);
  j.set("hotkeyVk", s.hotkeyVk);
  j.set("muteBeep", s.muteBeep);
  j.set("preset", s.preset);
  j.set("userPresets", s.userPresets);
  j.set("firstRun", s.firstRun);
  if (includeParams) j.set("params", ParamsToJson(s.params));
  return j;
}

void SettingsFromJson(const Json& j, AppSettings* s) {
  if (!j.isObject()) return;
  if (j["language"].isString()) s->language = j["language"].asStr();
  if (j["theme"].isString()) s->theme = j["theme"].asStr();
  if (j["micId"].isString()) s->micId = FromUtf8(j["micId"].asStr());
  if (!j["monitorOn"].isNull()) s->monitorOn = j["monitorOn"].asBool();
  if (j["monitorDevice"].isString()) s->monitorDevice = FromUtf8(j["monitorDevice"].asStr());
  if (j["monitorVolume"].isNumber()) s->monitorVolume = ms::ClampF((float)j["monitorVolume"].asNum(), 0.f, 1.5f);
  if (!j["monitorRaw"].isNull()) s->monitorRaw = j["monitorRaw"].asBool();
  if (!j["routeOn"].isNull()) s->routeOn = j["routeOn"].asBool();
  if (j["routeDevice"].isString()) s->routeDevice = FromUtf8(j["routeDevice"].asStr());
  if (!j["hotkeyOn"].isNull()) s->hotkeyOn = j["hotkeyOn"].asBool();
  if (j["hotkeyMods"].isNumber()) s->hotkeyMods = j["hotkeyMods"].asInt() & 0x000F;
  if (j["hotkeyVk"].isNumber()) s->hotkeyVk = j["hotkeyVk"].asInt() & 0xFF;
  if (!j["muteBeep"].isNull()) s->muteBeep = j["muteBeep"].asBool();
  if (j["preset"].isString()) s->preset = j["preset"].asStr();
  if (j["userPresets"].isArray()) s->userPresets = j["userPresets"];
  if (!j["firstRun"].isNull()) s->firstRun = j["firstRun"].asBool();
  if (j["params"].isObject()) ParamsFromJson(j["params"], &s->params);
}

std::wstring SettingsPath() { return ProgramDataDir() + L"\\settings.json"; }
std::wstring ParamsBinPath() { return ProgramDataDir() + L"\\params.bin"; }

bool LoadSettings(AppSettings* s) {
  std::string text;
  if (ReadFileBytes(SettingsPath(), &text)) {
    Json j;
    if (Json::Parse(text, &j)) {
      SettingsFromJson(j, s);
      return true;
    }
    Log("settings.json is damaged, using defaults");
  }
  // no settings yet: maybe the APO has parameters from an earlier install
  std::string bin;
  if (ReadFileBytes(ParamsBinPath(), &bin) && bin.size() == sizeof(ms::Params)) {
    ms::Params p;
    memcpy(&p, bin.data(), sizeof(p));
    if (p.magic == ms::kParamsMagic) {
      ms::SanitizeParams(&p);
      s->params = p;
    }
  }
  return false;
}

bool SaveSettings(const AppSettings& s) {
  EnsureDir(ProgramDataDir());
  std::string text = SettingsToJson(s, true).dump();
  return WriteFileAtomic(SettingsPath(), text.data(), text.size());
}

// ------------------------------------------------------------------ ParamsLink
ParamsLink::~ParamsLink() {
  flush();
  std::lock_guard<std::mutex> lock(mu_);
  ms::UnmapSharedFile(s_, file_, map_);
}

bool ParamsLink::openLocked() {
  if (s_) return true;
  lastOpenTry_ = GetTickCount64();
  EnsureDir(ProgramDataDir());
  DWORD err = 0;
  s_ = ms::MapSharedFile(&file_, &map_, &err);
  if (!s_) {
    Log("cannot open shared.bin (%lu)", err);
    return false;
  }
  if (s_->magic != ms::kSharedMagic || s_->version != ms::kSharedVersion) {
    // the APO has not run yet: lay out the block ourselves
    ZeroMemory((void*)&s_->tele, sizeof(ms::Telemetry));
    s_->version = ms::kSharedVersion;
    s_->size = sizeof(ms::SharedState);
    ms::Params p;
    if (haveLast_) p = last_; else ms::DefaultParams(&p);
    ms::WriteParamsSeq(s_, p);
    InterlockedExchange(&s_->magic, ms::kSharedMagic);
  }
  Log("shared.bin mapped");
  if (haveLast_) ms::WriteParamsSeq(s_, last_);
  return true;
}

void ParamsLink::write(const ms::Params& live, const ms::Params& persisted) {
  std::lock_guard<std::mutex> lock(mu_);
  last_ = live;
  haveLast_ = true;
  if (s_ || openLocked()) ms::WriteParamsSeq(s_, live);
  if (memcmp(&persisted, &persisted_, sizeof(ms::Params)) == 0 && !dirty_) return;
  persisted_ = persisted;
  if (!dirty_) dirtyAt_ = GetTickCount64();
  dirty_ = true;
}

void ParamsLink::flush() {
  ms::Params p;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!dirty_) return;
    dirty_ = false;
    p = persisted_;
  }
  EnsureDir(ProgramDataDir());
  if (!WriteFileAtomic(ParamsBinPath(), &p, sizeof(p))) Log("cannot write params.bin (%lu)", GetLastError());
}

void ParamsLink::tick() {
  bool doFlush = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const unsigned long long now = GetTickCount64();
    if (!s_ && now - lastOpenTry_ > 1000) openLocked();
    doFlush = dirty_ && now - dirtyAt_ > 150;
  }
  if (doFlush) flush();
}

bool ParamsLink::mapped() {
  std::lock_guard<std::mutex> lock(mu_);
  return s_ != nullptr;
}

bool ParamsLink::telemetry(ms::Telemetry* out, unsigned long long* ageMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!s_ || s_->magic != ms::kSharedMagic) return false;
  memcpy(out, (const void*)&s_->tele, sizeof(ms::Telemetry));
  const unsigned long long now = GetTickCount64();
  const unsigned long long last = (unsigned long long)out->lastTickMs;
  *ageMs = (last && now >= last) ? now - last : ~0ull;
  return true;
}

bool ParamsLink::apoAlive(unsigned long long maxAgeMs) {
  ms::Telemetry t;
  unsigned long long age = 0;
  return telemetry(&t, &age) && age <= maxAgeMs;
}

void ParamsLink::relearnNoise() {
  std::lock_guard<std::mutex> lock(mu_);
  if (s_) InterlockedIncrement(&s_->relearnSeq);
}

}  // namespace app
