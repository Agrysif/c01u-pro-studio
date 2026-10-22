#include "common.h"

#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <algorithm>

namespace app {

std::string ToUtf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

std::wstring FromUtf8(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::string Lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

std::wstring LowerW(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
  return s;
}

bool ContainsI(const std::wstring& hay, const std::wstring& needle) {
  return LowerW(hay).find(LowerW(needle)) != std::wstring::npos;
}

std::wstring ExePath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  return std::wstring(buf, n);
}

std::wstring ExeDir() {
  std::wstring p = ExePath();
  size_t pos = p.find_last_of(L'\\');
  return pos == std::wstring::npos ? p : p.substr(0, pos);
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id, const wchar_t* fallbackEnv, const wchar_t* fallback) {
  PWSTR path = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) out = path;
  if (path) CoTaskMemFree(path);
  if (out.empty()) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(fallbackEnv, buf, MAX_PATH);
    out = (n && n < MAX_PATH) ? std::wstring(buf) : std::wstring(fallback);
  }
  return out;
}

std::wstring ProgramDataDir() {
  return KnownFolder(FOLDERID_ProgramData, L"ProgramData", L"C:\\ProgramData") + L"\\" MS_DIR_NAME;
}

std::wstring InstallDir() {
  return KnownFolder(FOLDERID_ProgramFiles, L"ProgramFiles", L"C:\\Program Files") + L"\\" MS_DIR_NAME;
}

std::wstring InstalledExePath() { return InstallDir() + L"\\" MS_EXE_NAME; }

bool EnsureDir(const std::wstring& dir) {
  if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
  if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
  return SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) == ERROR_SUCCESS;
}

bool ReadFileBytes(const std::wstring& path, std::string* out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size;
  if (!GetFileSizeEx(f, &size) || size.QuadPart > 64 * 1024 * 1024) {
    CloseHandle(f);
    return false;
  }
  out->resize((size_t)size.QuadPart);
  DWORD read = 0;
  BOOL ok = size.QuadPart == 0 || ReadFile(f, &(*out)[0], (DWORD)size.QuadPart, &read, nullptr);
  CloseHandle(f);
  return ok && read == (DWORD)size.QuadPart;
}

bool WriteFileAtomic(const std::wstring& path, const void* data, size_t size) {
  std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(f, data, (DWORD)size, &written, nullptr) && written == size;
  FlushFileBuffers(f);
  CloseHandle(f);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION e = {};
  DWORD len = 0;
  BOOL ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &len);
  CloseHandle(token);
  return ok && e.TokenIsElevated;
}

static RTL_OSVERSIONINFOW OsVersion() {
  RTL_OSVERSIONINFOW v = {sizeof(v)};
  typedef LONG(WINAPI * RtlGetVersionFn)(RTL_OSVERSIONINFOW*);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  RtlGetVersionFn fn = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
  if (fn) fn(&v);
  return v;
}

bool IsWindows10OrLater() { return OsVersion().dwMajorVersion >= 10; }
DWORD WindowsBuild() { return OsVersion().dwBuildNumber; }

std::string WindowsVersionString() {
  RTL_OSVERSIONINFOW v = OsVersion();
  const char* name = "Windows";
  if (v.dwMajorVersion >= 10)
    name = v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
  else if (v.dwMajorVersion == 6 && v.dwMinorVersion == 3)
    name = "Windows 8.1";
  else if (v.dwMajorVersion == 6 && v.dwMinorVersion == 2)
    name = "Windows 8";
  else if (v.dwMajorVersion == 6 && v.dwMinorVersion == 1)
    name = "Windows 7";
  char buf[96];
  sprintf_s(buf, "%s (build %lu)", name, v.dwBuildNumber);
  return buf;
}

static std::mutex g_logMu;

void Log(const char* fmt, ...) {
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[1200];
  int n = sprintf_s(line, "%04d-%02d-%02d %02d:%02d:%02d [%lu] %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                    t.wSecond, GetCurrentProcessId(), msg);
  OutputDebugStringA(line);
  std::lock_guard<std::mutex> lock(g_logMu);
  std::wstring dir = ProgramDataDir();
  std::wstring path = dir + L"\\app.log";
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024) {
    std::wstring old = dir + L"\\app.old.log";
    MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
  }
  HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(f, line, (DWORD)n, &w, nullptr);
  CloseHandle(f);
}

bool LoadResourceData(const wchar_t* name, const void** data, DWORD* size) {
  HRSRC r = FindResourceW(nullptr, name, RT_RCDATA);
  if (!r) return false;
  HGLOBAL g = LoadResource(nullptr, r);
  if (!g) return false;
  *data = LockResource(g);
  *size = SizeofResource(nullptr, r);
  return *data != nullptr;
}

}  // namespace app
