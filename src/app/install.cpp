#include "install.h"

#include <windows.h>
#include <aclapi.h>
#include <objbase.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdarg.h>
#include <stdio.h>
#include <tlhelp32.h>

#include <algorithm>

#include "../shared/guids.h"
#include "../shared/params.h"
#include "common.h"
#include "update.h"

namespace app {

namespace {

const wchar_t kCaptureRoot[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture";
const wchar_t kAudioKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kWindowClass[] = L"C01UProStudioWnd";

FILE* g_log = nullptr;

void ILog(const char* fmt, ...) {
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Log("install: %s", msg);
  if (g_log) {
    fputs(msg, g_log);  // UTF-8 bytes as they are
    fputs("\r\n", g_log);
    fflush(g_log);
  }
}

struct LogScope {
  LogScope(const char* what) {
    EnsureDir(ProgramDataDir());
    _wfopen_s(&g_log, InstallLogPath().c_str(), L"ab");
    SYSTEMTIME t;
    GetLocalTime(&t);
    ILog("==== %s %04d-%02d-%02d %02d:%02d:%02d, %s, version %s", what, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
         t.wSecond, WindowsVersionString().c_str(), MS_VERSION_STR);
  }
  ~LogScope() {
    if (g_log) fclose(g_log);
    g_log = nullptr;
  }
};

bool EnablePrivilege(const wchar_t* name) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
  TOKEN_PRIVILEGES tp = {1};
  bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) != FALSE;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  ok = ok && AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
  CloseHandle(token);
  return ok;
}

void EnableBackupRestore() {
  EnablePrivilege(SE_BACKUP_NAME);
  EnablePrivilege(SE_RESTORE_NAME);
}

// Opens a protected key for writing via the backup/restore privilege, creating it if needed.
HKEY OpenProtected(const std::wstring& path, bool create) {
  HKEY key = nullptr;
  LSTATUS st;
  if (create)
    st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, REG_OPTION_BACKUP_RESTORE,
                         KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &key, nullptr);
  else
    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &key);
  if (st != ERROR_SUCCESS) {
    // plain access as a fallback (admins usually have SetValue)
    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
    if (st != ERROR_SUCCESS) return nullptr;
  }
  return key;
}

struct RegValue {
  bool exists = false;
  DWORD type = 0;
  std::vector<BYTE> data;
};

RegValue ReadValue(HKEY key, const wchar_t* name) {
  RegValue v;
  DWORD size = 0;
  if (RegQueryValueExW(key, name, nullptr, &v.type, nullptr, &size) != ERROR_SUCCESS) return v;
  v.data.resize(size + 4);
  if (RegQueryValueExW(key, name, nullptr, &v.type, v.data.data(), &size) != ERROR_SUCCESS) return RegValue();
  v.data.resize(size);
  v.exists = true;
  return v;
}

std::wstring ValueString(const RegValue& v) {
  if (!v.exists || (v.type != REG_SZ && v.type != REG_EXPAND_SZ)) return std::wstring();
  std::wstring s((const wchar_t*)v.data.data(), v.data.size() / sizeof(wchar_t));
  while (!s.empty() && s.back() == 0) s.pop_back();
  return s;
}

std::vector<std::wstring> ValueMulti(const RegValue& v) {
  std::vector<std::wstring> out;
  if (!v.exists) return out;
  if (v.type == REG_SZ || v.type == REG_EXPAND_SZ) {
    std::wstring s = ValueString(v);
    if (!s.empty()) out.push_back(s);
    return out;
  }
  if (v.type != REG_MULTI_SZ) return out;
  const wchar_t* p = (const wchar_t*)v.data.data();
  const wchar_t* end = p + v.data.size() / sizeof(wchar_t);
  while (p < end && *p) {
    std::wstring s(p);
    out.push_back(s);
    p += s.size() + 1;
  }
  return out;
}

LSTATUS WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
  return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

LSTATUS WriteMulti(HKEY key, const wchar_t* name, const std::vector<std::wstring>& values) {
  std::wstring buf;
  for (auto& s : values) {
    buf += s;
    buf += L'\0';
  }
  buf += L'\0';
  return RegSetValueExW(key, name, 0, REG_MULTI_SZ, (const BYTE*)buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
}

LSTATUS WriteDword(HKEY key, const wchar_t* name, DWORD v) {
  return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

bool SameGuid(const std::wstring& a, const std::wstring& b) { return LowerW(a) == LowerW(b); }

std::wstring OurClsid() { return MS_APO_CLSID_STR; }

DWORD OsMinor() {
  RTL_OSVERSIONINFOW v = {sizeof(v)};
  typedef LONG(WINAPI * Fn)(RTL_OSVERSIONINFOW*);
  Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  if (fn) fn(&v);
  return v.dwMajorVersion * 100 + v.dwMinorVersion;  // 602 = Windows 8, 603 = 8.1, 1000 = 10/11
}

std::wstring EndpointPath(const std::wstring& guid) { return std::wstring(kCaptureRoot) + L"\\" + guid; }
std::wstring BackupPath(const std::wstring& guid, const wchar_t* sub) {
  return std::wstring(MS_REG_KEY) + L"\\Backup\\" + guid + L"\\" + sub;
}

// Saves the listed values of `src` once (the first time we touch an endpoint).
void BackupValues(HKEY src, const std::wstring& backupKeyPath, const std::vector<const wchar_t*>& names) {
  HKEY b = nullptr;
  DWORD disp = 0;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, backupKeyPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr,
                      &b, &disp) != ERROR_SUCCESS)
    return;
  if (disp == REG_OPENED_EXISTING_KEY) {
    RegCloseKey(b);
    return;  // already backed up by an earlier install
  }
  std::vector<std::wstring> absent;
  for (const wchar_t* n : names) {
    RegValue v = ReadValue(src, n);
    if (v.exists)
      RegSetValueExW(b, n, 0, v.type, v.data.data(), (DWORD)v.data.size());
    else
      absent.push_back(n);
  }
  WriteMulti(b, L"__absent", absent);
  RegCloseKey(b);
}

void RestoreValues(HKEY dst, const std::wstring& backupKeyPath) {
  HKEY b = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, backupKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &b) != ERROR_SUCCESS) return;
  for (const std::wstring& n : ValueMulti(ReadValue(b, L"__absent"))) RegDeleteValueW(dst, n.c_str());
  for (DWORD i = 0;; ++i) {
    wchar_t name[256];
    DWORD nameLen = 256, type = 0, size = 0;
    if (RegEnumValueW(b, i, name, &nameLen, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) break;
    if (wcscmp(name, L"__absent") == 0) continue;
    std::vector<BYTE> data(size + 2);
    nameLen = 256;
    if (RegEnumValueW(b, i, name, &nameLen, nullptr, &type, data.data(), &size) == ERROR_SUCCESS)
      RegSetValueExW(dst, name, 0, type, data.data(), size);
  }
  RegCloseKey(b);
}

void ClearDisableSysFx(HKEY key) {
  RegValue v = ReadValue(key, MS_PKEY_DISABLE_SYSFX);
  if (v.exists && v.type == REG_DWORD && v.data.size() >= 4 && *(DWORD*)v.data.data() != 0)
    WriteDword(key, MS_PKEY_DISABLE_SYSFX, 0);
}

bool AttachEndpoint(const std::wstring& guid) {
  const std::wstring ep = EndpointPath(guid);
  HKEY probe = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ep.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &probe) != ERROR_SUCCESS) {
    ILog("endpoint %ls not found", guid.c_str());
    return false;
  }
  RegCloseKey(probe);
  HKEY fx = OpenProtected(ep + L"\\FxProperties", true);
  if (!fx) {
    ILog("cannot open FxProperties of %ls (%lu)", guid.c_str(), GetLastError());
    return false;
  }
  BackupValues(fx, BackupPath(guid, L"Fx"),
               {MS_PKEY_FX_PREMIX, MS_PKEY_FX_STREAM, MS_PKEY_SFX_MODES, MS_PKEY_COMPOSITE_STREAM});
  const std::wstring clsid = OurClsid();
  bool ok = true;
  if (OsMinor() <= 602) {
    ok = WriteString(fx, MS_PKEY_FX_PREMIX, clsid) == ERROR_SUCCESS;  // Windows 8: LFX
  } else {
    ok = WriteString(fx, MS_PKEY_FX_STREAM, clsid) == ERROR_SUCCESS;  // 8.1 / 10 / 11: SFX
    if (!ReadValue(fx, MS_PKEY_SFX_MODES).exists) WriteMulti(fx, MS_PKEY_SFX_MODES, {MS_AUDIO_MODE_DEFAULT_STR});
    RegValue comp = ReadValue(fx, MS_PKEY_COMPOSITE_STREAM);
    if (comp.exists) {
      std::vector<std::wstring> list = ValueMulti(comp);
      bool have = false;
      for (auto& s : list) have = have || SameGuid(s, clsid);
      if (!have) {
        list.insert(list.begin(), clsid);
        WriteMulti(fx, MS_PKEY_COMPOSITE_STREAM, list);
      }
    }
  }
  // "Audio enhancements" must be on, otherwise Windows skips every APO of the endpoint.
  // Windows 10 keeps the switch in Properties, Windows 11 in FxProperties; Windows itself
  // turns it off after an effect failed to load, so an older broken install leaves it off.
  BackupValues(fx, BackupPath(guid, L"FxSysFx"), {MS_PKEY_DISABLE_SYSFX});
  ClearDisableSysFx(fx);
  RegCloseKey(fx);
  HKEY props = OpenProtected(ep + L"\\Properties", false);
  if (props) {
    BackupValues(props, BackupPath(guid, L"Props"), {MS_PKEY_DISABLE_SYSFX});
    ClearDisableSysFx(props);
    RegCloseKey(props);
  }
  ILog("attached to endpoint %ls: %s", guid.c_str(), ok ? "ok" : "FAILED");
  return ok;
}

void DetachEndpoint(const std::wstring& guid) {
  const std::wstring ep = EndpointPath(guid);
  HKEY fx = OpenProtected(ep + L"\\FxProperties", false);
  if (fx) {
    HKEY b = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BackupPath(guid, L"Fx").c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &b) ==
        ERROR_SUCCESS) {
      RegCloseKey(b);
      RestoreValues(fx, BackupPath(guid, L"Fx"));
    }
    RestoreValues(fx, BackupPath(guid, L"FxSysFx"));
    // whatever is left that still points to us goes away
    for (const wchar_t* n : {MS_PKEY_FX_PREMIX, MS_PKEY_FX_STREAM}) {
      if (SameGuid(ValueString(ReadValue(fx, n)), OurClsid())) RegDeleteValueW(fx, n);
    }
    RegValue comp = ReadValue(fx, MS_PKEY_COMPOSITE_STREAM);
    if (comp.exists) {
      std::vector<std::wstring> list = ValueMulti(comp), keep;
      for (auto& s : list)
        if (!SameGuid(s, OurClsid())) keep.push_back(s);
      if (keep.size() != list.size()) WriteMulti(fx, MS_PKEY_COMPOSITE_STREAM, keep);
    }
    RegCloseKey(fx);
  }
  HKEY props = OpenProtected(ep + L"\\Properties", false);
  if (props) {
    RestoreValues(props, BackupPath(guid, L"Props"));
    RegCloseKey(props);
  }
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(MS_REG_KEY) + L"\\Backup\\" + guid).c_str());
  ILog("detached from endpoint %ls", guid.c_str());
}

std::vector<std::wstring> AllCaptureEndpoints() {
  std::vector<std::wstring> out;
  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kCaptureRoot, 0, KEY_READ | KEY_WOW64_64KEY, &root) != ERROR_SUCCESS) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[128];
    DWORD len = 128;
    if (RegEnumKeyExW(root, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    out.push_back(name);
  }
  RegCloseKey(root);
  return out;
}

// --------------------------------------------------------------- services
bool WaitServiceState(SC_HANDLE svc, DWORD state, DWORD timeoutMs) {
  const ULONGLONG until = GetTickCount64() + timeoutMs;
  SERVICE_STATUS_PROCESS ss;
  DWORD needed = 0;
  while (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ss, sizeof(ss), &needed)) {
    if (ss.dwCurrentState == state) return true;
    if (GetTickCount64() > until) return false;
    Sleep(150);
  }
  return false;
}

// Dependent services we had to stop (e.g. a vendor's audio service); started again afterwards.
std::vector<std::wstring> g_stoppedDependents;

bool StopServiceTree(SC_HANDLE scm, const wchar_t* name) {
  SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
  if (!svc) return false;
  // dependents first
  DWORD bytes = 0, count = 0;
  EnumDependentServicesW(svc, SERVICE_ACTIVE, nullptr, 0, &bytes, &count);
  if (bytes) {
    std::vector<BYTE> buf(bytes);
    ENUM_SERVICE_STATUSW* deps = (ENUM_SERVICE_STATUSW*)buf.data();
    if (EnumDependentServicesW(svc, SERVICE_ACTIVE, deps, bytes, &bytes, &count))
      for (DWORD i = 0; i < count; ++i) {
        const std::wstring dep = deps[i].lpServiceName;
        if (_wcsicmp(dep.c_str(), L"Audiosrv") != 0) g_stoppedDependents.push_back(dep);
        StopServiceTree(scm, dep.c_str());
      }
  }
  SERVICE_STATUS st;
  ControlService(svc, SERVICE_CONTROL_STOP, &st);
  bool ok = WaitServiceState(svc, SERVICE_STOPPED, 15000);
  CloseServiceHandle(svc);
  ILog("service %ls stopped: %s", name, ok ? "yes" : "no");
  return ok;
}

bool StartServiceWait(SC_HANDLE scm, const wchar_t* name) {
  SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
  if (!svc) return false;
  StartServiceW(svc, 0, nullptr);
  bool ok = WaitServiceState(svc, SERVICE_RUNNING, 15000);
  CloseServiceHandle(svc);
  ILog("service %ls running: %s", name, ok ? "yes" : "no");
  return ok;
}

bool StopAudio() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  bool a = StopServiceTree(scm, L"Audiosrv");
  bool b = StopServiceTree(scm, L"AudioEndpointBuilder");
  CloseServiceHandle(scm);
  return a && b;
}

// Starts every stopped automatic-start service that depends on `name`.
void StartAutoDependents(SC_HANDLE scm, const wchar_t* name) {
  SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_ENUMERATE_DEPENDENTS);
  if (!svc) return;
  DWORD bytes = 0, count = 0;
  EnumDependentServicesW(svc, SERVICE_INACTIVE, nullptr, 0, &bytes, &count);
  std::vector<std::wstring> names;
  if (bytes) {
    std::vector<BYTE> buf(bytes);
    ENUM_SERVICE_STATUSW* deps = (ENUM_SERVICE_STATUSW*)buf.data();
    if (EnumDependentServicesW(svc, SERVICE_INACTIVE, deps, bytes, &bytes, &count))
      for (DWORD i = 0; i < count; ++i) names.push_back(deps[i].lpServiceName);
  }
  CloseServiceHandle(svc);
  for (const std::wstring& n : names) {
    SC_HANDLE d = OpenServiceW(scm, n.c_str(), SERVICE_QUERY_CONFIG);
    if (!d) continue;
    DWORD need = 0;
    QueryServiceConfigW(d, nullptr, 0, &need);
    std::vector<BYTE> cfgBuf(need ? need : sizeof(QUERY_SERVICE_CONFIGW));
    QUERY_SERVICE_CONFIGW* cfg = (QUERY_SERVICE_CONFIGW*)cfgBuf.data();
    const bool autoStart = QueryServiceConfigW(d, cfg, (DWORD)cfgBuf.size(), &need) && cfg->dwStartType == SERVICE_AUTO_START;
    CloseServiceHandle(d);
    if (autoStart) StartServiceWait(scm, n.c_str());
  }
}

bool StartAudio() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  bool a = StartServiceWait(scm, L"AudioEndpointBuilder");
  bool b = StartServiceWait(scm, L"Audiosrv");
  for (const std::wstring& n : g_stoppedDependents) StartServiceWait(scm, n.c_str());
  g_stoppedDependents.clear();
  StartAutoDependents(scm, L"AudioEndpointBuilder");  // also heals services left stopped earlier
  StartAutoDependents(scm, L"Audiosrv");
  CloseServiceHandle(scm);
  return a && b;
}

// --------------------------------------------------------------- files
bool SamePath(const std::wstring& a, const std::wstring& b) { return LowerW(a) == LowerW(b); }

bool WriteBinaryFile(const std::wstring& path, const void* data, DWORD size) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD w = 0;
  BOOL ok = WriteFile(f, data, size, &w, nullptr) && w == size;
  CloseHandle(f);
  return ok != FALSE;
}

// Writes a file that may be loaded/locked: on failure the old one is renamed out of the way.
bool ReplaceFile2(const std::wstring& path, const void* data, DWORD size) {
  if (WriteBinaryFile(path, data, size)) return true;
  std::wstring old = path + L".old" + std::to_wstring(GetTickCount64());
  if (!MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
  MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  return WriteBinaryFile(path, data, size);
}

bool CopySelfTo(const std::wstring& target) {
  const std::wstring self = ExePath();
  if (SamePath(self, target)) return true;
  if (CopyFileW(self.c_str(), target.c_str(), FALSE)) return true;
  std::string bytes;
  if (!ReadFileBytes(self, &bytes)) return false;
  return ReplaceFile2(target, bytes.data(), (DWORD)bytes.size());
}

void GrantDataDirAccess(const std::wstring& dir) {
  PACL oldDacl = nullptr, newDacl = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr,
                            &sd) != ERROR_SUCCESS)
    return;
  // Users and the audio engine (LOCAL SERVICE, NT SERVICE\Audiosrv) read and write here:
  // params.bin, the shared.bin block and the APO's own log.
  BYTE usersSid[SECURITY_MAX_SID_SIZE], lsSid[SECURITY_MAX_SID_SIZE], audioSid[SECURITY_MAX_SID_SIZE];
  DWORD n1 = sizeof(usersSid), n2 = sizeof(lsSid), n3 = sizeof(audioSid);
  CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSid, &n1);
  CreateWellKnownSid(WinLocalServiceSid, nullptr, lsSid, &n2);
  wchar_t domain[64];
  DWORD domainLen = 64;
  SID_NAME_USE use;
  const bool haveAudio = LookupAccountNameW(nullptr, L"NT SERVICE\\Audiosrv", audioSid, &n3, domain, &domainLen, &use) != FALSE;
  EXPLICIT_ACCESSW ea[3] = {};
  ea[0].grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
  ea[0].grfAccessMode = GRANT_ACCESS;
  ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  ea[0].Trustee.ptstrName = (LPWSTR)usersSid;
  ea[1] = ea[0];
  ea[1].Trustee.TrusteeType = TRUSTEE_IS_USER;
  ea[1].Trustee.ptstrName = (LPWSTR)lsSid;
  ea[2] = ea[1];
  ea[2].Trustee.ptstrName = (LPWSTR)audioSid;
  if (SetEntriesInAclW(haveAudio ? 3 : 2, ea, oldDacl, &newDacl) == ERROR_SUCCESS) {
    SetNamedSecurityInfoW((LPWSTR)dir.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
    LocalFree(newDacl);
  }
  if (sd) LocalFree(sd);
}

bool CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args) {
  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link))) return false;
  link->SetPath(target.c_str());
  link->SetArguments(args.c_str());
  link->SetIconLocation(target.c_str(), 0);
  link->SetDescription(L"Samson C01U Pro: sound settings, monitoring and diagnostics");
  IPersistFile* pf = nullptr;
  bool ok = SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf)) && SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
  if (pf) pf->Release();
  link->Release();
  return ok;
}

std::wstring StartMenuShortcut() {
  PWSTR p = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &p)) && p) dir = p;
  if (p) CoTaskMemFree(p);
  return dir.empty() ? std::wstring() : dir + L"\\" MS_PRODUCT_NAME L".lnk";
}

DWORD ParentPid() {
  const DWORD self = GetCurrentProcessId();
  DWORD parent = 0;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe = {sizeof(pe)};
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    if (pe.th32ProcessID == self) {
      parent = pe.th32ParentProcessID;
      break;
    }
  CloseHandle(snap);
  return parent;
}

std::wstring ProcessPath(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return std::wstring();
  wchar_t buf[MAX_PATH * 2];
  DWORD n = (DWORD)std::size(buf);
  std::wstring out = QueryFullProcessImageNameW(h, 0, buf, &n) ? std::wstring(buf, n) : std::wstring();
  CloseHandle(h);
  return out;
}

// Asks running copies of the app to quit. The copy that launched us (it waits for our exit
// code) is left alone; with `onlyPath` only copies started from that file are closed.
void CloseOtherInstances(const std::wstring& onlyPath = std::wstring()) {
  const DWORD self = GetCurrentProcessId(), parent = ParentPid();
  for (int round = 0; round < 3; ++round) {
    HWND w = nullptr;
    bool any = false;
    while ((w = FindWindowExW(nullptr, w, kWindowClass, nullptr)) != nullptr) {
      DWORD pid = 0;
      GetWindowThreadProcessId(w, &pid);
      if (pid == self || pid == parent) continue;
      if (!onlyPath.empty() && LowerW(ProcessPath(pid)) != LowerW(onlyPath)) continue;
      PostMessageW(w, WM_CLOSE, 0, 0);
      any = true;
    }
    if (!any) break;
    Sleep(800);
  }
}

bool RegisterApoDll(const std::wstring& dll) {
  HMODULE m = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!m) {
    ILog("LoadLibrary(%ls) failed %lu", dll.c_str(), GetLastError());
    return false;
  }
  typedef HRESULT(STDAPICALLTYPE * Fn)();
  Fn reg = (Fn)GetProcAddress(m, "DllRegisterServer");
  HRESULT hr = reg ? reg() : E_FAIL;
  FreeLibrary(m);
  ILog("DllRegisterServer: 0x%08lx", hr);
  return SUCCEEDED(hr);
}

void UnregisterApo() {
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" MS_APO_CLSID_STR);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" MS_APO_CLSID_STR);
}

void SetProtectedAudioOff() {
  HKEY k = OpenProtected(kAudioKey, true);
  if (!k) {
    ILog("cannot open %ls", kAudioKey);
    return;
  }
  // remember the original value once
  HKEY ours = nullptr;
  DWORD disp = 0;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, MS_REG_KEY, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &ours,
                      &disp) == ERROR_SUCCESS) {
    if (!ReadValue(ours, L"OrigDisableProtectedAudioDG").exists) {
      RegValue v = ReadValue(k, L"DisableProtectedAudioDG");
      DWORD orig = (v.exists && v.type == REG_DWORD && v.data.size() >= 4) ? *(DWORD*)v.data.data() : 0xFFFFFFFF;
      WriteDword(ours, L"OrigDisableProtectedAudioDG", orig);
    }
    RegCloseKey(ours);
  }
  LSTATUS st = WriteDword(k, L"DisableProtectedAudioDG", 1);
  ILog("DisableProtectedAudioDG=1: %ld", st);
  RegCloseKey(k);
}

void RestoreProtectedAudio() {
  HKEY ours = nullptr;
  DWORD orig = 0xFFFFFFFF;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, MS_REG_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &ours) == ERROR_SUCCESS) {
    RegValue v = ReadValue(ours, L"OrigDisableProtectedAudioDG");
    if (v.exists && v.data.size() >= 4) orig = *(DWORD*)v.data.data();
    RegCloseKey(ours);
  }
  HKEY k = OpenProtected(kAudioKey, false);
  if (!k) return;
  if (orig == 0xFFFFFFFF)
    RegDeleteValueW(k, L"DisableProtectedAudioDG");
  else
    WriteDword(k, L"DisableProtectedAudioDG", orig);
  RegCloseKey(k);
}

void WriteUninstallEntry(const std::wstring& exe) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, MS_UNINSTALL_KEY, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &k,
                      nullptr) != ERROR_SUCCESS)
    return;
  WriteString(k, L"DisplayName", MS_PRODUCT_NAME L" (Samson C01U Pro)");
  WriteString(k, L"DisplayVersion", MS_VERSION_WSTR);
  WriteString(k, L"Publisher", MS_PUBLISHER);
  WriteString(k, L"DisplayIcon", exe + L",0");
  WriteString(k, L"InstallLocation", InstallDir());
  WriteString(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
  WriteString(k, L"QuietUninstallString", L"\"" + exe + L"\" --uninstall --quiet");
  WriteString(k, L"Comments", MS_COPYRIGHT);
  WriteDword(k, L"NoModify", 1);
  WriteDword(k, L"NoRepair", 1);
  WriteDword(k, L"EstimatedSize", 1500);
  RegCloseKey(k);
}

void AddUnique(std::vector<std::wstring>& v, const std::wstring& g) {
  for (auto& x : v)
    if (SameGuid(x, g)) return;
  v.push_back(g);
}

}  // namespace

std::wstring InstallLogPath() { return ProgramDataDir() + L"\\install.log"; }

// --------------------------------------------------------------- queries
DriverStatus QueryDriverStatus() {
  DriverStatus s;
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" MS_APO_CLSID_STR L"\\InprocServer32", 0,
                    KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
    s.dllPath = ValueString(ReadValue(k, nullptr));
    RegCloseKey(k);
  }
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" MS_APO_CLSID_STR, 0,
                    KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
    s.registered = !s.dllPath.empty();
    RegCloseKey(k);
  }
  s.filesInstalled = GetFileAttributesW(InstalledExePath().c_str()) != INVALID_FILE_ATTRIBUTES &&
                     !s.dllPath.empty() && GetFileAttributesW(s.dllPath.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kAudioKey, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
    RegValue v = ReadValue(k, L"DisableProtectedAudioDG");
    s.protectedAudioOff = v.exists && v.type == REG_DWORD && v.data.size() >= 4 && *(DWORD*)v.data.data() == 1;
    RegCloseKey(k);
  }
  s.installedVersion = InstalledVersion();
  return s;
}

std::wstring InstalledVersion() {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, MS_UNINSTALL_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
    return std::wstring();
  const std::wstring v = ValueString(ReadValue(k, L"DisplayVersion"));
  RegCloseKey(k);
  return v;
}

bool EndpointHasApo(const std::wstring& guid) {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (EndpointPath(guid) + L"\\FxProperties").c_str(), 0, KEY_READ | KEY_WOW64_64KEY,
                    &k) != ERROR_SUCCESS)
    return false;
  bool has = false;
  for (const wchar_t* n : {MS_PKEY_FX_STREAM, MS_PKEY_FX_PREMIX})
    has = has || SameGuid(ValueString(ReadValue(k, n)), OurClsid());
  for (auto& s : ValueMulti(ReadValue(k, MS_PKEY_COMPOSITE_STREAM))) has = has || SameGuid(s, OurClsid());
  RegCloseKey(k);
  return has;
}

std::wstring EndpointForeignSfx(const std::wstring& guid) {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (EndpointPath(guid) + L"\\FxProperties").c_str(), 0, KEY_READ | KEY_WOW64_64KEY,
                    &k) != ERROR_SUCCESS)
    return std::wstring();
  std::wstring out;
  for (const wchar_t* n : {MS_PKEY_FX_STREAM, MS_PKEY_FX_PREMIX}) {
    std::wstring v = ValueString(ReadValue(k, n));
    if (!v.empty() && !SameGuid(v, OurClsid())) out = v;
  }
  RegCloseKey(k);
  return out;
}

bool EndpointSysFxDisabled(const std::wstring& guid) {
  for (const wchar_t* sub : {L"\\FxProperties", L"\\Properties"}) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (EndpointPath(guid) + sub).c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &k) !=
        ERROR_SUCCESS)
      continue;
    RegValue v = ReadValue(k, MS_PKEY_DISABLE_SYSFX);
    RegCloseKey(k);
    if (v.exists && v.type == REG_DWORD && v.data.size() >= 4 && *(DWORD*)v.data.data() != 0) return true;
  }
  return false;
}

std::vector<std::wstring> SamsonCaptureEndpointGuids() {
  std::vector<std::wstring> out;
  for (const std::wstring& g : AllCaptureEndpoints()) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (EndpointPath(g) + L"\\Properties").c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &k) !=
        ERROR_SUCCESS)
      continue;
    std::wstring hw = LowerW(ValueString(ReadValue(k, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},2")));
    std::wstring name = ValueString(ReadValue(k, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6"));
    RegCloseKey(k);
    if (hw.find(L"vid_17a0") != std::wstring::npos || ContainsI(name, L"samson") || ContainsI(name, L"c01u"))
      out.push_back(g);
  }
  return out;
}

// --------------------------------------------------------------- elevated operations
int RunInstall(const std::vector<std::wstring>& extraEndpoints) {
  LogScope log("install");
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  EnableBackupRestore();
  const std::wstring dir = InstallDir();
  const std::wstring exe = InstalledExePath();
  const std::wstring dll = dir + L"\\" MS_APO_DLL_NAME;
  if (!EnsureDir(dir)) {
    ILog("cannot create %ls (%lu)", dir.c_str(), GetLastError());
    return 10;
  }
  if (!SamePath(ExePath(), exe)) CloseOtherInstances(exe);  // an older installed copy holds the file
  if (!CopySelfTo(exe)) {
    ILog("cannot copy the app to %ls (%lu)", exe.c_str(), GetLastError());
    return 10;
  }
  const void* data = nullptr;
  DWORD size = 0;
  if (!LoadResourceData(L"APO_DLL", &data, &size)) {
    ILog("APO dll resource missing");
    return 11;
  }
  // audiodg keeps the old dll loaded, and the endpoint builder keeps its own copy of the
  // endpoint settings that it would write back over ours: stop both before touching anything
  StopAudio();
  if (!ReplaceFile2(dll, data, size)) {
    ILog("cannot write %ls (%lu)", dll.c_str(), GetLastError());
    StartAudio();
    return 11;
  }
  if (!RegisterApoDll(dll)) {
    StartAudio();
    return 12;
  }
  const std::wstring dataDir = ProgramDataDir();
  EnsureDir(dataDir);
  GrantDataDirAccess(dataDir);
  if (GetFileAttributesW((dataDir + L"\\params.bin").c_str()) == INVALID_FILE_ATTRIBUTES) {
    ms::Params p;
    ms::DefaultParams(&p);
    WriteBinaryFile(dataDir + L"\\params.bin", &p, sizeof(p));
  }
  SetProtectedAudioOff();

  std::vector<std::wstring> endpoints = SamsonCaptureEndpointGuids();
  for (auto& g : extraEndpoints) AddUnique(endpoints, g);
  int attached = 0;
  for (auto& g : endpoints) attached += AttachEndpoint(g) ? 1 : 0;
  ILog("endpoints attached: %d of %d", attached, (int)endpoints.size());

  WriteUninstallEntry(exe);
  const std::wstring lnk = StartMenuShortcut();
  if (!lnk.empty()) CreateShortcut(lnk, exe, L"--open");

  const bool started = StartAudio();
  ILog("install finished, audio restarted: %s", started ? "yes" : "no");
  ILog("update task: %s", RegisterUpdateTask(exe) ? "ok" : "FAILED");
  CoUninitialize();
  if (endpoints.empty()) return 13;  // installed, but no microphone to attach to
  if (attached == 0) return 14;
  return started ? 0 : 15;
}

int RunAttach(const std::vector<std::wstring>& endpoints) {
  LogScope log("attach");
  EnableBackupRestore();
  StopAudio();
  int ok = 0;
  for (auto& g : endpoints) ok += AttachEndpoint(g) ? 1 : 0;
  const bool started = StartAudio();
  if (ok == 0) return 14;
  return started ? 0 : 15;
}

int RunDetach(const std::vector<std::wstring>& endpoints) {
  LogScope log("detach");
  EnableBackupRestore();
  StopAudio();
  for (auto& g : endpoints) DetachEndpoint(g);
  return StartAudio() ? 0 : 15;
}

int RunRestartAudio() {
  LogScope log("restart audio");
  StopAudio();
  return StartAudio() ? 0 : 15;
}

int RunEnableSysFx(const std::vector<std::wstring>& endpoints) {
  LogScope log("enable enhancements");
  EnableBackupRestore();
  StopAudio();
  for (auto& g : endpoints) {
    for (const wchar_t* sub : {L"\\FxProperties", L"\\Properties"}) {
      HKEY k = OpenProtected(EndpointPath(g) + sub, false);
      if (!k) continue;
      ClearDisableSysFx(k);
      RegCloseKey(k);
    }
  }
  return StartAudio() ? 0 : 15;
}

int RunUninstall(bool quiet) {
  (void)quiet;
  LogScope log("uninstall");
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  EnableBackupRestore();
  CloseOtherInstances();
  // every endpoint we touched (backups) plus any that still point to us
  std::vector<std::wstring> touched;
  HKEY b = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (std::wstring(MS_REG_KEY) + L"\\Backup").c_str(), 0, KEY_READ | KEY_WOW64_64KEY,
                    &b) == ERROR_SUCCESS) {
    for (DWORD i = 0;; ++i) {
      wchar_t name[128];
      DWORD len = 128;
      if (RegEnumKeyExW(b, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      touched.push_back(name);
    }
    RegCloseKey(b);
  }
  for (const std::wstring& g : AllCaptureEndpoints())
    if (EndpointHasApo(g)) AddUnique(touched, g);
  RemoveUpdateTask();
  StopAudio();  // before the registry: the endpoint builder would write its copy back
  for (auto& g : touched) DetachEndpoint(g);
  RestoreProtectedAudio();
  UnregisterApo();
  const std::wstring dir = InstallDir();
  RemoveDirectoryW((dir + L"\\update").c_str());
  const std::wstring dll = dir + L"\\" MS_APO_DLL_NAME;
  if (!DeleteFileW(dll.c_str())) MoveFileExW(dll.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  StartAudio();

  RegDeleteTreeW(HKEY_LOCAL_MACHINE, MS_UNINSTALL_KEY);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, MS_REG_KEY);
  HKEY run = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
    RegDeleteValueW(run, MS_RUN_VALUE);
    RegCloseKey(run);
  }
  const std::wstring lnk = StartMenuShortcut();
  if (!lnk.empty()) DeleteFileW(lnk.c_str());
  ILog("uninstall finished");
  if (g_log) {
    fclose(g_log);
    g_log = nullptr;
  }
  // settings and logs
  SHFILEOPSTRUCTW op = {};
  std::wstring dataDir = ProgramDataDir();
  dataDir.push_back(L'\0');
  op.wFunc = FO_DELETE;
  op.pFrom = dataDir.c_str();
  op.fFlags = FOF_NO_UI;
  SHFileOperationW(&op);
  // the running exe lives in the install dir: remove it once we have exited
  std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + dir + L"\"";
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, L"C:\\", &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  CoUninitialize();
  return 0;
}

// --------------------------------------------------------------- non-elevated helpers
int LaunchElevated(const std::wstring& args, unsigned long timeoutMs) {
  const std::wstring exe = ExePath();
  SHELLEXECUTEINFOW sei = {sizeof(sei)};
  sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  sei.lpVerb = L"runas";
  sei.lpFile = exe.c_str();
  sei.lpParameters = args.c_str();
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei)) {
    DWORD e = GetLastError();
    Log("elevation failed: %lu", e);
    return e == ERROR_CANCELLED ? -2 : -1;
  }
  if (!sei.hProcess) return -1;
  DWORD w = WaitForSingleObject(sei.hProcess, timeoutMs);
  DWORD code = (DWORD)-1;
  if (w == WAIT_OBJECT_0) GetExitCodeProcess(sei.hProcess, &code);
  CloseHandle(sei.hProcess);
  return (int)code;
}

bool SetAutostart(bool on) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &k, nullptr) !=
      ERROR_SUCCESS)
    return false;
  LSTATUS st;
  if (on) {
    std::wstring exe = GetFileAttributesW(InstalledExePath().c_str()) != INVALID_FILE_ATTRIBUTES ? InstalledExePath() : ExePath();
    st = WriteString(k, MS_RUN_VALUE, L"\"" + exe + L"\" --background");
  } else {
    st = RegDeleteValueW(k, MS_RUN_VALUE);
    if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
  }
  RegCloseKey(k);
  return st == ERROR_SUCCESS;
}

bool GetAutostart() {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
  bool has = ReadValue(k, MS_RUN_VALUE).exists;
  RegCloseKey(k);
  return has;
}

}  // namespace app
