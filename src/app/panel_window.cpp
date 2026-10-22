#include "panel_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <algorithm>

#include <WebView2.h>

#include "common.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace app {

namespace {

const wchar_t kClass[] = L"C01UProStudioPanel";
const wchar_t kRegKey[] = L"Software\\C01U Pro Studio";
const COLORREF kBack = RGB(20, 16, 13);  // the panel's dark background, also shown while it loads

HWND g_wnd = nullptr;
HBRUSH g_back = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_view;
std::wstring g_url, g_origin;

UINT DpiOf(HWND h) {
  typedef UINT(WINAPI * Fn)(HWND);  // Windows 10+
  static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  return fn && h ? fn(h) : 96;
}

void OpenExternal(const wchar_t* uri) {
  if (uri && (wcsncmp(uri, L"https://", 8) == 0 || wcsncmp(uri, L"http://", 7) == 0))
    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}

void SavePlacement() {
  WINDOWPLACEMENT wp = {sizeof(wp)};
  if (g_wnd && GetWindowPlacement(g_wnd, &wp))
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"PanelPlacement", REG_BINARY, &wp, sizeof(wp));
}

// The last size and position, if it still lands on a monitor.
bool LoadPlacement(WINDOWPLACEMENT* wp) {
  DWORD size = sizeof(*wp);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"PanelPlacement", RRF_RT_REG_BINARY, nullptr, wp, &size) != ERROR_SUCCESS ||
      size != sizeof(*wp) || wp->length != sizeof(*wp))
    return false;
  return MonitorFromRect(&wp->rcNormalPosition, MONITOR_DEFAULTTONULL) != nullptr;
}

void FitBrowser() {
  if (!g_controller) return;
  RECT r;
  GetClientRect(g_wnd, &r);
  g_controller->put_Bounds(r);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      FitBrowser();
      if (g_controller) g_controller->put_IsVisible(wp != SIZE_MINIMIZED);
      return 0;
    case WM_GETMINMAXINFO: {
      const UINT dpi = DpiOf(h);
      MINMAXINFO* mm = (MINMAXINFO*)lp;
      mm->ptMinTrackSize.x = MulDiv(960, dpi, 96);
      mm->ptMinTrackSize.y = MulDiv(640, dpi, 96);
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* r = (const RECT*)lp;
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && g_controller) g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
      return 0;
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(h, &r);
      FillRect((HDC)wp, &r, g_back);
      return 1;
    }
    case WM_CLOSE:
      SavePlacement();
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      // the browser engine and its memory go away with the window; the tray app keeps running
      if (g_controller) g_controller->Close();
      g_view.Reset();
      g_controller.Reset();
      g_wnd = nullptr;
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

HRESULT OnController(HRESULT result, ICoreWebView2Controller* controller) {
  if (FAILED(result) || !controller || !g_wnd) {
    Log("panel window: WebView2 controller failed 0x%08lx", result);
    return S_OK;
  }
  g_controller = controller;
  g_controller->get_CoreWebView2(&g_view);
  ComPtr<ICoreWebView2Controller2> c2;
  if (SUCCEEDED(g_controller.As(&c2))) c2->put_DefaultBackgroundColor({255, GetRValue(kBack), GetGValue(kBack), GetBValue(kBack)});

  // an app, not a browser: no context menu, dev tools, zoom, browser shortcuts, autofill
  ComPtr<ICoreWebView2Settings> s;
  if (SUCCEEDED(g_view->get_Settings(&s))) {
    s->put_AreDefaultContextMenusEnabled(FALSE);
    s->put_AreDevToolsEnabled(FALSE);
    s->put_IsStatusBarEnabled(FALSE);
    s->put_IsZoomControlEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> s3;
    if (SUCCEEDED(s.As(&s3))) s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    ComPtr<ICoreWebView2Settings4> s4;
    if (SUCCEEDED(s.As(&s4))) {
      s4->put_IsPasswordAutosaveEnabled(FALSE);
      s4->put_IsGeneralAutofillEnabled(FALSE);
    }
    ComPtr<ICoreWebView2Settings5> s5;
    if (SUCCEEDED(s.As(&s5))) s5->put_IsPinchZoomEnabled(FALSE);
    ComPtr<ICoreWebView2Settings6> s6;
    if (SUCCEEDED(s.As(&s6))) s6->put_IsSwipeNavigationEnabled(FALSE);
  }
  // links outside the panel (GitHub, help pages) open in the default browser
  EventRegistrationToken token;
  g_view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                     [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                       LPWSTR uri = nullptr;
                                       if (SUCCEEDED(args->get_Uri(&uri))) OpenExternal(uri);
                                       CoTaskMemFree(uri);
                                       args->put_Handled(TRUE);
                                       return S_OK;
                                     })
                                     .Get(),
                                 &token);
  g_view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                     [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                       LPWSTR uri = nullptr;
                                       if (SUCCEEDED(args->get_Uri(&uri)) && uri && wcsncmp(uri, g_origin.c_str(), g_origin.size()) != 0) {
                                         OpenExternal(uri);
                                         args->put_Cancel(TRUE);
                                       }
                                       CoTaskMemFree(uri);
                                       return S_OK;
                                     })
                                     .Get(),
                                 &token);
  FitBrowser();
  g_view->Navigate(g_url.c_str());
  return S_OK;
}

}  // namespace

bool OpenPanelWindow(const std::wstring& url, int iconId) {
  if (g_wnd) {
    if (IsIconic(g_wnd)) ShowWindow(g_wnd, SW_RESTORE);
    SetForegroundWindow(g_wnd);
    return true;
  }
  LPWSTR version = nullptr;
  if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) || !version) return false;
  CoTaskMemFree(version);

  HINSTANCE inst = GetModuleHandleW(nullptr);
  if (!g_back) {
    g_back = CreateSolidBrush(kBack);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(iconId), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(iconId), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_back;
    RegisterClassExW(&wc);
  }
  // 1240×860 in the middle of the monitor with the mouse, or where the user left it
  POINT pt;
  GetCursorPos(&pt);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
  const RECT& wa = mi.rcWork;
  HDC dc = GetDC(nullptr);
  const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  const int w = std::min<int>(MulDiv(1240, dpi, 96), (wa.right - wa.left) * 92 / 100);
  const int ht = std::min<int>(MulDiv(860, dpi, 96), (wa.bottom - wa.top) * 92 / 100);
  g_wnd = CreateWindowExW(0, kClass, MS_PRODUCT_NAME, WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - w) / 2,
                          wa.top + (wa.bottom - wa.top - ht) / 2, w, ht, nullptr, nullptr, inst, nullptr);
  if (!g_wnd) return false;
  // dark title bar in the colours of the panel (Windows 10 2004+ / Windows 11; ignored elsewhere)
  BOOL dark = TRUE;
  DwmSetWindowAttribute(g_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  COLORREF caption = RGB(26, 20, 16), text = RGB(241, 230, 207);
  DwmSetWindowAttribute(g_wnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
  DwmSetWindowAttribute(g_wnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
  WINDOWPLACEMENT wp = {sizeof(wp)};
  if (LoadPlacement(&wp)) {
    if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(g_wnd, &wp);
  } else {
    ShowWindow(g_wnd, SW_SHOWNORMAL);
  }
  SetForegroundWindow(g_wnd);

  g_url = url;
  g_origin = url.substr(0, url.find(L'/', url.find(L"//") + 2));  // http://127.0.0.1:47811
  wchar_t* local = nullptr;
  std::wstring data;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) data = std::wstring(local) + L"\\" MS_DIR_NAME L"\\WebView2";
  CoTaskMemFree(local);
  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, data.empty() ? nullptr : data.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(result) || !env || !g_wnd) {
          Log("panel window: WebView2 environment failed 0x%08lx", result);
          return S_OK;
        }
        return env->CreateCoreWebView2Controller(g_wnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(OnController).Get());
      }).Get());
  if (FAILED(hr)) {
    Log("panel window: WebView2 unavailable 0x%08lx", hr);
    DestroyWindow(g_wnd);
    return false;
  }
  return true;
}

void ClosePanelWindow() {
  if (g_wnd) SendMessageW(g_wnd, WM_CLOSE, 0, 0);
}

}  // namespace app
