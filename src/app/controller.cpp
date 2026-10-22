#include "controller.h"

#include <mmsystem.h>
#include <shellapi.h>

#include <math.h>
#include <algorithm>

#include "common.h"
#include "install.h"
#include "update.h"
#include "update_core.h"

namespace app {

namespace {

Json DeviceJson(const DeviceInfo& d) {
  Json j = Json::Object();
  j.set("id", ToUtf8(d.id));
  j.set("name", ToUtf8(d.name));
  j.set("default", d.isDefault);
  j.set("defaultComm", d.isDefaultComm);
  j.set("samson", d.samson);
  j.set("c01u", d.c01uPro);
  j.set("active", d.state == DEVICE_STATE_ACTIVE);
  return j;
}

float R1(float v) { return (float)(lround(v * 10.0) / 10.0); }

BOOL CALLBACK FindUiWindow(HWND h, LPARAM out) {
  wchar_t cls[64], title[64];
  if (!IsWindowVisible(h) || !GetClassNameW(h, cls, 64) || wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;
  if (!GetWindowTextW(h, title, 64) || wcscmp(title, MS_PRODUCT_NAME) != 0) return TRUE;
  *(HWND*)out = h;
  return FALSE;
}

// Edge and Chrome ignore --window-size when the browser already runs in the background
// (startup boost) and reuse a saved size, often the full screen height. Once the panel window
// shows up, an oversized one gets a compact size in the middle of its monitor; a size the
// user picked is left alone.
void FitUiWindow() {
  HWND h = nullptr;
  for (int i = 0; i < 80 && !h; ++i) {
    EnumWindows(FindUiWindow, (LPARAM)&h);
    if (!h) Sleep(100);
  }
  if (!h) return;
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
  const RECT& wa = mi.rcWork;
  const int areaW = wa.right - wa.left, areaH = wa.bottom - wa.top;
  RECT r;
  GetWindowRect(h, &r);
  if (!IsZoomed(h) && r.right - r.left <= areaW * 9 / 10 && r.bottom - r.top <= areaH * 9 / 10) return;
  typedef UINT(WINAPI * DpiForWindow)(HWND);  // Windows 10+
  DpiForWindow dpiFor = (DpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  const int dpi = dpiFor ? (int)dpiFor(h) : 96;
  const int w = std::min(MulDiv(1240, dpi, 96), areaW * 9 / 10);
  const int ht = std::min(MulDiv(860, dpi, 96), areaH * 9 / 10);
  if (IsZoomed(h)) ShowWindow(h, SW_RESTORE);
  SetWindowPos(h, nullptr, wa.left + (areaW - w) / 2, wa.top + (areaH - ht) / 2, w, ht, SWP_NOZORDER);
}

std::string MakeToneWav(bool up) {
  const int rate = 44100;
  const int n = rate / 8;
  std::string w(44 + n * 2, '\0');
  char* p = &w[0];
  auto u32 = [&](size_t off, uint32_t v) { memcpy(p + off, &v, 4); };
  auto u16 = [&](size_t off, uint16_t v) { memcpy(p + off, &v, 2); };
  memcpy(p, "RIFF", 4);
  u32(4, 36 + n * 2);
  memcpy(p + 8, "WAVEfmt ", 8);
  u32(16, 16);
  u16(20, 1);
  u16(22, 1);
  u32(24, rate);
  u32(28, rate * 2);
  u16(32, 2);
  u16(34, 16);
  memcpy(p + 36, "data", 4);
  u32(40, n * 2);
  int16_t* s = (int16_t*)(p + 44);
  double ph = 0;
  for (int i = 0; i < n; ++i) {
    const double t = (double)i / n;
    const double f = up ? 660 + 330 * t : 990 - 330 * t;
    ph += 2 * 3.14159265358979 * f / rate;
    const double env = sin(3.14159265358979 * t);
    s[i] = (int16_t)(sin(ph) * env * 0.25 * 32767);
  }
  return w;
}

std::string SystemLanguage() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return "en";
  std::string s = ToUtf8(name);
  size_t dash = s.find('-');
  return Lower(dash == std::string::npos ? s : s.substr(0, dash));
}

const char* ContentType(const std::string& path) {
  auto ends = [&](const char* ext) {
    size_t n = strlen(ext);
    return path.size() >= n && Lower(path.substr(path.size() - n)) == ext;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".css")) return "text/css; charset=utf-8";
  if (ends(".js")) return "application/javascript; charset=utf-8";
  if (ends(".json")) return "application/json; charset=utf-8";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".ico")) return "image/x-icon";
  if (ends(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

}  // namespace

// ------------------------------------------------------------------ lifecycle
bool Controller::init(HWND hwnd) {
  hwnd_ = hwnd;
  LoadSettings(&settings_);
  settings_.params.mute = 0;
  sysLang_ = SystemLanguage();
  wavUp_ = MakeToneWav(true);
  wavDown_ = MakeToneWav(false);
  pushParams();
  engine_.setDevice(settings_.micId);
  engine_.setMonitor(settings_.monitorOn, settings_.monitorDevice, settings_.monitorVolume, settings_.monitorRaw);
  engine_.setRoute(settings_.routeOn, settings_.routeDevice);
  if (!http_.start(47811, 47830, [this](const HttpRequest& q, HttpResponse& r) { handle(q, r); })) return false;
  onTimer();
  return true;
}

void Controller::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    settings_.params.mute = 0;
  }
  tempBypass_ = false;
  pushParams();
  link_.flush();
  AppSettings copy;
  bool save;
  {
    std::lock_guard<std::mutex> lock(mu_);
    save = dirty_;
    copy = settings_;
  }
  if (save) SaveSettings(copy);
  http_.stop();
  engine_.setDemand(false);
}

void Controller::markDirty() {
  if (!dirty_) dirtyAt_ = GetTickCount64();
  dirty_ = true;
}

void Controller::pushParams() {
  ms::Params p;
  {
    std::lock_guard<std::mutex> lock(mu_);
    p = settings_.params;
  }
  ms::Params live = p;
  live.bypass = (p.bypass || tempBypass_) ? 1 : 0;
  ms::Params persisted = p;
  persisted.mute = 0;
  link_.write(live, persisted);
  engine_.setParams(p);
}

void Controller::setTemporaryBypass(bool on) {
  if (tempBypass_ == on) return;
  tempBypass_ = on;
  pushParams();
  engine_.setAppProcessing(!apoActive_ || on);
}

std::wstring Controller::micGuid() {
  std::lock_guard<std::mutex> lock(mu_);
  return micId_.empty() ? std::wstring() : EndpointGuid(micId_);
}

bool Controller::apoActiveForMic() { return apoActive_.load(); }

void Controller::onDevicesChanged() { lastApoCheck_ = 0; }

void Controller::onTimer() {
  link_.tick();
  const unsigned long long now = GetTickCount64();
  if (now - lastApoCheck_ > 2000) {
    lastApoCheck_ = now;
    std::wstring id = engine_.deviceId();
    std::wstring configured;
    {
      std::lock_guard<std::mutex> lock(mu_);
      configured = settings_.micId;
    }
    if (id.empty() || (!configured.empty() && configured != id)) {
      DeviceInfo d;
      if (ResolveMic(configured, &d)) id = d.id;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      micId_ = id;
    }
    micHasApo_ = !id.empty() && EndpointHasApo(EndpointGuid(id));
  }
  if (now < enhanceUntil_ && now - lastEnhanceCheck_ > 1000) {
    lastEnhanceCheck_ = now;
    std::wstring id;
    {
      std::lock_guard<std::mutex> lock(mu_);
      id = micId_;
    }
    bool off = false;
    if (!id.empty() && GetEnhancementsDisabled(id, &off) && off) EnableEnhancements();
  }
  // the update task installed a newer version: continue in it
  if (!handingOff_ && now - lastVersionCheck_ > 5000) {
    lastVersionCheck_ = now;
    const std::string installed = ToUtf8(InstalledVersion());
    if (!installed.empty() && upd::CompareVersions(installed, MS_VERSION_STR) > 0 &&
        GetFileAttributesW(InstalledExePath().c_str()) != INVALID_FILE_ATTRIBUTES) {
      Log("version %s is installed, restarting into it", installed.c_str());
      handingOff_ = true;
      ShellExecuteW(nullptr, L"open", InstalledExePath().c_str(), L"--handoff", nullptr, SW_SHOWNORMAL);
      PostMessageW(hwnd_, WM_APP_QUIT, 1, 0);
    }
  }
  ms::Telemetry t;
  unsigned long long age = 0;
  const bool alive = link_.telemetry(&t, &age) && age < 1500;
  apoActive_ = micHasApo_ && alive;
  engine_.setAppProcessing(!apoActive_ || tempBypass_);

  bool monitorOn, routeOn;
  {
    std::lock_guard<std::mutex> lock(mu_);
    monitorOn = settings_.monitorOn;
    routeOn = settings_.routeOn;
  }
  engine_.setDemand(http_.sseClients() > 0 || monitorOn || routeOn || busy_ > 0);

  // end of an A/B recording: give the processing back to the APO
  double pos = 0, len = 0;
  const int rs = engine_.recordState(&pos, &len);
  if ((lastRecState_ == 1 || lastRecState_ == 2) && rs != 1 && rs != 2) {
    setTemporaryBypass(false);
    if (busy_ > 0) --busy_;
  }
  lastRecState_ = rs;

  if (dirty_ && now - dirtyAt_ > 1000) {
    AppSettings copy;
    {
      std::lock_guard<std::mutex> lock(mu_);
      copy = settings_;
      dirty_ = false;
    }
    copy.params.mute = 0;
    SaveSettings(copy);
  }
}

// ------------------------------------------------------------------ tray
void Controller::openUi() {
  // the panel window is open already: bring it to the front instead of opening another one
  HWND open = nullptr;
  EnumWindows(FindUiWindow, (LPARAM)&open);
  if (open) {
    if (IsIconic(open)) ShowWindow(open, SW_RESTORE);
    SetForegroundWindow(open);
    return;
  }
  wchar_t url[64];
  swprintf_s(url, L"http://127.0.0.1:%d/", http_.port());
  // a chromeless app window if Edge or Chrome is around, otherwise the default browser
  const wchar_t* candidates[] = {L"msedge.exe", L"chrome.exe"};
  for (const wchar_t* exe : candidates) {
    const std::wstring key = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + exe;
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
      wchar_t path[MAX_PATH] = {};
      DWORD size = sizeof(path);
      if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS) continue;
      const std::wstring args = std::wstring(L"--app=") + url + L" --window-size=1240,860";
      if ((INT_PTR)ShellExecuteW(nullptr, L"open", path, args.c_str(), nullptr, SW_SHOWNORMAL) > 32) {
        std::thread(FitUiWindow).detach();
        return;
      }
    }
  }
  ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void Controller::beep(bool up) {
  bool on;
  {
    std::lock_guard<std::mutex> lock(mu_);
    on = settings_.muteBeep;
  }
  if (on) PlaySoundA((up ? wavUp_ : wavDown_).data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void Controller::toggleMute() {
  bool nowMuted;
  {
    std::lock_guard<std::mutex> lock(mu_);
    settings_.params.mute = settings_.params.mute ? 0 : 1;
    nowMuted = settings_.params.mute != 0;
  }
  pushParams();
  beep(!nowMuted);
  PostMessageW(hwnd_, WM_APP_STATE, 0, 0);
}

void Controller::toggleBypass() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    settings_.params.bypass = settings_.params.bypass ? 0 : 1;
    markDirty();
  }
  pushParams();
  PostMessageW(hwnd_, WM_APP_STATE, 0, 0);
}

void Controller::toggleMonitor() {
  std::lock_guard<std::mutex> lock(mu_);
  settings_.monitorOn = !settings_.monitorOn;
  engine_.setMonitor(settings_.monitorOn, settings_.monitorDevice, settings_.monitorVolume, settings_.monitorRaw);
  markDirty();
}

bool Controller::muted() {
  std::lock_guard<std::mutex> lock(mu_);
  return settings_.params.mute != 0;
}

bool Controller::bypassed() {
  std::lock_guard<std::mutex> lock(mu_);
  return settings_.params.bypass != 0;
}

bool Controller::monitoring() {
  std::lock_guard<std::mutex> lock(mu_);
  return settings_.monitorOn;
}

std::string Controller::language() {
  std::lock_guard<std::mutex> lock(mu_);
  return settings_.language.empty() ? sysLang_ : settings_.language;
}

bool Controller::hotkey(int* mods, int* vk) {
  std::lock_guard<std::mutex> lock(mu_);
  *mods = settings_.hotkeyMods;
  *vk = settings_.hotkeyVk;
  return settings_.hotkeyOn;
}

std::string Controller::trayTooltip() {
  std::string s = MS_PRODUCT_NAME_A;
  s += apoActive_ ? " - driver active" : "";
  return s;
}

// ------------------------------------------------------------------ JSON views
Json Controller::devicesJson() {
  Json j = Json::Object();
  Json in = Json::Array(), out = Json::Array();
  for (const DeviceInfo& d : ListDevices(eCapture, false)) in.push(DeviceJson(d));
  for (const DeviceInfo& d : ListDevices(eRender, false)) out.push(DeviceJson(d));
  j.set("inputs", in);
  j.set("outputs", out);
  return j;
}

Json Controller::statusJson() {
  Json s = Json::Object();
  DeviceInfo mic;
  std::wstring configured;
  {
    std::lock_guard<std::mutex> lock(mu_);
    configured = settings_.micId;
  }
  const bool haveMic = ResolveMic(configured, &mic);
  Json dev = Json::Object();
  dev.set("found", haveMic);
  if (haveMic) {
    dev.set("id", ToUtf8(mic.id));
    dev.set("name", ToUtf8(mic.name));
    dev.set("samson", mic.samson);
    dev.set("c01u", mic.c01uPro);
    dev.set("default", mic.isDefault);
    dev.set("defaultComm", mic.isDefaultComm);
    EndpointDetails det;
    if (GetEndpointDetails(mic.id, &det)) {
      dev.set("volume", R1(det.volume * 100.f));
      dev.set("mute", det.mute);
      dev.set("rate", det.rate);
      dev.set("bits", det.bits);
      dev.set("channels", det.channels);
    }
  }
  dev.set("capturing", engine_.running() && engine_.deviceOk());
  s.set("device", dev);

  DriverStatus ds = QueryDriverStatus();
  Json drv = Json::Object();
  drv.set("installed", ds.filesInstalled && ds.registered);
  drv.set("version", ToUtf8(ds.installedVersion));
  drv.set("protectedOff", ds.protectedAudioOff);
  const std::wstring guid = haveMic ? EndpointGuid(mic.id) : std::wstring();
  drv.set("attached", haveMic && EndpointHasApo(guid));
  bool sysfxOff = false;
  if (haveMic && !GetEnhancementsDisabled(mic.id, &sysfxOff)) sysfxOff = EndpointSysFxDisabled(guid);
  drv.set("sysfxDisabled", haveMic && sysfxOff);
  drv.set("active", apoActive_.load());
  // Samson endpoints (e.g. another USB port) that do not carry the APO yet
  Json missing = Json::Array();
  if (ds.registered)
    for (const std::wstring& g : SamsonCaptureEndpointGuids())
      if (!EndpointHasApo(g)) missing.push(ToUtf8(g));
  drv.set("unattached", missing);
  ms::Telemetry t;
  unsigned long long age = 0;
  if (link_.telemetry(&t, &age)) {
    drv.set("streams", (int)t.activeStreams);
    drv.set("load", R1(t.dspLoad * 1000.f) / 10.f);
    drv.set("latencyMs", t.sampleRate > 0 ? R1(t.latencySamples * 1000.f / t.sampleRate) : 0.f);
    drv.set("lastSeenMs", age == ~0ull ? -1.0 : (double)age);
  }
  s.set("driver", drv);

  s.set("mode", !engine_.running() ? "idle" : (apoActive_ && !tempBypass_) ? "apo" : "app");
  s.set("muted", muted());
  s.set("bypass", bypassed());
  Json mon = Json::Object();
  {
    std::lock_guard<std::mutex> lock(mu_);
    mon.set("on", settings_.monitorOn);
    mon.set("route", settings_.routeOn);
  }
  mon.set("ok", engine_.monitorOk());
  mon.set("routeOk", engine_.routeOk());
  s.set("monitor", mon);
  s.set("autostart", GetAutostart());
  s.set("installedExe", GetFileAttributesW(InstalledExePath().c_str()) != INVALID_FILE_ATTRIBUTES);
  s.set("runningInstalled", LowerW(ExePath()) == LowerW(InstalledExePath()));
  s.set("glitches", engine_.glitches());
  return s;
}

Json Controller::stateJson() {
  Json j = Json::Object();
  j.set("version", MS_VERSION_STR);
  j.set("os", WindowsVersionString());
  j.set("sysLang", sysLang_);
  {
    std::lock_guard<std::mutex> lock(mu_);
    j.set("settings", SettingsToJson(settings_, false));
    j.set("params", ParamsToJson(settings_.params));
  }
  j.set("status", statusJson());
  j.set("devices", devicesJson());
  j.set("update", UpdateStateJson());
  return j;
}

Json Controller::metersJson() {
  Meters m;
  engine_.snapshot(&m);
  Json j = Json::Object();
  j.set("run", m.running);
  const bool apo = apoActive_ && !tempBypass_;
  ms::Telemetry t;
  unsigned long long age = 0;
  const bool tele = apo && link_.telemetry(&t, &age);
  Json in = Json::Array(), out = Json::Array();
  in.push(R1(tele ? t.inPeakDb : m.inPeakDb));
  in.push(R1(tele ? t.inRmsDb : m.inRmsDb));
  out.push(R1(m.outPeakDb));
  out.push(R1(m.outRmsDb));
  j.set("in", in);
  j.set("out", out);
  j.set("clip", m.clip);
  Json gr = Json::Object();
  if (tele) {
    gr.set("gate", R1(t.gateGainDb));
    gr.set("ns", R1(t.nsReductionDb));
    gr.set("comp", R1(t.compGainDb));
    gr.set("lim", R1(t.limGainDb));
    gr.set("agc", R1(t.agcGainDb));
    gr.set("deess", R1(t.deessGainDb));
    gr.set("pop", R1(t.popGainDb));
    j.set("floor", R1(t.noiseFloorDb));
    j.set("voice", t.voice != 0);
  } else if (m.chainValid) {
    gr.set("gate", R1(20.f * log10f(std::max(1e-6f, m.chain.gateGain))));
    gr.set("ns", R1(m.chain.nsReductionDb));
    gr.set("comp", R1(m.chain.compGainDb));
    gr.set("lim", R1(20.f * log10f(std::max(1e-6f, m.chain.limGain))));
    gr.set("agc", R1(m.chain.agcGainDb));
    gr.set("deess", R1(20.f * log10f(std::max(1e-6f, m.chain.deessGain))));
    gr.set("pop", R1(20.f * log10f(std::max(1e-6f, m.chain.popGain))));
    j.set("floor", R1(m.chain.inputFloorDb));
    j.set("voice", m.chain.voice);
  }
  j.set("gr", gr);
  j.set("lufsM", R1(m.lufsM));
  j.set("lufsS", R1(m.lufsS));
  j.set("pitch", R1(m.pitchHz));
  Json spec = Json::Array();
  for (int i = 0; i < kSpectrumBands; ++i) spec.push((int)lround(m.spectrum[i]));
  j.set("spec", spec);
  j.set("mode", !m.running ? "idle" : apo ? "apo" : "app");
  double pos = 0, len = 0;
  const int rs = engine_.recordState(&pos, &len);
  Json rec = Json::Object();
  rec.set("state", rs);
  rec.set("pos", R1((float)pos));
  rec.set("len", R1((float)len));
  j.set("rec", rec);
  return j;
}

// ------------------------------------------------------------------ HTTP routing
void Controller::handle(const HttpRequest& req, HttpResponse& res) {
  const std::string& p = req.path;
  if (p.compare(0, 5, "/api/") != 0) {
    if (req.method != "GET" && req.method != "HEAD") return res.error(405, "method");
    return serveStatic(req, res);
  }
  if (req.method == "GET") {
    if (p == "/api/state") return res.json(stateJson());
    if (p == "/api/status") return res.json(statusJson());
    if (p == "/api/devices") return res.json(devicesJson());
    if (p == "/api/update") return res.json(UpdateStateJson());
    if (p == "/api/events") {
      res.sse = [this](SseStream& s) { events(s); };
      return;
    }
    if (p == "/api/record/raw.wav" || p == "/api/record/processed.wav") {
      std::string wav;
      if (!engine_.recordingWav(p == "/api/record/processed.wav", &wav)) return res.error(404, "no recording");
      res.contentType = "audio/wav";
      res.body.swap(wav);
      return;
    }
    if (p == "/api/export") {
      std::lock_guard<std::mutex> lock(mu_);
      Json j = SettingsToJson(settings_, true);
      j.set("format", "c01u-pro-studio");
      j.set("version", MS_VERSION_STR);
      res.json(j);
      res.headers.emplace_back("Content-Disposition", "attachment; filename=\"c01u-pro-studio-settings.json\"");
      return;
    }
    if (p == "/api/log") {
      std::string text;
      ReadFileBytes(InstallLogPath(), &text);
      std::string app;
      ReadFileBytes(ProgramDataDir() + L"\\app.log", &app);
      res.contentType = "text/plain; charset=utf-8";
      res.body = "== install.log\n" + text + "\n== app.log\n" + app;
      return;
    }
    return res.error(404, "not found");
  }
  if (req.method != "POST") return res.error(405, "method");
  Json body;
  if (!req.jsonBody(&body)) return res.error(400, "bad json");
  if (p == "/api/params") return apiParams(body, res);
  if (p == "/api/settings") return apiSettings(body, res);
  if (p == "/api/record") return apiRecord(body, res);
  if (p == "/api/measure") return apiMeasure(body, res);
  if (p == "/api/diagnostics") return res.json(diagnostics());
  if (p == "/api/fix") return apiFix(body, res);
  if (p == "/api/driver") return apiDriver(body, res);
  if (p == "/api/update") return apiUpdate(body, res);
  if (p == "/api/relearn") {
    link_.relearnNoise();
    engine_.relearnNoise();
    Json ok = Json::Object();
    ok.set("ok", true);
    return res.json(ok);
  }
  if (p == "/api/import") {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (body["format"].asStr() != "c01u-pro-studio") return res.error(400, "not a settings file");
      SettingsFromJson(body, &settings_);
      settings_.params.mute = 0;
      markDirty();
    }
    pushParams();
    return res.json(stateJson());
  }
  if (p == "/api/quit") {
    PostMessageW(hwnd_, WM_APP_QUIT, 0, 0);
    Json ok = Json::Object();
    ok.set("ok", true);
    return res.json(ok);
  }
  res.error(404, "not found");
}

void Controller::serveStatic(const HttpRequest& req, HttpResponse& res) {
  std::string path = req.path;
  if (path == "/" || path.empty()) path = "/index.html";
  if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return res.error(400, "bad path");
  std::string rel = path.substr(1);
  res.contentType = ContentType(rel);
  res.headers.emplace_back("Cache-Control", "no-cache");
  if (rel == "index.html") {
    res.headers.emplace_back("Content-Security-Policy",
                             "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                             "media-src 'self' blob:; connect-src 'self'; script-src 'self'");
  }
  // development: serve straight from a folder
  wchar_t dev[MAX_PATH];
  if (GetEnvironmentVariableW(L"C01U_WEB_DIR", dev, MAX_PATH)) {
    std::wstring file = std::wstring(dev) + L"\\" + FromUtf8(rel);
    for (auto& c : file)
      if (c == L'/') c = L'\\';
    if (ReadFileBytes(file, &res.body)) return;
    return res.error(404, "not found");
  }
  // same naming as build.ps1: web/i18n/ru.json -> WEB_I18N_RU_JSON
  std::wstring name = L"WEB_" + FromUtf8(rel);
  CharUpperBuffW(&name[0], (DWORD)name.size());
  for (auto& c : name)
    if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) c = L'_';
  name[3] = L'_';
  const void* data = nullptr;
  DWORD size = 0;
  if (!LoadResourceData(name.c_str(), &data, &size)) return res.error(404, "not found");
  res.body.assign((const char*)data, size);
}

void Controller::events(SseStream& s) {
  int n = 0;
  while (s.alive()) {
    if (n % 25 == 0 && !s.send("s", statusJson().dump())) break;
    if (!s.send("m", metersJson().dump())) break;
    ++n;
    Sleep(40);
  }
}

void Controller::apiParams(const Json& body, HttpResponse& res) {
  Json out = Json::Object();
  {
    std::lock_guard<std::mutex> lock(mu_);
    const Json& src = body["params"].isObject() ? body["params"] : body;
    const int32_t mute = settings_.params.mute;
    ParamsFromJson(src, &settings_.params);
    if (!src.has("mute")) settings_.params.mute = mute;
    if (body["preset"].isString()) settings_.preset = body["preset"].asStr();
    markDirty();
    out.set("ok", true);
    out.set("params", ParamsToJson(settings_.params));
    out.set("preset", settings_.preset);
  }
  pushParams();
  PostMessageW(hwnd_, WM_APP_STATE, 0, 0);
  res.json(out);
}

void Controller::apiSettings(const Json& b, HttpResponse& res) {
  bool micChanged = false, monitorChanged = false, routeChanged = false, hotkeyChanged = false;
  std::wstring micId;
  {
    std::lock_guard<std::mutex> lock(mu_);
    AppSettings& s = settings_;
    if (b["language"].isString()) s.language = b["language"].asStr();
    if (b["theme"].isString()) s.theme = b["theme"].asStr();
    if (b["micId"].isString()) {
      std::wstring id = FromUtf8(b["micId"].asStr());
      micChanged = id != s.micId;
      s.micId = id;
    }
    if (!b["monitorOn"].isNull()) {
      s.monitorOn = b["monitorOn"].asBool();
      monitorChanged = true;
    }
    if (b["monitorDevice"].isString()) {
      s.monitorDevice = FromUtf8(b["monitorDevice"].asStr());
      monitorChanged = true;
    }
    if (b["monitorVolume"].isNumber()) {
      s.monitorVolume = ms::ClampF((float)b["monitorVolume"].asNum(), 0.f, 1.5f);
      monitorChanged = true;
    }
    if (!b["monitorRaw"].isNull()) {
      s.monitorRaw = b["monitorRaw"].asBool();
      monitorChanged = true;
    }
    if (!b["routeOn"].isNull()) {
      s.routeOn = b["routeOn"].asBool();
      routeChanged = true;
    }
    if (b["routeDevice"].isString()) {
      s.routeDevice = FromUtf8(b["routeDevice"].asStr());
      routeChanged = true;
    }
    if (!b["hotkeyOn"].isNull()) {
      s.hotkeyOn = b["hotkeyOn"].asBool();
      hotkeyChanged = true;
    }
    if (b["hotkeyMods"].isNumber()) {
      s.hotkeyMods = b["hotkeyMods"].asInt() & 0xF;
      hotkeyChanged = true;
    }
    if (b["hotkeyVk"].isNumber()) {
      s.hotkeyVk = b["hotkeyVk"].asInt() & 0xFF;
      hotkeyChanged = true;
    }
    if (!b["muteBeep"].isNull()) s.muteBeep = b["muteBeep"].asBool();
    if (b["userPresets"].isArray()) s.userPresets = b["userPresets"];
    if (!b["firstRun"].isNull()) s.firstRun = b["firstRun"].asBool();
    if (monitorChanged) engine_.setMonitor(s.monitorOn, s.monitorDevice, s.monitorVolume, s.monitorRaw);
    if (routeChanged) engine_.setRoute(s.routeOn, s.routeDevice);
    micId = s.micId;
    markDirty();
  }
  if (micChanged) {
    engine_.setDevice(micId);
    lastApoCheck_ = 0;
  }
  if (hotkeyChanged) PostMessageW(hwnd_, WM_APP_HOTKEY_CHANGED, 0, 0);
  if (!b["autostart"].isNull()) SetAutostart(b["autostart"].asBool());
  if (b["micVolume"].isNumber() || !b["micMute"].isNull()) {
    DeviceInfo d;
    if (ResolveMic(micId, &d)) {
      if (b["micVolume"].isNumber()) SetEndpointVolume(d.id, (float)b["micVolume"].asNum() / 100.f);
      if (!b["micMute"].isNull()) SetEndpointMute(d.id, b["micMute"].asBool());
    }
  }
  PostMessageW(hwnd_, WM_APP_STATE, 0, 0);
  res.json(stateJson());
}

void Controller::apiRecord(const Json& body, HttpResponse& res) {
  const std::string action = body["action"].asStr("start");
  Json out = Json::Object();
  if (action == "stop") {
    engine_.stopRecording();
    out.set("ok", true);
    return res.json(out);
  }
  double pos = 0, len = 0;
  const int rs = engine_.recordState(&pos, &len);
  if (rs == 1 || rs == 2) return res.error(409, "busy");
  ++busy_;
  engine_.setDemand(true);
  for (int i = 0; i < 30 && !(engine_.running() && engine_.deviceOk()); ++i) Sleep(100);
  if (!engine_.running()) {
    --busy_;
    return res.error(503, "microphone not available");
  }
  setTemporaryBypass(true);  // raw signal from the APO, processed copy made here
  const double seconds = std::max(2.0, std::min(30.0, body["seconds"].asNum(10)));
  if (!engine_.startRecording(seconds, 0.8)) {
    setTemporaryBypass(false);
    --busy_;
    return res.error(409, "busy");
  }
  lastRecState_ = 1;
  out.set("ok", true);
  out.set("seconds", seconds);
  res.json(out);
}

void Controller::apiMeasure(const Json& body, HttpResponse& res) {
  const double seconds = std::max(1.0, std::min(15.0, body["seconds"].asNum(4)));
  ++busy_;
  const bool needBypass = apoActive_;
  if (needBypass) {
    setTemporaryBypass(true);
    Sleep(250);
  }
  Measurement m;
  const bool ok = engine_.measure(seconds, &m);
  if (needBypass) setTemporaryBypass(false);
  --busy_;
  if (body["relearn"].asBool()) {
    link_.relearnNoise();
    engine_.relearnNoise();
  }
  if (!ok) return res.error(503, "microphone not available");
  Json j = Json::Object();
  j.set("ok", true);
  j.set("seconds", m.seconds);
  j.set("rate", m.rate);
  j.set("silent", m.silent);
  j.set("peak", R1(m.peakDb));
  j.set("rms", R1(m.rmsDb));
  j.set("dc", m.dcOffset);
  j.set("clipped", m.clipped);
  j.set("glitches", m.glitches);
  j.set("floor", R1(m.noiseFloorDb));
  j.set("speech", R1(m.speechDb));
  j.set("active", R1(m.activeRatio * 100.f));
  j.set("loudness", R1(m.loudnessDb));
  Json bands = Json::Array();
  for (float b : m.bandDb) bands.push(R1(b));
  j.set("bands", bands);
  j.set("hum50", R1(m.hum50Db));
  j.set("hum60", R1(m.hum60Db));
  j.set("sibilance", R1(m.sibilanceDb));
  j.set("low", R1(m.lowDb));
  j.set("pitch", R1(m.pitchHz));
  res.json(j);
}

int Controller::runElevated(const std::wstring& args) {
  if (elevating_.exchange(true)) return -3;
  const int code = LaunchElevated(args, 180000);
  elevating_ = false;
  lastApoCheck_ = 0;
  Log("elevated '%ls' -> %d", args.c_str(), code);
  return code;
}

void Controller::apiDriver(const Json& body, HttpResponse& res) {
  const std::string action = body["action"].asStr();
  std::wstring guid = micGuid();
  if (body["endpoint"].isString()) guid = FromUtf8(body["endpoint"].asStr());
  std::wstring args;
  if (action == "install") {
    args = L"--install";
    if (!guid.empty()) args += L" --endpoints " + guid;
  } else if (action == "uninstall") {
    args = L"--uninstall";
  } else if (action == "attach") {
    std::wstring list = guid;
    if (body["all"].asBool())
      for (const std::wstring& g : SamsonCaptureEndpointGuids()) list += L";" + g;
    args = L"--attach " + list;
  } else if (action == "detach") {
    args = L"--detach " + guid;
  } else if (action == "restartAudio") {
    args = L"--restart-audio";
  } else if (action == "enableSysfx") {
    // goes through the audio service like the Sound control panel: no elevation needed
    Json out = Json::Object();
    out.set("ok", EnableEnhancements());
    out.set("code", 0);
    return res.json(out);
  } else {
    return res.error(400, "unknown action");
  }
  const int code = runElevated(args);
  Json out = Json::Object();
  out.set("code", code);
  out.set("ok", code == 0 || (action == "install" && code == 13));
  if (code == -2) out.set("error", "declined");
  // Windows switches enhancements off by itself whenever an effect failed to load (for
  // example before the driver was installed or after a driver update); switch them back on.
  if ((action == "install" || action == "attach" || action == "restartAudio") && code >= 0) ensureEnhancements();
  if (action == "install" && (code == 0 || code == 13 || code == 14 || code == 15)) {
    SetAutostart(true);
    if (LowerW(ExePath()) != LowerW(InstalledExePath())) {
      // continue from the installed copy
      ShellExecuteW(nullptr, L"open", InstalledExePath().c_str(), L"--handoff", nullptr, SW_SHOWNORMAL);
      out.set("restart", true);
      PostMessageW(hwnd_, WM_APP_QUIT, 1, 0);
    }
  }
  if (action == "uninstall" && code == 0) {
    out.set("restart", false);
    out.set("removed", true);
    PostMessageW(hwnd_, WM_APP_QUIT, 1, 0);
  }
  res.json(out);
}

void Controller::apiUpdate(const Json& body, HttpResponse& res) {
  if (body["auto"].isBool()) SetAutoUpdate(body["auto"].asBool());
  Json out = UpdateStateJson();
  if (body["check"].asBool()) {
    bool started = StartUpdateTask();
    if (!started && !elevating_.exchange(true)) {
      // no task yet (driver installed by an older version): once through the UAC prompt
      std::thread([this] {
        LaunchElevated(L"--update --force", 15 * 60 * 1000);
        elevating_ = false;
      }).detach();
      started = true;
      out.set("elevated", true);
    }
    out.set("started", started);
  }
  res.json(out);
}

bool Controller::EnableEnhancements() {
  std::wstring configured;
  {
    std::lock_guard<std::mutex> lock(mu_);
    configured = settings_.micId;
  }
  DeviceInfo mic;
  const bool haveMic = ResolveMic(configured, &mic);
  bool ok = true;
  for (const DeviceInfo& d : ListDevices(eCapture, false))
    if (d.samson || (haveMic && d.id == mic.id)) ok = SetEnhancementsEnabled(d.id) && ok;
  lastApoCheck_ = 0;
  return ok;
}

void Controller::ensureEnhancements() {
  EnableEnhancements();
  enhanceUntil_ = GetTickCount64() + 15000;
}

void Controller::apiFix(const Json& body, HttpResponse& res) {
  const std::string id = body["id"].asStr();
  DeviceInfo mic;
  std::wstring configured;
  {
    std::lock_guard<std::mutex> lock(mu_);
    configured = settings_.micId;
  }
  const bool haveMic = ResolveMic(configured, &mic);
  bool ok = false;
  if (id == "setDefault" && haveMic) ok = SetDefaultDevice(mic.id);
  else if (id == "unmute" && haveMic) ok = SetEndpointMute(mic.id, false);
  else if (id == "volume" && haveMic) ok = SetEndpointVolume(mic.id, (float)body["arg"].asNum(100) / 100.f);
  else if (id == "openPrivacy")
    ok = (INT_PTR)ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone", nullptr, nullptr, SW_SHOWNORMAL) > 32;
  else if (id == "openSound")
    ok = (INT_PTR)ShellExecuteW(nullptr, L"open", L"control.exe", L"mmsys.cpl,,1", nullptr, SW_SHOWNORMAL) > 32;
  else if (id == "openLog")
    ok = (INT_PTR)ShellExecuteW(nullptr, L"open", ProgramDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
  else if (id == "hum") {
    {
      std::lock_guard<std::mutex> lock(mu_);
      settings_.params.humOn = 1;
      settings_.params.humFreq = (float)body["arg"].asNum(50);
      markDirty();
    }
    pushParams();
    ok = true;
  } else if (id == "install" || id == "attach" || id == "restartAudio" || id == "enableSysfx") {
    Json b = Json::Object();
    b.set("action", id);
    return apiDriver(b, res);
  }
  Json out = Json::Object();
  out.set("ok", ok);
  res.json(out);
}

}  // namespace app
