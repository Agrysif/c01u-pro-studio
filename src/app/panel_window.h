#pragma once
// The panel's own window: a normal Windows window with the app's title, icon and taskbar button.
// The interface inside is drawn by WebView2 (part of Windows 10/11, the engine of Edge, but with no
// tabs, address bar, browser menus or translate offers). Closing the window leaves the app in the
// tray; the driver works either way.
#include <windows.h>

#include <string>

namespace app {

// Opens the panel or brings it to the front. false: the WebView2 runtime is missing (Windows 8),
// the caller falls back to a browser window.
bool OpenPanelWindow(const std::wstring& url, int iconId);
void ClosePanelWindow();

}  // namespace app
