#pragma once
// Driver installation: files, COM/APO registration, attaching the APO to microphone
// endpoints (MMDevices FxProperties) with backups, audio service restart, uninstall.
#include <string>
#include <vector>

namespace app {

struct DriverStatus {
  bool filesInstalled = false;     // exe + dll in Program Files
  bool registered = false;         // COM + AudioEngine registration present
  bool protectedAudioOff = false;  // DisableProtectedAudioDG = 1 (unsigned APOs may load)
  std::wstring installedVersion;
  std::wstring dllPath;
};

DriverStatus QueryDriverStatus();
std::wstring InstalledVersion();  // version of the copy in Program Files ("" if none)
bool EndpointHasApo(const std::wstring& endpointGuid);
bool EndpointSysFxDisabled(const std::wstring& endpointGuid);
std::wstring EndpointForeignSfx(const std::wstring& endpointGuid);  // another vendor's SFX CLSID, if any
std::vector<std::wstring> SamsonCaptureEndpointGuids();              // every Samson mic Windows ever saw

// Elevated entry points (called from WinMain of the elevated child). Return process exit codes.
int RunInstall(const std::vector<std::wstring>& extraEndpoints);
int RunUninstall(bool quiet);
int RunAttach(const std::vector<std::wstring>& endpoints);
int RunDetach(const std::vector<std::wstring>& endpoints);
int RunRestartAudio();
int RunEnableSysFx(const std::vector<std::wstring>& endpoints);

// From the normal (non-elevated) app: run this exe elevated and wait.
// Returns the child's exit code, -2 if the user declined the UAC prompt, -1 on other errors.
int LaunchElevated(const std::wstring& args, unsigned long timeoutMs);

bool SetAutostart(bool on);
bool GetAutostart();
std::wstring InstallLogPath();

}  // namespace app
