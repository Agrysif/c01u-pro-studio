#pragma once
// App settings, parameter <-> JSON mapping and the link to the APO (shared memory + params.bin).
#include <mutex>
#include <string>

#include "../shared/params.h"
#include "../shared/shared_state.h"
#include "json.h"

namespace app {

Json ParamsToJson(const ms::Params& p);
// Applies the fields present in `j` on top of `p` (partial update), then sanitizes.
void ParamsFromJson(const Json& j, ms::Params* p);

struct AppSettings {
  std::string language;            // "" = follow Windows
  std::string theme = "auto";      // auto | light | dark
  std::wstring micId;              // "" = Samson if present, else the default microphone
  bool monitorOn = false;
  std::wstring monitorDevice;      // "" = default output
  float monitorVolume = 0.8f;
  bool monitorRaw = false;         // listen to the unprocessed signal
  bool routeOn = false;            // send processed voice to another output (virtual cable)
  std::wstring routeDevice;
  bool hotkeyOn = true;
  int hotkeyMods = 0x0003;         // MOD_ALT | MOD_CONTROL
  int hotkeyVk = 'M';
  bool muteBeep = true;
  std::string preset = "c01u";
  Json userPresets = Json::Array();
  bool firstRun = true;
  ms::Params params;

  AppSettings() { ms::DefaultParams(&params); }
};

Json SettingsToJson(const AppSettings& s, bool includeParams);
void SettingsFromJson(const Json& j, AppSettings* s);
bool LoadSettings(AppSettings* s);
bool SaveSettings(const AppSettings& s);
std::wstring SettingsPath();
std::wstring ParamsBinPath();

// App side of the APO link. Thread safe.
class ParamsLink {
 public:
  ~ParamsLink();
  // `live` goes to the APO right away, `persisted` to params.bin (debounced) for the
  // next audio session; they differ in temporary flags (mute, A/B bypass).
  void write(const ms::Params& live, const ms::Params& persisted);
  void tick();                          // ~4 Hz: reopen the section, flush the file
  void flush();
  bool mapped();
  // Snapshot of the APO telemetry; ageMs = time since the APO last processed audio.
  bool telemetry(ms::Telemetry* out, unsigned long long* ageMs);
  bool apoAlive(unsigned long long maxAgeMs = 1500);
  void relearnNoise();

 private:
  bool openLocked();
  std::mutex mu_;
  HANDLE file_ = nullptr;
  HANDLE map_ = nullptr;
  ms::SharedState* s_ = nullptr;
  ms::Params last_ = {}, persisted_ = {};
  bool haveLast_ = false;
  bool dirty_ = false;
  unsigned long long dirtyAt_ = 0;
  unsigned long long lastOpenTry_ = 0;
};

}  // namespace app
