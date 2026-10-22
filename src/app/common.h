#pragma once
// Shared helpers for the control app.
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

#include "../shared/version.h"

namespace app {

std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::string Lower(std::string s);
std::wstring LowerW(std::wstring s);
bool ContainsI(const std::wstring& hay, const std::wstring& needle);

std::wstring ExePath();
std::wstring ExeDir();
std::wstring ProgramDataDir();     // %ProgramData%\C01U Pro Studio
std::wstring InstallDir();         // %ProgramFiles%\C01U Pro Studio
std::wstring InstalledExePath();
bool EnsureDir(const std::wstring& dir);
bool ReadFileBytes(const std::wstring& path, std::string* out);
bool WriteFileAtomic(const std::wstring& path, const void* data, size_t size);

bool IsElevated();
bool IsWindows10OrLater();
DWORD WindowsBuild();
std::string WindowsVersionString();

void Log(const char* fmt, ...);    // appends to %ProgramData%\...\app.log (small, rotated)

// Loads an RCDATA resource by name ("WEB/INDEX.HTML", "APO_DLL"). Pointer stays valid.
bool LoadResourceData(const wchar_t* name, const void** data, DWORD* size);

}  // namespace app
