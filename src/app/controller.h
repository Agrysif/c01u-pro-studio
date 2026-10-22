#pragma once
// Glue between the web API, the audio engine, the APO link, settings and the tray.
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "audio.h"
#include "http.h"
#include "settings.h"

namespace app {

constexpr UINT WM_APP_TRAY = WM_APP + 1;      // tray icon callback
constexpr UINT WM_APP_STATE = WM_APP + 2;     // refresh tray icon/tooltip
constexpr UINT WM_APP_QUIT = WM_APP + 3;      // leave the app (after uninstall / handoff)
constexpr UINT WM_APP_HOTKEY_CHANGED = WM_APP + 4;
constexpr UINT WM_APP_DEVICES = WM_APP + 5;   // audio devices changed

class Controller {
 public:
  bool init(HWND hwnd);
  void shutdown();
  void onTimer();
  void onDevicesChanged();

  // tray / hotkey
  void openUi();
  void toggleMute();
  void toggleBypass();
  void toggleMonitor();
  bool muted();
  bool bypassed();
  bool monitoring();
  std::string language();
  std::string trayTooltip();
  bool hotkey(int* mods, int* vk);
  int port() const { return http_.port(); }
  // Turns "Audio enhancements" on and keeps them on for a few seconds: right after the audio
  // service restarts it may still load an older "off" from the registry over ours.
  void ensureEnhancements();

  void handle(const HttpRequest& req, HttpResponse& res);

 private:
  // API
  Json stateJson();
  Json statusJson();
  Json metersJson();
  Json devicesJson();
  Json diagnostics();
  void serveStatic(const HttpRequest& req, HttpResponse& res);
  void apiParams(const Json& body, HttpResponse& res);
  void apiSettings(const Json& body, HttpResponse& res);
  void apiRecord(const Json& body, HttpResponse& res);
  void apiMeasure(const Json& body, HttpResponse& res);
  void apiFix(const Json& body, HttpResponse& res);
  void apiDriver(const Json& body, HttpResponse& res);
  void apiUpdate(const Json& body, HttpResponse& res);
  void events(SseStream& s);

  // helpers
  void pushParams();                       // settings_.params (+ temporary flags) -> APO + engine
  void markDirty();
  bool apoActiveForMic();
  std::wstring micGuid();
  void setTemporaryBypass(bool on);
  int runElevated(const std::wstring& args);
  bool EnableEnhancements();  // "Audio enhancements" on for the Samson / selected microphone
  void beep(bool up);

  HWND hwnd_ = nullptr;
  std::mutex mu_;
  AppSettings settings_;
  bool dirty_ = false;
  unsigned long long dirtyAt_ = 0;
  ParamsLink link_;
  AudioEngine engine_;
  HttpServer http_;
  std::atomic<int> busy_{0};               // measurements, recordings, diagnostics in flight
  std::atomic<bool> tempBypass_{false};
  std::atomic<bool> apoActive_{false};
  std::atomic<bool> elevating_{false};
  unsigned long long lastApoCheck_ = 0;
  unsigned long long enhanceUntil_ = 0, lastEnhanceCheck_ = 0;
  unsigned long long lastVersionCheck_ = 0;
  bool handingOff_ = false;
  bool micHasApo_ = false;
  std::wstring micId_;
  std::string sysLang_;
  std::string wavUp_, wavDown_;
  int lastRecState_ = 0;
};

}  // namespace app
