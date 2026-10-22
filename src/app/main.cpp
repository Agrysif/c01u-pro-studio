// C01U Pro Studio: tray app + local web UI + installer for the system APO.
//
//   C01UProStudio.exe                 start (or bring up the panel of the running copy)
//   C01UProStudio.exe --background    start silently (autostart)
//   C01UProStudio.exe --open          start and open the panel
//   C01UProStudio.exe --install [--endpoints {guid};{guid}]   (elevated)
//   C01UProStudio.exe --uninstall [--quiet]
//   C01UProStudio.exe --attach / --detach / --enable-sysfx {guid};...   (elevated)
//   C01UProStudio.exe --restart-audio  (elevated)
//   C01UProStudio.exe --update [--force]   (the SYSTEM task; see update.h)
#include <winsock2.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <locale.h>

#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "controller.h"
#include "install.h"
#include "panel_window.h"
#include "resource.h"
#include "tray_text.h"
#include "update.h"

using namespace app;

namespace {

const wchar_t kWindowClass[] = L"C01UProStudioWnd";
const wchar_t kMutexName[] = L"Local\\C01UProStudio.App";
const UINT kTrayId = 1;
const UINT_PTR kTimerId = 1;
const int kHotkeyId = 1;
enum : UINT { kCmdOpen = 100, kCmdMute, kCmdBypass, kCmdMonitor, kCmdExit };
enum : ULONG_PTR { kCopyOpen = 1, kCopyYield = 2 };

Controller* g_ctl = nullptr;
UINT g_taskbarCreated = 0;
HICON g_iconNormal = nullptr, g_iconMuted = nullptr;
bool g_trayAdded = false;
bool g_lastMuted = false;
bool g_hotkey = false;
bool g_quitByHandoff = false;

std::vector<std::wstring> SplitList(const std::wstring& s) {
  std::vector<std::wstring> out;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t e = s.find(L';', pos);
    if (e == std::wstring::npos) e = s.size();
    if (e > pos) out.push_back(s.substr(pos, e - pos));
    pos = e + 1;
  }
  return out;
}

struct Args {
  bool install = false, uninstall = false, attach = false, detach = false, restartAudio = false, enableSysfx = false;
  bool update = false, force = false;
  bool background = false, open = false, handoff = false, quiet = false;
  std::vector<std::wstring> endpoints;
};

Args ParseArgs() {
  Args a;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    std::wstring s = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
    if (s == L"--install") a.install = true;
    else if (s == L"--uninstall") a.uninstall = true;
    else if (s == L"--attach") a.attach = true, a.endpoints = SplitList(next());
    else if (s == L"--detach") a.detach = true, a.endpoints = SplitList(next());
    else if (s == L"--enable-sysfx") a.enableSysfx = true, a.endpoints = SplitList(next());
    else if (s == L"--endpoints") a.endpoints = SplitList(next());
    else if (s == L"--restart-audio") a.restartAudio = true;
    else if (s == L"--update") a.update = true;
    else if (s == L"--force") a.force = true;
    else if (s == L"--background") a.background = true;
    else if (s == L"--open") a.open = true;
    else if (s == L"--handoff") a.handoff = true;
    else if (s == L"--quiet") a.quiet = true;
  }
  if (argv) LocalFree(argv);
  return a;
}

std::wstring JoinArgs() {
  // the original command line minus the program name
  std::wstring cmd = GetCommandLineW();
  size_t pos = 0;
  if (!cmd.empty() && cmd[0] == L'"') {
    pos = cmd.find(L'"', 1);
    pos = pos == std::wstring::npos ? cmd.size() : pos + 1;
  } else {
    pos = cmd.find(L' ');
    if (pos == std::wstring::npos) pos = cmd.size();
  }
  while (pos < cmd.size() && cmd[pos] == L' ') ++pos;
  return cmd.substr(pos);
}

// ------------------------------------------------------------------ tray
const TrayText& Texts() { return TrayTextFor(g_ctl ? g_ctl->language() : std::string("en")); }

void UpdateTray(HWND hwnd, bool add) {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = hwnd;
  nid.uID = kTrayId;
  nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
  nid.uCallbackMessage = WM_APP_TRAY;
  const bool muted = g_ctl && g_ctl->muted();
  nid.hIcon = muted ? g_iconMuted : g_iconNormal;
  const TrayText& t = Texts();
  std::wstring tip = MS_PRODUCT_NAME;
  if (muted) tip += std::wstring(L"\n") + t.muted;
  else if (g_ctl && g_ctl->bypassed()) tip += std::wstring(L"\n") + t.processing + L": off";
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  if (add || !g_trayAdded) {
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
  } else {
    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }
  g_lastMuted = muted;
}

void RemoveTray(HWND hwnd) {
  NOTIFYICONDATAW nid = {sizeof(nid)};
  nid.hWnd = hwnd;
  nid.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  g_trayAdded = false;
}

void ShowTrayMenu(HWND hwnd) {
  const TrayText& t = Texts();
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, kCmdOpen, t.open);
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kCmdMute, g_ctl->muted() ? t.unmute : t.mute);
  AppendMenuW(m, MF_STRING | (g_ctl->bypassed() ? MF_UNCHECKED : MF_CHECKED), kCmdBypass, t.processing);
  AppendMenuW(m, MF_STRING | (g_ctl->monitoring() ? MF_CHECKED : MF_UNCHECKED), kCmdMonitor, t.monitor);
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kCmdExit, t.exit);
  SetMenuDefaultItem(m, kCmdOpen, FALSE);
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(m);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  switch (cmd) {
    case kCmdOpen: g_ctl->openUi(); break;
    case kCmdMute: g_ctl->toggleMute(); break;
    case kCmdBypass: g_ctl->toggleBypass(); break;
    case kCmdMonitor: g_ctl->toggleMonitor(); break;
    case kCmdExit: DestroyWindow(hwnd); break;
  }
  UpdateTray(hwnd, false);
}

void ApplyHotkey(HWND hwnd) {
  if (g_hotkey) UnregisterHotKey(hwnd, kHotkeyId);
  g_hotkey = false;
  int mods = 0, vk = 0;
  if (g_ctl && g_ctl->hotkey(&mods, &vk) && vk)
    g_hotkey = RegisterHotKey(hwnd, kHotkeyId, (UINT)mods | MOD_NOREPEAT, (UINT)vk) != FALSE;
}

// ------------------------------------------------------------------ device notifications
class DeviceWatcher : public IMMNotificationClient {
 public:
  explicit DeviceWatcher(HWND hwnd) : hwnd_(hwnd) {}
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
      *ppv = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return notify(); }
  STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return notify(); }
  STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return notify(); }
  STDMETHODIMP OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return notify(); }
  STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

 private:
  HRESULT notify() {
    PostMessageW(hwnd_, WM_APP_DEVICES, 0, 0);
    return S_OK;
  }
  HWND hwnd_;
  volatile LONG ref_ = 1;
};

// ------------------------------------------------------------------ window
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_taskbarCreated && g_taskbarCreated) {
    UpdateTray(hwnd, true);
    return 0;
  }
  switch (msg) {
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case NIN_SELECT:
        case NIN_KEYSELECT: g_ctl->openUi(); break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: ShowTrayMenu(hwnd); break;
      }
      return 0;
    case WM_APP_STATE: UpdateTray(hwnd, false); return 0;
    case WM_APP_HOTKEY_CHANGED: ApplyHotkey(hwnd); return 0;
    case WM_APP_DEVICES:
      if (g_ctl) g_ctl->onDevicesChanged();
      return 0;
    case WM_APP_QUIT:
      g_quitByHandoff = wp == 1;
      SetTimer(hwnd, 2, wp == 1 ? 700 : 10, nullptr);  // let the HTTP answer leave first
      return 0;
    case WM_HOTKEY:
      if (wp == kHotkeyId && g_ctl) {
        g_ctl->toggleMute();
        UpdateTray(hwnd, false);
      }
      return 0;
    case WM_TIMER:
      if (wp == kTimerId && g_ctl) {
        g_ctl->onTimer();
        if (g_ctl->muted() != g_lastMuted) UpdateTray(hwnd, false);
      } else if (wp == 2) {
        KillTimer(hwnd, 2);
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_COPYDATA: {
      const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)lp;
      if (cds->dwData == kCopyYield) {
        PostMessageW(hwnd, WM_APP_QUIT, 1, 0);
        return 2;
      }
      if (cds->dwData == kCopyOpen) {
        // a newer build asks to take over; an equal or older one just wants the panel
        std::wstring theirs((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
        while (!theirs.empty() && theirs.back() == 0) theirs.pop_back();
        if (theirs > std::wstring(MS_VERSION_WSTR)) {
          PostMessageW(hwnd, WM_APP_QUIT, 1, 0);
          return 2;
        }
        g_ctl->openUi();
        return 1;
      }
      return 0;
    }
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
      if (wp && g_ctl) g_ctl->shutdown();
      return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
      ClosePanelWindow();
      KillTimer(hwnd, kTimerId);
      if (g_hotkey) UnregisterHotKey(hwnd, kHotkeyId);
      RemoveTray(hwnd);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunElevatedCommand(const Args& a) {
  // e.g. "Uninstall" from Windows Settings starts us without admin rights
  if (!IsElevated()) return LaunchElevated(JoinArgs(), 300000);
  if (a.install) return RunInstall(a.endpoints);
  if (a.uninstall) return RunUninstall(a.quiet);
  if (a.attach) return RunAttach(a.endpoints);
  if (a.detach) return RunDetach(a.endpoints);
  if (a.enableSysfx) return RunEnableSysFx(a.endpoints);
  if (a.restartAudio) return RunRestartAudio();
  if (a.update) return RunUpdate(a.force);
  return 1;
}

std::string UiLanguage() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return "en";
  std::string s = ToUtf8(name);
  size_t dash = s.find('-');
  return Lower(dash == std::string::npos ? s : s.substr(0, dash));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  setlocale(LC_CTYPE, ".UTF8");  // %ls in logs: wide -> UTF-8 (numbers keep the "C" format)
  const Args args = ParseArgs();
  if (args.install || args.uninstall || args.attach || args.detach || args.restartAudio || args.enableSysfx ||
      args.update) {
    const int code = RunElevatedCommand(args);
    if (args.uninstall && !args.quiet && !IsElevated()) {
      const TrayText& t = TrayTextFor(UiLanguage());
      if (code == 0) MessageBoxW(nullptr, t.removed, MS_PRODUCT_NAME, MB_OK | MB_ICONINFORMATION);
    }
    return code;
  }

  // one running copy per user session
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND other = FindWindowW(kWindowClass, nullptr);
    bool takeOver = args.handoff;
    if (other && !args.handoff) {
      if (args.background) return 0;
      std::wstring ver = MS_VERSION_WSTR;
      DWORD otherPid = 0;
      GetWindowThreadProcessId(other, &otherPid);
      AllowSetForegroundWindow(otherPid);  // it may bring an open panel window to the front
      COPYDATASTRUCT cds = {kCopyOpen, (DWORD)((ver.size() + 1) * sizeof(wchar_t)), (PVOID)ver.c_str()};
      DWORD_PTR result = 0;
      SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &result);
      takeOver = result == 2;
      if (!takeOver) return 0;
    } else if (other && args.handoff) {
      COPYDATASTRUCT cds = {kCopyYield, 0, nullptr};
      DWORD_PTR result = 0;
      SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &result);
    }
    if (!takeOver && !other) {
      // the other copy is still starting or already leaving: wait for it
      takeOver = true;
    }
    DWORD w = WaitForSingleObject(mutex, 15000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) return 0;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDPIAware();
  // one taskbar button and pinning for the panel window and the shortcuts (same id there)
  SetCurrentProcessExplicitAppUserModelID(MS_APP_ID);
  EnsureDir(ProgramDataDir());
  Log("start %s (%ls) args='%ls'", MS_VERSION_STR, ExePath().c_str(), JoinArgs().c_str());

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kWindowClass;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, MS_PRODUCT_NAME, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                              nullptr);
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd, WM_CLOSE, MSGFLT_ALLOW, nullptr);
  if (g_taskbarCreated) ChangeWindowMessageFilterEx(hwnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

  const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
  g_iconNormal = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
  g_iconMuted = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_TRAY_MUTED), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);

  std::unique_ptr<Controller> ctl(new Controller());
  g_ctl = ctl.get();
  if (!ctl->init(hwnd)) {
    MessageBoxW(nullptr, L"C01U Pro Studio: cannot start the local web server (ports 47811-47830 are busy).",
                MS_PRODUCT_NAME, MB_OK | MB_ICONERROR);
    return 1;
  }
  UpdateTray(hwnd, true);
  ApplyHotkey(hwnd);
  SetTimer(hwnd, kTimerId, 250, nullptr);

  IMMDeviceEnumerator* en = nullptr;
  DeviceWatcher* watcher = new DeviceWatcher(hwnd);
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 (void**)&en)))
    en->RegisterEndpointNotificationCallback(watcher);

  if (args.handoff) ctl->ensureEnhancements();  // we were just installed
  if (!args.background && !args.handoff) ctl->openUi();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (en) {
    en->UnregisterEndpointNotificationCallback(watcher);
    en->Release();
  }
  watcher->Release();
  ctl->shutdown();
  g_ctl = nullptr;
  ctl.reset();
  Log("exit");
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  CoUninitialize();
  return 0;
}
