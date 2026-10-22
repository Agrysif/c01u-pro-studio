// Diagnostics: a list of checks, each with a status and (where possible) a one-click fix.
// Texts live in the web UI (i18n); here we only produce ids, statuses and numbers.
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <math.h>
#include <algorithm>

#include "common.h"
#include "controller.h"
#include "install.h"

namespace app {

namespace {

struct UsbInfo {
  bool present = false;
  ULONG problem = 0;
  std::wstring desc;
};

UsbInfo FindSamsonUsb() {
  UsbInfo info;
  HDEVINFO h = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (h == INVALID_HANDLE_VALUE) return info;
  SP_DEVINFO_DATA d = {sizeof(d)};
  for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); ++i) {
    wchar_t buf[2048] = {};
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, &type, (PBYTE)buf, sizeof(buf) - 4, &size)) continue;
    if (!ContainsI(buf, L"VID_17A0")) continue;
    ULONG status = 0, problem = 0;
    ULONG prob = 0;
    if (CM_Get_DevNode_Status(&status, &problem, d.DevInst, 0) == CR_SUCCESS && (status & DN_HAS_PROBLEM)) prob = problem;
    wchar_t desc[512] = {};
    SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_DEVICEDESC, &type, (PBYTE)desc, sizeof(desc) - 4, &size);
    if (!info.present || (info.problem && !prob)) {
      info.present = true;
      info.problem = prob;
      info.desc = desc;
    } else if (prob && !info.problem) {
      info.problem = prob;
    }
  }
  SetupDiDestroyDeviceInfoList(h);
  return info;
}

std::wstring Consent(HKEY root, const std::wstring& sub) {
  wchar_t v[64] = {};
  DWORD size = sizeof(v);
  if (RegGetValueW(root, sub.c_str(), L"Value", RRF_RT_REG_SZ, nullptr, v, &size) != ERROR_SUCCESS) return std::wstring();
  return v;
}

float R1(float v) { return (float)(lround(v * 10.0) / 10.0); }

}  // namespace

Json Controller::diagnostics() {
  Json checks = Json::Array();
  auto add = [&](const char* id, const char* status, Json data = Json::Object(), const char* fix = nullptr,
                 Json fixArg = Json()) {
    Json c = Json::Object();
    c.set("id", id);
    c.set("status", status);
    c.set("data", data);
    if (fix) {
      c.set("fix", fix);
      if (!fixArg.isNull()) c.set("fixArg", fixArg);
    }
    checks.push(c);
  };

  std::wstring configured;
  {
    std::lock_guard<std::mutex> lock(mu_);
    configured = settings_.micId;
  }

  // ---- the microphone itself
  const std::vector<DeviceInfo> all = ListDevices(eCapture, true);
  bool samsonActive = false, samsonInactive = false;
  std::wstring samsonName;
  for (const DeviceInfo& d : all) {
    if (!d.samson) continue;
    if (d.state == DEVICE_STATE_ACTIVE) {
      samsonActive = true;
      samsonName = d.name;
    } else if (d.state == DEVICE_STATE_DISABLED) {
      samsonInactive = true;
    }
  }
  const UsbInfo usb = FindSamsonUsb();
  {
    Json d = Json::Object();
    d.set("name", ToUtf8(samsonActive ? samsonName : usb.desc));
    if (usb.problem) {
      d.set("problem", (int)usb.problem);
      add("device", "fail", d, "openSound");
    } else if (samsonActive) {
      add("device", "ok", d);
    } else if (samsonInactive) {
      d.set("disabled", true);
      add("device", "fail", d, "openSound");
    } else if (usb.present) {
      add("device", "warn", d, "openSound");
    } else {
      add("device", "fail", d);
    }
  }

  DeviceInfo mic;
  const bool haveMic = ResolveMic(configured, &mic);
  {
    Json d = Json::Object();
    d.set("name", ToUtf8(mic.name));
    d.set("samson", mic.samson);
    add("selected", haveMic ? (mic.samson ? "ok" : "warn") : "fail", d);
  }
  if (haveMic) {
    Json d = Json::Object();
    d.set("console", mic.isDefault);
    d.set("comm", mic.isDefaultComm);
    if (mic.isDefault && mic.isDefaultComm)
      add("default", "ok", d);
    else
      add("default", "warn", d, "setDefault");
  }

  // ---- Windows privacy switches (Windows 10/11)
  if (IsWindows10OrLater()) {
    const std::wstring base = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";
    Json d = Json::Object();
    const bool sys = Consent(HKEY_LOCAL_MACHINE, base) == L"Deny";
    const bool user = Consent(HKEY_CURRENT_USER, base) == L"Deny";
    const bool desktop = Consent(HKEY_CURRENT_USER, base + L"\\NonPackaged") == L"Deny";
    d.set("scope", sys ? "system" : user ? "user" : desktop ? "desktop" : "");
    add("privacy", (sys || user || desktop) ? "fail" : "ok", d, (sys || user || desktop) ? "openPrivacy" : nullptr);
  }

  // ---- level and format in Windows
  EndpointDetails det;
  if (haveMic && GetEndpointDetails(mic.id, &det)) {
    Json d = Json::Object();
    d.set("volume", R1(det.volume * 100.f));
    d.set("rangeDb", R1(det.maxDb - det.minDb));
    if (det.mute)
      add("volume", "fail", d, "unmute");
    else if (det.volume < 0.5f)
      add("volume", "warn", d, "volume", Json(100));
    else
      add("volume", "ok", d);
    Json f = Json::Object();
    f.set("rate", det.rate);
    f.set("bits", det.bits);
    f.set("channels", det.channels);
    add("format", (det.rate == 44100 || det.rate == 48000) ? "ok" : "warn", f, (det.rate == 44100 || det.rate == 48000) ? nullptr : "openSound");
  }

  // ---- driver
  const DriverStatus ds = QueryDriverStatus();
  const std::wstring guid = haveMic ? EndpointGuid(mic.id) : std::wstring();
  const bool installed = ds.filesInstalled && ds.registered;
  {
    Json d = Json::Object();
    d.set("version", ToUtf8(ds.installedVersion));
    d.set("current", MS_VERSION_STR);
    const bool outdated = installed && ds.installedVersion != MS_VERSION_WSTR;
    add("driver", !installed ? "warn" : outdated ? "warn" : "ok", d, (!installed || outdated) ? "install" : nullptr);
  }
  if (ds.registered && !ds.protectedAudioOff) add("protected", "fail", Json::Object(), "install");
  const bool attached = haveMic && EndpointHasApo(guid);
  if (ds.registered && haveMic) add("attached", attached ? "ok" : "fail", Json::Object(), attached ? nullptr : "attach");
  if (haveMic) {
    bool off = false;
    if (!GetEnhancementsDisabled(mic.id, &off)) off = EndpointSysFxDisabled(guid);
    if (off) add("enhancements", "fail", Json::Object(), "enableSysfx");
  }
  if (haveMic) {
    const std::wstring foreign = EndpointForeignSfx(guid);
    if (!foreign.empty()) {
      Json d = Json::Object();
      d.set("clsid", ToUtf8(foreign));
      add("foreign", "warn", d);
    }
  }
  HKEY eq = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", 0, KEY_READ | KEY_WOW64_64KEY, &eq) == ERROR_SUCCESS) {
    RegCloseKey(eq);
    add("equalizerapo", "info");
  }

  // ---- live checks with the microphone open
  if (haveMic) {
    ++busy_;
    engine_.setDemand(true);
    for (int i = 0; i < 40 && !(engine_.running() && engine_.deviceOk()); ++i) Sleep(100);
    const bool capturing = engine_.running() && engine_.deviceOk();
    if (!capturing) {
      add("capture", "fail", Json::Object(), "openPrivacy");
    } else {
      Sleep(1200);  // give audiodg time to build the stream with our APO
      ms::Telemetry t;
      unsigned long long age = 0;
      const bool alive = link_.telemetry(&t, &age) && age < 1500;
      if (attached && installed) {
        Json d = Json::Object();
        d.set("streams", alive ? (int)t.activeStreams : 0);
        add("apo", alive ? "ok" : "fail", d, alive ? nullptr : "restartAudio");
        if (alive) {
          Json p = Json::Object();
          p.set("load", R1(t.dspLoad * 100.f));
          p.set("latencyMs", t.sampleRate > 0 ? R1(t.latencySamples * 1000.f / t.sampleRate) : 0.f);
          add("performance", "info", p);
        }
      }
      const bool bypass = alive && attached;
      if (bypass) {
        setTemporaryBypass(true);
        Sleep(250);
      }
      Measurement m;
      const bool ok = engine_.measure(3.0, &m);
      if (bypass) setTemporaryBypass(false);
      if (ok) {
        Json s = Json::Object();
        s.set("floor", R1(m.noiseFloorDb));
        s.set("peak", R1(m.peakDb));
        s.set("rms", R1(m.rmsDb));
        if (m.silent) {
          add("signal", "fail", s, "openPrivacy");
        } else {
          add("signal", "ok", s);
          const float f = m.noiseFloorDb;
          Json n = Json::Object();
          n.set("floor", R1(f));
          add("noise", f < -50.f ? "ok" : f < -40.f ? "warn" : "fail", n);
          if (m.clipped > 0) {
            Json c = Json::Object();
            c.set("count", m.clipped);
            add("clipping", "warn", c, "volume", Json(70));
          }
          if (fabsf(m.dcOffset) > 0.01f) {
            Json c = Json::Object();
            c.set("dc", R1(m.dcOffset * 1000.f) / 10.f);
            add("dc", "warn", c);
          }
          const float hum = std::max(m.hum50Db, m.hum60Db);
          if (hum > 12.f) {
            Json h = Json::Object();
            const int hz = m.hum60Db > m.hum50Db ? 60 : 50;
            h.set("hz", hz);
            h.set("db", R1(hum));
            add("hum", "warn", h, "hum", Json(hz));
          }
        }
        Json g = Json::Object();
        g.set("count", m.glitches);
        add("glitches", m.glitches > 0 ? "warn" : "ok", g);
      }
    }
    --busy_;
  }

  {
    Json d = Json::Object();
    d.set("on", GetAutostart());
    add("autostart", "info", d);
    Json o = Json::Object();
    o.set("os", WindowsVersionString());
    o.set("version", MS_VERSION_STR);
    add("system", "info", o);
  }

  Json out = Json::Object();
  out.set("ok", true);
  out.set("checks", checks);
  int fails = 0, warns = 0;
  for (size_t i = 0; i < checks.size(); ++i) {
    const std::string& st = checks.at(i)["status"].asStr();
    fails += st == "fail";
    warns += st == "warn";
  }
  out.set("fails", fails);
  out.set("warns", warns);
  return out;
}

}  // namespace app
