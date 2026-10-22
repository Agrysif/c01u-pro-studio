#include "audio.h"

#include <audioclient.h>
#include <avrt.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <math.h>
#include <propvarutil.h>

#include <algorithm>
#include <chrono>

#include "common.h"
#include "policyconfig.h"

namespace app {

namespace {

const CLSID kClsidEnumerator = __uuidof(MMDeviceEnumerator);
// {b3f8fa53-0004-438e-9003-51a46e139bfc},2 : "{1}.USB\VID_17A0&PID_0303&MI_00\..."
const PROPERTYKEY kPkeyEndpointHwid = {{0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}}, 2};
// PKEY_AudioEngine_DeviceFormat
const PROPERTYKEY kPkeyDeviceFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};

template <class T>
struct ComPtr {
  T* p = nullptr;
  ~ComPtr() {
    if (p) p->Release();
  }
  T** operator&() { return &p; }
  T* operator->() const { return p; }
  operator T*() const { return p; }
  void reset() {
    if (p) p->Release();
    p = nullptr;
  }
};

struct ComScope {
  HRESULT hr;
  ComScope() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
  ~ComScope() {
    if (SUCCEEDED(hr)) CoUninitialize();
  }
};

bool GetEnumerator(IMMDeviceEnumerator** out) {
  return SUCCEEDED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)out));
}

std::wstring PropString(IPropertyStore* store, const PROPERTYKEY& key) {
  PROPVARIANT v;
  PropVariantInit(&v);
  std::wstring out;
  if (SUCCEEDED(store->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal) out = v.pwszVal;
  PropVariantClear(&v);
  return out;
}

std::wstring DefaultId(IMMDeviceEnumerator* en, EDataFlow flow, ERole role) {
  ComPtr<IMMDevice> dev;
  std::wstring out;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, role, &dev))) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(dev->GetId(&id)) && id) out = id;
    if (id) CoTaskMemFree(id);
  }
  return out;
}

bool OpenDevice(const std::wstring& id, IMMDevice** out) {
  ComPtr<IMMDeviceEnumerator> en;
  if (!GetEnumerator(&en)) return false;
  return SUCCEEDED(en->GetDevice(id.c_str(), out));
}

float ToDb(float lin) { return lin > 1e-6f ? 20.f * log10f(lin) : -120.f; }
float PowDb(double p) { return p > 1e-12 ? (float)(10.0 * log10(p)) : -120.f; }

}  // namespace

// ------------------------------------------------------------------ devices
std::vector<DeviceInfo> ListDevices(EDataFlow flow, bool includeInactive) {
  ComScope com;
  std::vector<DeviceInfo> out;
  ComPtr<IMMDeviceEnumerator> en;
  if (!GetEnumerator(&en)) return out;
  const std::wstring defConsole = DefaultId(en, flow, eConsole);
  const std::wstring defComm = DefaultId(en, flow, eCommunications);
  ComPtr<IMMDeviceCollection> col;
  DWORD mask = includeInactive ? (DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED) : DEVICE_STATE_ACTIVE;
  if (FAILED(en->EnumAudioEndpoints(flow, mask, &col))) return out;
  UINT count = 0;
  col->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> dev;
    if (FAILED(col->Item(i, &dev))) continue;
    DeviceInfo d;
    LPWSTR id = nullptr;
    if (FAILED(dev->GetId(&id)) || !id) continue;
    d.id = id;
    CoTaskMemFree(id);
    dev->GetState(&d.state);
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store))) {
      d.name = PropString(store, PKEY_Device_FriendlyName);
      d.hwid = PropString(store, kPkeyEndpointHwid);
    }
    const std::wstring hw = LowerW(d.hwid);
    d.samson = hw.find(L"vid_17a0") != std::wstring::npos || ContainsI(d.name, L"samson");
    d.c01uPro = (hw.find(L"vid_17a0") != std::wstring::npos && hw.find(L"pid_0303") != std::wstring::npos) ||
                ContainsI(d.name, L"c01u");
    d.isDefault = d.id == defConsole;
    d.isDefaultComm = d.id == defComm;
    out.push_back(d);
  }
  return out;
}

bool FindDevice(const std::wstring& id, DeviceInfo* out) {
  for (const DeviceInfo& d : ListDevices(eCapture, true))
    if (d.id == id) {
      *out = d;
      return true;
    }
  for (const DeviceInfo& d : ListDevices(eRender, true))
    if (d.id == id) {
      *out = d;
      return true;
    }
  return false;
}

bool ResolveMic(const std::wstring& configuredId, DeviceInfo* out) {
  std::vector<DeviceInfo> mics = ListDevices(eCapture, false);
  if (!configuredId.empty())
    for (const DeviceInfo& d : mics)
      if (d.id == configuredId) {
        *out = d;
        return true;
      }
  for (const DeviceInfo& d : mics)
    if (d.c01uPro) {
      *out = d;
      return true;
    }
  for (const DeviceInfo& d : mics)
    if (d.samson) {
      *out = d;
      return true;
    }
  for (const DeviceInfo& d : mics)
    if (d.isDefault) {
      *out = d;
      return true;
    }
  if (!mics.empty()) {
    *out = mics[0];
    return true;
  }
  return false;
}

std::wstring EndpointGuid(const std::wstring& id) {
  size_t dot = id.rfind(L"}.{");
  if (dot != std::wstring::npos) return id.substr(dot + 2);
  return id;
}

bool GetEndpointDetails(const std::wstring& id, EndpointDetails* out) {
  ComScope com;
  *out = EndpointDetails();
  ComPtr<IMMDevice> dev;
  if (!OpenDevice(id, &dev)) return false;
  ComPtr<IAudioEndpointVolume> vol;
  if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&vol))) {
    vol->GetMasterVolumeLevelScalar(&out->volume);
    BOOL m = FALSE;
    vol->GetMute(&m);
    out->mute = m != FALSE;
    float step = 0;
    vol->GetVolumeRange(&out->minDb, &out->maxDb, &step);
  }
  ComPtr<IPropertyStore> store;
  if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store))) {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(kPkeyDeviceFormat, &v)) && v.vt == VT_BLOB && v.blob.cbSize >= sizeof(WAVEFORMATEX)) {
      const WAVEFORMATEX* w = (const WAVEFORMATEX*)v.blob.pBlobData;
      out->rate = (int)w->nSamplesPerSec;
      out->bits = w->wBitsPerSample;
      out->channels = w->nChannels;
    }
    PropVariantClear(&v);
  }
  out->ok = true;
  return true;
}

bool SetEndpointVolume(const std::wstring& id, float scalar) {
  ComScope com;
  ComPtr<IMMDevice> dev;
  if (!OpenDevice(id, &dev)) return false;
  ComPtr<IAudioEndpointVolume> vol;
  if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&vol))) return false;
  return SUCCEEDED(vol->SetMasterVolumeLevelScalar(std::min(1.f, std::max(0.f, scalar)), nullptr));
}

bool SetEndpointMute(const std::wstring& id, bool mute) {
  ComScope com;
  ComPtr<IMMDevice> dev;
  if (!OpenDevice(id, &dev)) return false;
  ComPtr<IAudioEndpointVolume> vol;
  if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&vol))) return false;
  return SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr));
}

bool SetDefaultDevice(const std::wstring& id) {
  ComScope com;
  ComPtr<IPolicyConfig> pc;
  if (FAILED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void**)&pc)))
    return false;
  bool ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eConsole));
  ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eMultimedia)) && ok;
  ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eCommunications)) && ok;
  return ok;
}

// PKEY_AudioEndpoint_Disable_SysFx
static const PROPERTYKEY kPkeyDisableSysFx = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 5};

bool GetEnhancementsDisabled(const std::wstring& id, bool* disabled) {
  ComScope com;
  ComPtr<IPolicyConfig> pc;
  if (FAILED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void**)&pc)))
    return false;
  PROPVARIANT v;
  PropVariantInit(&v);
  if (FAILED(pc->GetPropertyValue(id.c_str(), TRUE, kPkeyDisableSysFx, &v))) return false;
  *disabled = (v.vt == VT_UI4 && v.ulVal != 0) || (v.vt == VT_BOOL && v.boolVal != VARIANT_FALSE);
  PropVariantClear(&v);
  return true;
}

bool SetEnhancementsEnabled(const std::wstring& id) {
  ComScope com;
  ComPtr<IPolicyConfig> pc;
  if (FAILED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void**)&pc)))
    return false;
  PROPVARIANT v;
  PropVariantInit(&v);
  v.vt = VT_UI4;
  v.ulVal = 0;
  const HRESULT hr = pc->SetPropertyValue(id.c_str(), TRUE, kPkeyDisableSysFx, &v);
  Log("enable enhancements on %ls -> 0x%08lx", id.c_str(), hr);
  return SUCCEEDED(hr);
}

// ------------------------------------------------------------------ ring
void SpscRing::push(const float* x, size_t n) {
  size_t w = w_.load(std::memory_order_relaxed);
  const size_t r = r_.load(std::memory_order_acquire);
  if (w - r + n > buf_.size()) return;  // consumer stalled: drop
  for (size_t i = 0; i < n; ++i) buf_[(w + i) & mask_] = x[i];
  w_.store(w + n, std::memory_order_release);
}

bool SpscRing::pop(float* v) {
  const size_t r = r_.load(std::memory_order_relaxed);
  if (r == w_.load(std::memory_order_acquire)) return false;
  *v = buf_[r & mask_];
  r_.store(r + 1, std::memory_order_release);
  return true;
}

void SpscRing::skip(size_t n) {
  const size_t r = r_.load(std::memory_order_relaxed);
  const size_t avail = w_.load(std::memory_order_acquire) - r;
  r_.store(r + std::min(n, avail), std::memory_order_release);
}

// ------------------------------------------------------------------ render sink
void RenderSink::start(const std::wstring& deviceId, int srcRate) {
  if (thread_.joinable()) {
    if (deviceId == device_ && srcRate == srcRate_) return;
    stop();
  }
  device_ = deviceId;
  srcRate_ = srcRate;
  ring_.clear();
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  thread_ = std::thread(&RenderSink::run, this);
}

void RenderSink::stop() {
  if (!thread_.joinable()) return;
  SetEvent(stop_);
  thread_.join();
  CloseHandle(stop_);
  stop_ = nullptr;
  ok_ = false;
}

void RenderSink::run() {
  ComScope com;
  ComPtr<IMMDevice> dev;
  if (device_.empty()) {
    ComPtr<IMMDeviceEnumerator> en;
    if (!GetEnumerator(&en) || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return;
  } else if (!OpenDevice(device_, &dev)) {
    Log("render sink: device not found");
    return;
  }
  ComPtr<IAudioClient> client;
  if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) return;
  WAVEFORMATEX* wfx = nullptr;
  if (FAILED(client->GetMixFormat(&wfx)) || !wfx) return;
  const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)wfx;
  const bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                       (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  const int ch = wfx->nChannels;
  const int bits = wfx->wBitsPerSample;
  const int dstRate = (int)wfx->nSamplesPerSec;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 300000, 0, wfx, nullptr);
  CoTaskMemFree(wfx);
  ComPtr<IAudioRenderClient> render;
  UINT32 bufFrames = 0;
  if (FAILED(hr) || FAILED(client->SetEventHandle(ev)) || FAILED(client->GetBufferSize(&bufFrames)) ||
      FAILED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) {
    Log("render sink: init failed 0x%08lx", hr);
    CloseHandle(ev);
    return;
  }
  DWORD task = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task);
  client->Start();
  ok_ = true;
  const double baseStep = (double)srcRate_ / dstRate;
  const double target = srcRate_ * 0.035;  // keep ~35 ms queued
  double frac = 0;
  float cur = 0, next = 0;
  bool primed = false;
  HANDLE hs[2] = {stop_, ev};
  for (;;) {
    DWORD w = WaitForMultipleObjects(2, hs, FALSE, 200);
    if (w == WAIT_OBJECT_0) break;
    UINT32 pad = 0;
    if (FAILED(client->GetCurrentPadding(&pad))) break;
    const UINT32 avail = bufFrames - pad;
    if (!avail) continue;
    BYTE* data = nullptr;
    if (FAILED(render->GetBuffer(avail, &data))) break;
    const double fill = (double)ring_.size();
    if (!primed && fill >= target) primed = true;
    if (fill > srcRate_ * 0.25) {  // fell far behind: jump close to the target
      ring_.skip((size_t)(fill - target));
    }
    const double step = baseStep * (1.0 + std::max(-0.01, std::min(0.01, (fill - target) / srcRate_ * 0.2)));
    const float vol = volume_.load();
    for (UINT32 i = 0; i < avail; ++i) {
      float s = 0.f;
      if (primed) {
        s = cur + (next - cur) * (float)frac;
        frac += step;
        while (frac >= 1.0) {
          frac -= 1.0;
          cur = next;
          if (!ring_.pop(&next)) {
            primed = false;  // underrun: wait for the queue to refill
            break;
          }
        }
      }
      s *= vol;
      if (isFloat && bits == 32) {
        float* o = (float*)data + (size_t)i * ch;
        for (int c = 0; c < ch; ++c) o[c] = s;
      } else if (bits == 16) {
        int16_t* o = (int16_t*)data + (size_t)i * ch;
        const int16_t v = (int16_t)std::max(-32768.f, std::min(32767.f, s * 32767.f));
        for (int c = 0; c < ch; ++c) o[c] = v;
      } else if (bits == 32) {
        int32_t* o = (int32_t*)data + (size_t)i * ch;
        const int32_t v = (int32_t)std::max(-2147483648.0, std::min(2147483647.0, (double)s * 2147483647.0));
        for (int c = 0; c < ch; ++c) o[c] = v;
      } else {
        memset(data + (size_t)i * (bits / 8) * ch, 0, (size_t)(bits / 8) * ch);
      }
    }
    render->ReleaseBuffer(avail, 0);
  }
  client->Stop();
  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
  CloseHandle(ev);
  ok_ = false;
}

// ------------------------------------------------------------------ engine
AudioEngine::AudioEngine() {
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ms::DefaultParams(&pendingParams_);
}

AudioEngine::~AudioEngine() {
  want_ = false;
  SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
  monitor_.stop();
  route_.stop();
  CloseHandle(stopEvent_);
}

void AudioEngine::setDemand(bool want) {
  want_ = want;
  if (want) lastWant_ = GetTickCount64();
  tickDemand();
}

void AudioEngine::tickDemand() {
  static unsigned long long lastStart = 0;
  if (!want_ || running_) return;
  const unsigned long long now = GetTickCount64();
  if (now - lastStart < 2000) return;
  lastStart = now;
  if (thread_.joinable()) thread_.join();
  ResetEvent(stopEvent_);
  running_ = true;
  thread_ = std::thread(&AudioEngine::run, this);
}

void AudioEngine::setDevice(const std::wstring& id) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (id == configuredId_) return;
    configuredId_ = id;
  }
  if (running_) SetEvent(stopEvent_);  // the next tick restarts on the new device
}

void AudioEngine::setParams(const ms::Params& p) {
  std::lock_guard<std::mutex> lock(mu_);
  pendingParams_ = p;
  paramsDirty_ = true;
}

void AudioEngine::setAppProcessing(bool on) { appProcessing_ = on; }
void AudioEngine::relearnNoise() { relearn_ = true; }

void AudioEngine::setMonitor(bool on, const std::wstring& device, float volume, bool raw) {
  std::lock_guard<std::mutex> lock(mu_);
  monitorOn_ = on;
  monitorDev_ = device;
  monitorVolume_ = volume;
  monitorRaw_ = raw;
  monitor_.setVolume(volume);
  if (running_ && rate_ > 0) applySinks();
}

void AudioEngine::setRoute(bool on, const std::wstring& device) {
  std::lock_guard<std::mutex> lock(mu_);
  routeOn_ = on;
  routeDev_ = device;
  if (running_ && rate_ > 0) applySinks();
}

// mu_ held
void AudioEngine::applySinks() {
  const int rate = rate_;
  if (monitorOn_ && rate > 0)
    monitor_.start(monitorDev_, rate);
  else
    monitor_.stop();
  monitor_.setVolume(monitorVolume_);
  if (routeOn_ && rate > 0 && !routeDev_.empty())
    route_.start(routeDev_, rate);
  else
    route_.stop();
}

std::wstring AudioEngine::deviceId() {
  std::lock_guard<std::mutex> lock(mu_);
  return currentId_;
}

std::wstring AudioEngine::deviceName() {
  std::lock_guard<std::mutex> lock(mu_);
  return currentName_;
}

void AudioEngine::snapshot(Meters* m) {
  std::lock_guard<std::mutex> lock(mu_);
  *m = meters_;
  m->running = running_;
}

void AudioEngine::run() {
  ComScope com;
  std::wstring configured;
  {
    std::lock_guard<std::mutex> lock(mu_);
    configured = configuredId_;
  }
  DeviceInfo dev;
  if (!ResolveMic(configured, &dev)) {
    deviceOk_ = false;
    running_ = false;
    return;
  }
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioCaptureClient> cap;
  WAVEFORMATEX* wfx = nullptr;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HRESULT hr = E_FAIL;
  if (OpenDevice(dev.id, &device) &&
      SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
      SUCCEEDED(client->GetMixFormat(&wfx)) && wfx) {
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, wfx, nullptr);
    if (SUCCEEDED(hr)) hr = client->SetEventHandle(ev);
    if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
  }
  if (FAILED(hr) || !wfx) {
    Log("capture init failed 0x%08lx", hr);
    if (wfx) CoTaskMemFree(wfx);
    CloseHandle(ev);
    deviceOk_ = false;
    running_ = false;
    return;
  }
  const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)wfx;
  const bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                       (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  const int ch = wfx->nChannels, bits = wfx->wBitsPerSample, rate = (int)wfx->nSamplesPerSec;
  CoTaskMemFree(wfx);

  {
    std::lock_guard<std::mutex> lock(mu_);
    currentId_ = dev.id;
    currentName_ = dev.name;
    rate_ = rate;
    chain_.prepare(rate);
    chain_.setParams(pendingParams_);
    paramsDirty_ = false;
    post_.assign(rate, 0.f);
    specFft_.init(2048);
    specRing_.assign(4096, 0.f);
    specRe_.assign(2048, 0.f);
    specIm_.assign(2048, 0.f);
    specWin_.resize(2048);
    for (int i = 0; i < 2048; ++i) specWin_[i] = (float)(0.5 - 0.5 * cos(2 * ms::dsp::kPi * i / 2048));
    specPos_ = 0;
    specCountdown_ = rate / 25;
    kShelf_.design(ms::dsp::Biquad::kHighShelf, rate, 1681.97, 3.9998, 0.7071752);
    kHp_.design(ms::dsp::Biquad::kHighPass, rate, 38.13547, 0.0, 0.5003270);
    lufsHist_.assign(30, 0.0);
    lufsPos_ = 0;
    lufsBlock_ = 0;
    lufsN_ = 0;
    pitchBuf_.assign(2048, 0.f);
    pitchCountdown_ = rate / 20;
    meters_ = Meters();
    acc_n_ = 0;
    applySinks();
  }
  DWORD task = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task);
  client->Start();
  deviceOk_ = true;
  Log("capture started: %ls, %d Hz, %d ch, %d bit%s", dev.name.c_str(), rate, ch, bits, isFloat ? " float" : "");

  std::vector<float> mono(4096);
  HANDLE hs[2] = {stopEvent_, ev};
  bool lost = false;
  for (;;) {
    DWORD w = WaitForMultipleObjects(2, hs, FALSE, 200);
    if (w == WAIT_OBJECT_0) break;
    if (!want_ && GetTickCount64() - lastWant_ > 3000) break;
    UINT32 packet = 0;
    if (FAILED(cap->GetNextPacketSize(&packet))) {
      lost = true;
      break;
    }
    while (packet) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
        lost = true;
        break;
      }
      if (mono.size() < frames) mono.resize(frames);
      if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
        std::fill(mono.begin(), mono.begin() + frames, 0.f);
      } else {
        const float inv = 1.f / ch;
        for (UINT32 i = 0; i < frames; ++i) {
          float s = 0;
          for (int c = 0; c < ch; ++c) {
            const size_t k = (size_t)i * ch + c;
            if (isFloat && bits == 32) s += ((const float*)data)[k];
            else if (bits == 16) s += ((const int16_t*)data)[k] / 32768.f;
            else if (bits == 32) s += ((const int32_t*)data)[k] / 2147483648.f;
            else if (bits == 24) {
              const BYTE* b = data + k * 3;
              s += ((int32_t)((b[0] << 8) | (b[1] << 16) | (b[2] << 24)) >> 8) / 8388608.f;
            }
          }
          mono[i] = s * inv;
        }
      }
      cap->ReleaseBuffer(frames);
      onBlock(mono.data(), (int)frames, (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
      if (FAILED(cap->GetNextPacketSize(&packet))) {
        lost = true;
        break;
      }
    }
    if (lost) break;
  }
  client->Stop();
  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
  CloseHandle(ev);
  {
    std::lock_guard<std::mutex> lock(mu_);
    monitor_.stop();
    route_.stop();
    meters_.running = false;
    if (measActive_) {
      measActive_ = false;
      measResult_.ok = false;
      measCv_.notify_all();
    }
    if (recState_ == 1 || recState_ == 2) recState_ = recRaw_.empty() ? 0 : 3;
  }
  if (lost) {
    Log("capture device lost");
    deviceOk_ = false;
  }
  running_ = false;
}

void AudioEngine::onBlock(const float* raw, int n, bool discontinuity) {
  if (n <= 0) return;
  if (discontinuity) ++glitches_;
  if (paramsDirty_) {
    std::lock_guard<std::mutex> lock(mu_);
    chain_.setParams(pendingParams_);
    paramsDirty_ = false;
  }
  if (relearn_.exchange(false)) chain_.relearnNoise();
  if ((int)post_.size() < n) post_.resize(n);
  float* post = post_.data();
  memcpy(post, raw, n * sizeof(float));
  const bool proc = appProcessing_;
  if (proc) chain_.process(post, n);

  for (int i = 0; i < n; ++i) {
    const float a = fabsf(raw[i]), b = fabsf(post[i]);
    if (a > acc_inPeak_) acc_inPeak_ = a;
    if (b > acc_outPeak_) acc_outPeak_ = b;
    if (a >= 0.999f) acc_clip_ = true;
    acc_inSq_ += (double)raw[i] * raw[i];
    acc_outSq_ += (double)post[i] * post[i];
  }
  acc_n_ += n;
  analyse(post, n);

  std::lock_guard<std::mutex> lock(mu_);
  if (acc_n_ >= rate_ / 25) {
    meters_.seq++;
    meters_.inPeakDb = ToDb(acc_inPeak_);
    meters_.outPeakDb = ToDb(acc_outPeak_);
    meters_.inRmsDb = PowDb(acc_inSq_ / acc_n_);
    meters_.outRmsDb = PowDb(acc_outSq_ / acc_n_);
    meters_.clip = acc_clip_;
    meters_.lufsM = lufsM_;
    meters_.lufsS = lufsS_;
    meters_.pitchHz = pitch_;
    meters_.chainValid = proc;
    meters_.chain = chain_.stats();
    acc_inPeak_ = acc_outPeak_ = 0;
    acc_inSq_ = acc_outSq_ = 0;
    acc_n_ = 0;
    acc_clip_ = false;
  }
  if (monitorOn_) monitor_.push(monitorRaw_ ? raw : post, n);
  if (routeOn_) route_.push(post, n);

  // recording
  if (recState_ == 1) {
    if (recWarmup_ > (size_t)n)
      recWarmup_ -= n;
    else {
      recWarmup_ = 0;
      recState_ = 2;
    }
  } else if (recState_ == 2) {
    const size_t take = std::min((size_t)n, recTarget_ - recRaw_.size());
    recRaw_.insert(recRaw_.end(), raw, raw + take);
    recPost_.insert(recPost_.end(), post, post + take);
    if (recRaw_.size() >= recTarget_) recState_ = 3;
  }

  // measurement on the raw signal
  if (measActive_) {
    const int block = std::max(1, rate_.load() / 20);
    for (int i = 0; i < n && measActive_; ++i) {
      const float x = raw[i];
      measSum_ += x;
      measSq_ += (double)x * x;
      const float a = fabsf(x);
      if (a > measPeak_) measPeak_ = a;
      if (a >= 0.999f) ++measClip_;
      if (x != 0.f) measNonZero_ = true;
      const float k = measKShelf_.process(measKHp_.process(x));
      measBlockSq_ += (double)x * x;
      measBlockK_ += (double)k * k;
      if (++measBlockN_ >= block) {
        measBlocks_.push_back(PowDb(measBlockSq_ / measBlockN_));
        measLoud_.push_back(PowDb(measBlockK_ / measBlockN_));
        measBlockSq_ = measBlockK_ = 0;
        measBlockN_ = 0;
        if (measBlocks_.back() > -50.f && pitchBuf_.size() >= 2048) {
          float f = estimatePitch(pitchBuf_.data(), (int)pitchBuf_.size(), rate_);
          if (f > 0) measPitches_.push_back(f);
        }
      }
      measFftBuf_[measFftPos_++] = x;
      if (measFftPos_ == measFftBuf_.size()) {
        const size_t N = measFftBuf_.size();
        std::vector<float> re(N), im(N, 0.f);
        for (size_t j = 0; j < N; ++j) re[j] = measFftBuf_[j] * (float)(0.5 - 0.5 * cos(2 * ms::dsp::kPi * j / N));
        measFft_.transform(re.data(), im.data(), false);
        for (size_t j = 0; j <= N / 2; ++j) measPow_[j] += (double)re[j] * re[j] + (double)im[j] * im[j];
        ++measFrames_;
        memmove(measFftBuf_.data(), measFftBuf_.data() + N / 2, sizeof(float) * (N / 2));
        measFftPos_ = N / 2;
      }
      if (++measCount_ >= measTarget_) finishMeasurement();
    }
  }
}

void AudioEngine::analyse(const float* post, int n) {
  const int rate = rate_;
  for (int i = 0; i < n; ++i) {
    const float x = post[i];
    specRing_[specPos_] = x;
    specPos_ = (specPos_ + 1) & 4095;
    // loudness (K-weighted, 100 ms blocks)
    const float k = kShelf_.process(kHp_.process(x));
    lufsBlock_ += (double)k * k;
    if (++lufsN_ >= rate / 10) {
      lufsHist_[lufsPos_] = lufsBlock_ / lufsN_;
      lufsPos_ = (lufsPos_ + 1) % lufsHist_.size();
      lufsBlock_ = 0;
      lufsN_ = 0;
      double m = 0, s = 0;
      for (double v : lufsHist_) s += v;
      for (int j = 1; j <= 4; ++j) m += lufsHist_[(lufsPos_ + lufsHist_.size() - j) % lufsHist_.size()];
      lufsM_ = std::max(-70.f, -0.691f + PowDb(m / 4));
      lufsS_ = std::max(-70.f, -0.691f + PowDb(s / lufsHist_.size()));
    }
  }
  // pitch buffer: keep the newest 2048 samples
  if (n >= 2048) {
    memcpy(pitchBuf_.data(), post + n - 2048, 2048 * sizeof(float));
  } else {
    memmove(pitchBuf_.data(), pitchBuf_.data() + n, (2048 - n) * sizeof(float));
    memcpy(pitchBuf_.data() + 2048 - n, post, n * sizeof(float));
  }
  pitchCountdown_ -= n;
  if (pitchCountdown_ <= 0) {
    pitchCountdown_ += rate / 20;
    double sq = 0;
    for (float v : pitchBuf_) sq += (double)v * v;
    float f = 0;
    if (PowDb(sq / pitchBuf_.size()) > -50.f) f = estimatePitch(pitchBuf_.data(), 2048, rate);
    if (f > 0)
      pitch_ = pitch_ > 0 ? pitch_ + (f - pitch_) * 0.5f : f;
    else
      pitch_ = 0;
  }
  // spectrum every 40 ms
  specCountdown_ -= n;
  if (specCountdown_ > 0) return;
  specCountdown_ += rate / 25;
  for (int i = 0; i < 2048; ++i) {
    specRe_[i] = specRing_[(specPos_ + 4096 - 2048 + i) & 4095] * specWin_[i];
    specIm_[i] = 0.f;
  }
  specFft_.transform(specRe_.data(), specIm_.data(), false);
  const double norm = 1.0 / (512.0 * 512.0);  // (N/4)^2: full-scale sine -> 0 dB
  const double binHz = (double)rate / 2048;
  const double fLo = 30.0, fHi = std::min(20000.0, rate * 0.5);
  float bands[kSpectrumBands];
  for (int b = 0; b < kSpectrumBands; ++b) {
    const double f0 = fLo * pow(fHi / fLo, (double)b / kSpectrumBands);
    const double f1 = fLo * pow(fHi / fLo, (double)(b + 1) / kSpectrumBands);
    int k0 = (int)floor(f0 / binHz), k1 = (int)ceil(f1 / binHz);
    k0 = std::max(1, std::min(1023, k0));
    k1 = std::max(k0 + 1, std::min(1024, k1));
    double p = 0;
    for (int k = k0; k < k1; ++k) p = std::max(p, (double)specRe_[k] * specRe_[k] + (double)specIm_[k] * specIm_[k]);
    bands[b] = std::max(-120.f, PowDb(p * norm));
  }
  std::lock_guard<std::mutex> lock(mu_);
  memcpy(meters_.spectrum, bands, sizeof(bands));
}

// YIN on a 12 kHz decimated copy; returns 0 when no clear pitch is found.
float AudioEngine::estimatePitch(const float* x, int n, int rate) {
  const int dec = std::max(1, rate / 12000);
  const float fs = (float)rate / dec;
  float y[2048];
  int m = 0;
  for (int i = 0; i + dec <= n && m < 2048; i += dec) {
    float s = 0;
    for (int j = 0; j < dec; ++j) s += x[i + j];
    y[m++] = s / dec;
  }
  const int tauMin = std::max(2, (int)(fs / 500.f));
  const int tauMax = std::min(m / 2 - 1, (int)(fs / 60.f));
  const int W = m - tauMax - 1;
  if (W < 64 || tauMax <= tauMin) return 0.f;
  float d[1024];
  float run = 0;
  d[0] = 1.f;
  for (int tau = 1; tau <= tauMax; ++tau) {
    float s = 0;
    for (int j = 0; j < W; ++j) {
      const float v = y[j] - y[j + tau];
      s += v * v;
    }
    run += s;
    d[tau] = run > 0 ? s * tau / run : 1.f;
  }
  for (int tau = tauMin; tau < tauMax; ++tau) {
    if (d[tau] < 0.15f) {
      while (tau + 1 < tauMax && d[tau + 1] < d[tau]) ++tau;
      const float a = d[tau - 1], b = d[tau], c = d[tau + 1];
      const float den = a - 2 * b + c;
      const float shift = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.f;
      return fs / (tau + std::max(-1.f, std::min(1.f, shift)));
    }
  }
  return 0.f;
}

// ------------------------------------------------------------------ recording
bool AudioEngine::startRecording(double seconds, double warmupSeconds) {
  std::lock_guard<std::mutex> lock(mu_);
  if (recState_ == 1 || recState_ == 2) return false;
  const int rate = rate_ > 0 ? rate_.load() : 48000;
  seconds = std::max(1.0, std::min(30.0, seconds));
  recTarget_ = (size_t)(seconds * rate);
  recWarmup_ = (size_t)(warmupSeconds * rate);
  recRaw_.clear();
  recPost_.clear();
  recRaw_.reserve(recTarget_);
  recPost_.reserve(recTarget_);
  recState_ = 1;
  return true;
}

void AudioEngine::stopRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  if (recState_ == 1) recState_ = 0;
  if (recState_ == 2) recState_ = recRaw_.empty() ? 0 : 3;
}

int AudioEngine::recordState(double* pos, double* len) {
  std::lock_guard<std::mutex> lock(mu_);
  const double rate = rate_ > 0 ? rate_.load() : 48000;
  if (pos) *pos = recRaw_.size() / rate;
  if (len) *len = recTarget_ / rate;
  return recState_;
}

static void AppendWav(std::string* out, const std::vector<float>& x, int rate) {
  const uint32_t dataBytes = (uint32_t)x.size() * 2;
  out->resize(44 + dataBytes);
  char* p = &(*out)[0];
  auto u32 = [&](size_t off, uint32_t v) { memcpy(p + off, &v, 4); };
  auto u16 = [&](size_t off, uint16_t v) { memcpy(p + off, &v, 2); };
  memcpy(p, "RIFF", 4);
  u32(4, 36 + dataBytes);
  memcpy(p + 8, "WAVEfmt ", 8);
  u32(16, 16);
  u16(20, 1);
  u16(22, 1);
  u32(24, (uint32_t)rate);
  u32(28, (uint32_t)rate * 2);
  u16(32, 2);
  u16(34, 16);
  memcpy(p + 36, "data", 4);
  u32(40, dataBytes);
  int16_t* s = (int16_t*)(p + 44);
  for (size_t i = 0; i < x.size(); ++i) s[i] = (int16_t)lrintf(std::max(-1.f, std::min(1.f, x[i])) * 32767.f);
}

bool AudioEngine::recordingWav(bool processed, std::string* wav) {
  std::lock_guard<std::mutex> lock(mu_);
  if (recState_ != 3) return false;
  AppendWav(wav, processed ? recPost_ : recRaw_, rate_ > 0 ? rate_.load() : 48000);
  return true;
}

// ------------------------------------------------------------------ measurement
bool AudioEngine::measure(double seconds, Measurement* out) {
  *out = Measurement();
  // wait for the capture to be running
  for (int i = 0; i < 40 && !(running_ && deviceOk_ && rate_ > 0); ++i) {
    setDemand(true);
    Sleep(100);
  }
  if (!(running_ && rate_ > 0)) return false;
  std::unique_lock<std::mutex> lock(mu_);
  if (measActive_) return false;
  const int rate = rate_;
  measResult_ = Measurement();
  measResult_.rate = rate;
  measTarget_ = (size_t)(std::max(0.5, std::min(20.0, seconds)) * rate);
  measCount_ = 0;
  measSum_ = measSq_ = measBlockSq_ = measBlockK_ = 0;
  measPeak_ = 0;
  measBlockN_ = measClip_ = 0;
  measNonZero_ = false;
  measGlitch0_ = glitches_;
  measBlocks_.clear();
  measLoud_.clear();
  measPitches_.clear();
  const size_t N = rate > 50000 ? 32768 : 16384;
  measFft_.init((int)N);
  measFftBuf_.assign(N, 0.f);
  measFftPos_ = 0;
  measPow_.assign(N / 2 + 1, 0.0);
  measFrames_ = 0;
  measKShelf_.design(ms::dsp::Biquad::kHighShelf, rate, 1681.97, 3.9998, 0.7071752);
  measKHp_.design(ms::dsp::Biquad::kHighPass, rate, 38.13547, 0.0, 0.5003270);
  measKShelf_.reset();
  measKHp_.reset();
  measActive_ = true;
  const bool done = measCv_.wait_for(lock, std::chrono::milliseconds((int)(seconds * 1000) + 4000),
                                     [this] { return !measActive_; });
  if (!done) {
    measActive_ = false;
    return false;
  }
  *out = measResult_;
  return out->ok;
}

// mu_ held, capture thread
void AudioEngine::finishMeasurement() {
  Measurement& r = measResult_;
  const double n = (double)measCount_;
  r.seconds = n / rate_;
  r.silent = !measNonZero_;
  r.peakDb = ToDb(measPeak_);
  r.rmsDb = PowDb(measSq_ / n);
  r.dcOffset = (float)(measSum_ / n);
  r.clipped = measClip_;
  r.glitches = glitches_ - measGlitch0_;
  if (!measBlocks_.empty()) {
    std::vector<float> s = measBlocks_;
    std::sort(s.begin(), s.end());
    r.noiseFloorDb = s[(size_t)(s.size() * 0.10)];
    r.speechDb = s[std::min(s.size() - 1, (size_t)(s.size() * 0.90))];
    int active = 0;
    double loud = 0;
    for (size_t i = 0; i < measBlocks_.size(); ++i)
      if (measBlocks_[i] > r.noiseFloorDb + 10.f) {
        ++active;
        loud += pow(10.0, measLoud_[i] / 10.0);
      }
    r.activeRatio = (float)active / measBlocks_.size();
    r.loudnessDb = active ? std::max(-70.f, -0.691f + PowDb(loud / active)) : -70.f;
  }
  if (!measPitches_.empty()) {
    std::vector<float> p = measPitches_;
    std::sort(p.begin(), p.end());
    r.pitchHz = p[p.size() / 2];
  }
  if (measFrames_ > 0) {
    const size_t N = measFftBuf_.size();
    const double binHz = (double)rate_ / N;
    const double norm = 1.0 / ((N / 4.0) * (N / 4.0) * measFrames_);
    auto sumRange = [&](double f0, double f1) {
      int k0 = std::max(1, (int)(f0 / binHz)), k1 = std::min((int)(N / 2), (int)(f1 / binHz));
      double s = 0;
      for (int k = k0; k <= k1; ++k) s += measPow_[k];
      return s * norm;
    };
    static const double kOct[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int b = 0; b < 10; ++b) r.bandDb[b] = PowDb(sumRange(kOct[b] / 1.41421356, kOct[b] * 1.41421356));
    const double mid = sumRange(300, 3000);
    r.sibilanceDb = PowDb(sumRange(4500, std::min(9000.0, rate_ * 0.45)) / (mid + 1e-20));
    r.lowDb = PowDb(sumRange(40, 150) / (mid + 1e-20));
    auto hum = [&](double f0) {
      float best = 0;
      for (int h = 1; h <= 3; ++h) {
        const double f = f0 * h;
        const int c = (int)lround(f / binHz);
        double peak = 0;
        for (int k = c - 1; k <= c + 1; ++k) peak = std::max(peak, measPow_[std::max(1, k)]);
        std::vector<double> nb;
        for (int k = (int)((f - 14) / binHz); k <= (int)((f + 14) / binHz); ++k)
          if (k > 0 && fabs(k * binHz - f) > 5.0) nb.push_back(measPow_[k]);
        if (nb.empty()) continue;
        std::sort(nb.begin(), nb.end());
        const double med = nb[nb.size() / 2] + 1e-30;
        best = std::max(best, (float)(10.0 * log10(peak / med + 1e-30)));
      }
      return best;
    };
    r.hum50Db = hum(50.0);
    r.hum60Db = hum(60.0);
  }
  r.ok = true;
  measActive_ = false;
  measCv_.notify_all();
}

}  // namespace app
