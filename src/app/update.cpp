#include "update.h"

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <taskschd.h>

#include <stdio.h>
#include <time.h>

#include "../shared/shared_state.h"
#include "common.h"
#include "update_core.h"

namespace app {

namespace {

const wchar_t kTaskName[] = L"C01U Pro Studio Updater";
const char kProductId[] = "c01u-pro-studio";
const long long kCheckEverySec = 6 * 3600;
const long long kWaitForIdleSec = 48 * 3600;  // then install even if the microphone is busy

std::wstring StatePath() { return ProgramDataDir() + L"\\update-state.json"; }
std::wstring OffMarker() { return ProgramDataDir() + L"\\no-auto-update"; }
std::wstring NowMarker() { return ProgramDataDir() + L"\\update-now"; }
std::wstring UpdateDir() { return InstallDir() + L"\\update"; }

Json LoadState() {
  std::string text;
  Json j;
  if (ReadFileBytes(StatePath(), &text) && Json::Parse(text, &j) && j.isObject()) return j;
  return Json::Object();
}

void SaveState(const Json& j) {
  const std::string s = j.dump();
  WriteFileAtomic(StatePath(), s.data(), s.size());
}

void SetStatus(Json& st, const char* status, const std::string& error = std::string()) {
  st.set("status", status);
  if (error.empty()) st.erase("error");
  else st.set("error", error);
  SaveState(st);
  Log("update: %s%s%s", status, error.empty() ? "" : " - ", error.c_str());
}

// An app records from the microphone right now (our APO counts its running streams).
bool MicrophoneBusy() {
  HANDLE file = nullptr, map = nullptr;
  DWORD err = 0;
  ms::SharedState* s = ms::MapSharedFile(&file, &map, &err);
  if (!s) return false;
  const bool busy = s->magic == ms::kSharedMagic && s->tele.activeStreams > 0 &&
                    GetTickCount64() - (ULONGLONG)s->tele.lastTickMs < 5000;
  ms::UnmapSharedFile(s, file, map);
  return busy;
}

// Old exe/dll copies renamed away by earlier updates (the ones still in use stay).
void CleanupLeftovers() {
  for (const std::wstring& pattern : {InstallDir() + L"\\*.old*", UpdateDir() + L"\\*.exe"}) {
    const std::wstring dir = pattern.substr(0, pattern.rfind(L'\\'));
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
}

int RunAndWait(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs) {
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
  DWORD code = (DWORD)-1;
  if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
}

std::string UserAgent() { return std::string(MS_PRODUCT_NAME_A "/" MS_VERSION_STR " (Windows ") + WindowsVersionString() + ")"; }

// ---- Task Scheduler
struct Com {
  bool uninit;
  Com() { uninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
  ~Com() {
    if (uninit) CoUninitialize();
  }
};

struct Bstr {
  BSTR s;
  explicit Bstr(const std::wstring& v) : s(SysAllocString(v.c_str())) {}
  ~Bstr() { SysFreeString(s); }
};

ITaskFolder* RootFolder() {
  ITaskService* svc = nullptr;
  if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&svc)))
    return nullptr;
  VARIANT empty;
  VariantInit(&empty);
  ITaskFolder* root = nullptr;
  if (SUCCEEDED(svc->Connect(empty, empty, empty, empty))) {
    Bstr path(L"\\");
    svc->GetFolder(path.s, &root);
  }
  svc->Release();
  return root;
}

std::wstring XmlEscape(const std::wstring& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c == L'&') out += L"&amp;";
    else if (c == L'<') out += L"&lt;";
    else if (c == L'>') out += L"&gt;";
    else out += c;
  }
  return out;
}

}  // namespace

bool AutoUpdateEnabled() { return GetFileAttributesW(OffMarker().c_str()) == INVALID_FILE_ATTRIBUTES; }

void SetAutoUpdate(bool on) {
  if (on) {
    DeleteFileW(OffMarker().c_str());
  } else {
    EnsureDir(ProgramDataDir());
    WriteFileAtomic(OffMarker(), "off", 3);
  }
}

int RunUpdate(bool force) {
  EnsureDir(ProgramDataDir());
  // one update run at a time (the task, a manual "--update")
  HANDLE once = CreateMutexW(nullptr, FALSE, L"Global\\C01UProStudio.Update");
  if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(once);
    return 0;
  }
  if (DeleteFileW(NowMarker().c_str())) force = true;  // "Check now" in the panel
  CleanupLeftovers();
  // installed by a version without automatic updates: from now on the task does this
  if (!UpdateTaskExists() && GetFileAttributesW(InstalledExePath().c_str()) != INVALID_FILE_ATTRIBUTES)
    RegisterUpdateTask(InstalledExePath());

  Json st = LoadState();
  const long long now = (long long)time(nullptr);
  int code = 0;
  do {
    if (!force && !AutoUpdateEnabled()) break;
    const bool pending = st["status"].asStr() == "pending";
    if (!force && !pending && now - (long long)st["checked"].asNum() < kCheckEverySec) break;

    SetStatus(st, "checking");
    const std::string base = "https://github.com/" MS_UPDATE_REPO "/releases/";
    const upd::HttpResult man = upd::HttpGet(base + "latest/download/update.txt", 64 * 1024, UserAgent());
    const upd::HttpResult sig = man.ok ? upd::HttpGet(base + "latest/download/update.txt.sig", 4096, UserAgent()) : man;
    if (!man.ok || !sig.ok) {
      SetStatus(st, "error", "network: " + (man.ok ? sig.error : man.error));
      code = 2;
      break;
    }
    upd::Manifest m;
    if (!upd::VerifyRelease(man.body, sig.body)) {
      SetStatus(st, "error", "signature");
      code = 3;
      break;
    }
    if (!upd::ParseManifest(man.body, &m) || m.name != kProductId) {
      SetStatus(st, "error", "format");
      code = 3;
      break;
    }
    st.set("checked", (double)now);
    st.set("latest", m.version);
    st.set("page", m.page);
    st.set("notesRu", m.notesRu);
    st.set("notesEn", m.notesEn);
    if (upd::CompareVersions(m.version, MS_VERSION_STR) <= 0) {
      st.erase("pendingSince");
      SetStatus(st, "current");
      break;
    }
    if (!st.has("pendingSince") || st["pendingVersion"].asStr() != m.version) {
      st.set("pendingSince", (double)now);
      st.set("pendingVersion", m.version);
    }
    if (!force && MicrophoneBusy() && now - (long long)st["pendingSince"].asNum() < kWaitForIdleSec) {
      SetStatus(st, "pending");
      break;
    }

    SetStatus(st, "downloading");
    const upd::HttpResult bin = upd::HttpGet(base + "download/v" + m.version + "/" + m.file, 64 << 20, UserAgent());
    uint8_t hash[32];
    if (!bin.ok) {
      SetStatus(st, "error", "download: " + bin.error);
      code = 2;
      break;
    }
    if (bin.body.size() != m.size || !upd::Sha256(bin.body.data(), bin.body.size(), hash) ||
        upd::ToHex(hash, 32) != m.sha256) {
      SetStatus(st, "error", "checksum");
      code = 3;
      break;
    }
    EnsureDir(UpdateDir());
    const std::wstring setup = UpdateDir() + L"\\" + FromUtf8(m.file);
    if (!WriteFileAtomic(setup, bin.body.data(), bin.body.size())) {
      SetStatus(st, "error", "write");
      code = 4;
      break;
    }

    SetStatus(st, "installing");
    const int rc = RunAndWait(setup, L"--install", 10 * 60 * 1000);
    DeleteFileW(setup.c_str());
    // 13: no Samson connected right now, 14: not attached to any endpoint; the files are new anyway
    if (rc == 0 || rc == 13 || rc == 14) {
      st.set("from", MS_VERSION_STR);
      st.set("updated", (double)time(nullptr));
      st.erase("pendingSince");
      SetStatus(st, "updated");
    } else {
      SetStatus(st, "error", "installer exit code " + std::to_string(rc));
      code = 5;
    }
  } while (false);

  if (once) CloseHandle(once);
  return code;
}

bool RegisterUpdateTask(const std::wstring& exe) {
  Com com;
  ITaskFolder* root = RootFolder();
  if (!root) return false;
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t start[32];
  swprintf_s(start, L"%04d-%02d-%02dT%02d:%02d:00", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  const std::wstring xml =
      L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
      L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
      L"<RegistrationInfo><Author>" MS_PRODUCT_NAME L"</Author><Description>Installs signed updates of " MS_PRODUCT_NAME
      L" from GitHub while the microphone is not in use.</Description></RegistrationInfo>"
      L"<Triggers>"
      L"<BootTrigger><Enabled>true</Enabled><Delay>PT10M</Delay></BootTrigger>"
      L"<TimeTrigger><Repetition><Interval>PT1H</Interval><StopAtDurationEnd>false</StopAtDurationEnd></Repetition>"
      L"<StartBoundary>" + std::wstring(start) + L"</StartBoundary><Enabled>true</Enabled><RandomDelay>PT10M</RandomDelay>"
      L"</TimeTrigger>"
      L"</Triggers>"
      L"<Principals><Principal id=\"System\"><UserId>S-1-5-18</UserId><RunLevel>HighestAvailable</RunLevel></Principal>"
      L"</Principals>"
      L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
      L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
      L"<AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable>"
      L"<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable><AllowStartOnDemand>true</AllowStartOnDemand>"
      L"<Enabled>true</Enabled><Hidden>false</Hidden><ExecutionTimeLimit>PT30M</ExecutionTimeLimit>"
      L"<Priority>7</Priority></Settings>"
      L"<Actions Context=\"System\"><Exec><Command>\"" + XmlEscape(exe) + L"\"</Command><Arguments>--update</Arguments>"
      L"</Exec></Actions>"
      L"</Task>";
  Bstr name(kTaskName), text(xml);
  VARIANT user, empty, sddl;
  VariantInit(&user);
  VariantInit(&empty);
  VariantInit(&sddl);
  user.vt = VT_BSTR;
  user.bstrVal = SysAllocString(L"SYSTEM");
  // admins and SYSTEM own it; any signed-in user may read and start it ("Check now")
  sddl.vt = VT_BSTR;
  sddl.bstrVal = SysAllocString(L"D:(A;;FA;;;BA)(A;;FA;;;SY)(A;;GRGX;;;AU)");
  IRegisteredTask* task = nullptr;
  const HRESULT hr = root->RegisterTask(name.s, text.s, TASK_CREATE_OR_UPDATE, user, empty, TASK_LOGON_SERVICE_ACCOUNT,
                                        sddl, &task);
  if (task) task->Release();
  VariantClear(&user);
  VariantClear(&sddl);
  root->Release();
  Log("update task registered: 0x%08lx", hr);
  return SUCCEEDED(hr);
}

void RemoveUpdateTask() {
  Com com;
  ITaskFolder* root = RootFolder();
  if (!root) return;
  Bstr name(kTaskName);
  root->DeleteTask(name.s, 0);
  root->Release();
}

bool UpdateTaskExists() {
  Com com;
  ITaskFolder* root = RootFolder();
  if (!root) return false;
  Bstr name(kTaskName);
  IRegisteredTask* task = nullptr;
  const bool ok = SUCCEEDED(root->GetTask(name.s, &task)) && task;
  if (task) task->Release();
  root->Release();
  return ok;
}

bool StartUpdateTask() {
  Com com;
  ITaskFolder* root = RootFolder();
  if (!root) return false;
  WriteFileAtomic(NowMarker(), "1", 1);
  Bstr name(kTaskName);
  IRegisteredTask* task = nullptr;
  bool ok = false;
  if (SUCCEEDED(root->GetTask(name.s, &task)) && task) {
    VARIANT empty;
    VariantInit(&empty);
    IRunningTask* running = nullptr;
    const HRESULT hr = task->Run(empty, &running);
    ok = SUCCEEDED(hr);
    if (running) running->Release();
    task->Release();
    Log("update task started: 0x%08lx", hr);
  }
  root->Release();
  if (!ok) DeleteFileW(NowMarker().c_str());
  return ok;
}

Json UpdateStateJson() {
  Json j = LoadState();
  j.set("auto", AutoUpdateEnabled());
  j.set("current", MS_VERSION_STR);
  j.set("repo", MS_UPDATE_REPO);
  return j;
}

}  // namespace app
