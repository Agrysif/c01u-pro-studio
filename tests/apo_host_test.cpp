// Loads the APO dll the same way audiodg.exe does (DllGetClassObject -> IClassFactory) and
// drives it through format negotiation, LockForProcess and APOProcess.
// Usage: apo_host_test.exe path\to\C01UProStudioApo.dll
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#include <audiomediatype.h>
#include <audioenginebaseapo.h>
#include <audioengineextensionapo.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include "../src/shared/guids.h"

static int g_failed = 0;
#define CHECK(cond, ...)                   \
  do {                                     \
    const bool ok_ = (cond);               \
    printf(ok_ ? "  ok   " : "  FAIL ");   \
    if (!ok_) ++g_failed;                  \
    printf(__VA_ARGS__);                   \
    printf("\n");                          \
  } while (0)

static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kPcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

class TestType : public IAudioMediaType {
 public:
  explicit TestType(const UNCOMPRESSEDAUDIOFORMAT& u) : u_(u) {
    ZeroMemory(&w_, sizeof(w_));
    w_.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w_.Format.nChannels = (WORD)u.dwSamplesPerFrame;
    w_.Format.nSamplesPerSec = (DWORD)u.fFramesPerSecond;
    w_.Format.wBitsPerSample = (WORD)(u.dwBytesPerSampleContainer * 8);
    w_.Format.nBlockAlign = (WORD)(u.dwBytesPerSampleContainer * u.dwSamplesPerFrame);
    w_.Format.nAvgBytesPerSec = w_.Format.nBlockAlign * w_.Format.nSamplesPerSec;
    w_.Format.cbSize = 22;
    w_.Samples.wValidBitsPerSample = (WORD)u.dwValidBitsPerSample;
    w_.SubFormat = u.guidFormatType;
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioMediaType)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = --ref_;
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP IsCompressedFormat(BOOL* c) override { *c = FALSE; return S_OK; }
  STDMETHODIMP IsEqual(IAudioMediaType*, DWORD* f) override { *f = 0; return S_OK; }
  STDMETHODIMP_(const WAVEFORMATEX*) GetAudioFormat() override { return &w_.Format; }
  STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* o) override { *o = u_; return S_OK; }

 private:
  ULONG ref_ = 1;
  UNCOMPRESSEDAUDIOFORMAT u_;
  WAVEFORMATEXTENSIBLE w_;
};

static UNCOMPRESSEDAUDIOFORMAT Fmt(const GUID& g, DWORD ch, DWORD bytes, float rate) {
  UNCOMPRESSEDAUDIOFORMAT u = {};
  u.guidFormatType = g;
  u.dwSamplesPerFrame = ch;
  u.dwBytesPerSampleContainer = bytes;
  u.dwValidBitsPerSample = bytes * 8;
  u.fFramesPerSecond = rate;
  u.dwChannelMask = ch == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
  return u;
}

// Minimal aggregating outer object, the way the audio engine wraps system effect APOs.
class Outer : public IUnknown {
 public:
  IUnknown* inner = nullptr;
  ULONG ref = 1;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == __uuidof(IUnknown)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    return inner ? inner->QueryInterface(riid, ppv) : E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++ref; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = --ref;
    if (!r) {
      if (inner) inner->Release();
      delete this;
    }
    return r;
  }
};

static void RunStream(IAudioProcessingObject* apo, DWORD ch, float rate);

static void TestAggregation(IClassFactory* factory) {
  printf("aggregated by an outer object (audiodg style)\n");
  Outer* outer = new Outer();
  IAudioProcessingObject* bad = nullptr;
  CHECK(factory->CreateInstance(outer, __uuidof(IAudioProcessingObject), (void**)&bad) == CLASS_E_NOAGGREGATION,
        "aggregation asking for a non-IUnknown interface is refused");
  HRESULT hr = factory->CreateInstance(outer, __uuidof(IUnknown), (void**)&outer->inner);
  CHECK(SUCCEEDED(hr) && outer->inner, "CreateInstance(outer, IUnknown) -> 0x%08lx", hr);
  if (!outer->inner) return;
  IAudioProcessingObject* apo = nullptr;
  hr = outer->QueryInterface(__uuidof(IAudioProcessingObject), (void**)&apo);
  CHECK(SUCCEEDED(hr) && apo, "outer exposes IAudioProcessingObject");
  CHECK(outer->ref == 2, "inner AddRef went to the outer object (outer refs %lu)", outer->ref);
  IUnknown* id = nullptr;
  apo->QueryInterface(__uuidof(IUnknown), (void**)&id);
  CHECK(id == static_cast<IUnknown*>(outer), "identity: IUnknown of the APO is the outer object");
  if (id) id->Release();
  APOInitSystemEffects init = {};
  init.APOInit.cbSize = sizeof(init);
  init.APOInit.clsid = CLSID_MicStudioApo;
  CHECK(apo->Initialize(sizeof(init), (BYTE*)&init) == S_OK, "Initialize through the outer object");
  RunStream(apo, 1, 48000.f);
  apo->Release();
  CHECK(outer->ref == 1, "outer refs back to 1");
  outer->Release();  // releases the inner object too
}

static void RunStream(IAudioProcessingObject* apo, DWORD ch, float rate) {
  IAudioProcessingObjectConfiguration* cfg = nullptr;
  IAudioProcessingObjectRT* rt = nullptr;
  apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), (void**)&cfg);
  apo->QueryInterface(__uuidof(IAudioProcessingObjectRT), (void**)&rt);
  CHECK(cfg && rt, "configuration and RT interfaces");
  if (!cfg || !rt) return;

  const UINT32 frames = (UINT32)(rate / 100);
  TestType* type = new TestType(Fmt(kFloat, ch, 4, rate));
  std::vector<float> buf(frames * ch);
  APO_CONNECTION_DESCRIPTOR din = {APO_CONNECTION_BUFFER_TYPE_EXTERNAL, (UINT_PTR)buf.data(), frames, type,
                                   APO_CONNECTION_DESCRIPTOR_SIGNATURE};
  APO_CONNECTION_DESCRIPTOR dout = din;
  APO_CONNECTION_DESCRIPTOR* pin = &din;
  APO_CONNECTION_DESCRIPTOR* pout = &dout;
  HRESULT hr = cfg->LockForProcess(1, &pin, 1, &pout);
  CHECK(hr == S_OK, "LockForProcess %lu ch @ %.0f Hz -> 0x%08lx", ch, rate, hr);
  CHECK(cfg->LockForProcess(1, &pin, 1, &pout) == APOERR_APO_LOCKED, "second lock refused");

  HNSTIME lat = 0;
  apo->GetLatency(&lat);
  CHECK(lat > 0 && lat < 300000, "latency %.1f ms", lat / 10000.0);

  APO_CONNECTION_PROPERTY pi = {(UINT_PTR)buf.data(), frames, BUFFER_VALID, APO_CONNECTION_PROPERTY_SIGNATURE};
  APO_CONNECTION_PROPERTY po = {(UINT_PTR)buf.data(), 0, BUFFER_INVALID, APO_CONNECTION_PROPERTY_SIGNATURE};
  APO_CONNECTION_PROPERTY* ppi = &pi;
  APO_CONNECTION_PROPERTY* ppo = &po;
  unsigned seed = 1;
  double phase = 0;
  float peak = 0;
  bool finite = true, channelsEqual = true;
  double inEnergy = 0, outEnergy = 0;
  LARGE_INTEGER f, t0, t1;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);
  const int blocks = 400;  // 4 s
  for (int b = 0; b < blocks; ++b) {
    for (UINT32 i = 0; i < frames; ++i) {
      seed = seed * 1664525u + 1013904223u;
      float noise = ((seed >> 9) / 8388608.f - 1.f) * 0.003f;
      phase += 2 * 3.14159265 * 180.0 / rate;
      float voice = (b > 150 && b < 300) ? 0.3f * (float)sin(phase) : 0.f;
      for (DWORD c = 0; c < ch; ++c) buf[i * ch + c] = voice + noise;
      inEnergy += (double)(voice + noise) * (voice + noise);
    }
    pi.u32ValidFrameCount = frames;
    rt->APOProcess(1, &ppi, 1, &ppo);
    for (UINT32 i = 0; i < frames; ++i) {
      for (DWORD c = 0; c < ch; ++c) {
        float v = buf[i * ch + c];
        if (!_finite(v)) finite = false;
        if (fabsf(v) > peak) peak = fabsf(v);
        if (c > 0 && v != buf[i * ch]) channelsEqual = false;
      }
      outEnergy += (double)buf[i * ch] * buf[i * ch];
    }
  }
  QueryPerformanceCounter(&t1);
  const double sec = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
  CHECK(po.u32ValidFrameCount == frames && po.u32BufferFlags == BUFFER_VALID, "output connection marked valid");
  CHECK(finite, "output finite");
  CHECK(peak <= 0.892f, "output peak %.3f (limiter ceiling -1 dBFS)", peak);
  CHECK(ch == 1 || channelsEqual, "all channels carry the processed voice");
  CHECK(fabs(10 * log10(outEnergy / inEnergy)) < 20.0, "energy change %.1f dB", 10 * log10(outEnergy / inEnergy));
  CHECK(sec < 4.0 * 0.05, "4 s of audio processed in %.1f ms (%.2f%% CPU)", sec * 1000, sec / 4.0 * 100);

  CHECK(cfg->UnlockForProcess() == S_OK, "UnlockForProcess");
  CHECK(cfg->UnlockForProcess() == APOERR_ALREADY_UNLOCKED, "second unlock refused");
  type->Release();
  cfg->Release();
  rt->Release();
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: apo_host_test <dll>\n");
    return 2;
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  HMODULE dll = LoadLibraryA(argv[1]);
  CHECK(dll != nullptr, "LoadLibrary %s", argv[1]);
  if (!dll) return 1;
  typedef HRESULT(STDAPICALLTYPE * GetClassObjectFn)(REFCLSID, REFIID, void**);
  GetClassObjectFn getClass = (GetClassObjectFn)GetProcAddress(dll, "DllGetClassObject");
  CHECK(getClass && GetProcAddress(dll, "DllRegisterServer") && GetProcAddress(dll, "DllCanUnloadNow"),
        "COM exports present");
  IClassFactory* factory = nullptr;
  HRESULT hr = getClass(CLSID_MicStudioApo, __uuidof(IClassFactory), (void**)&factory);
  CHECK(SUCCEEDED(hr) && factory, "DllGetClassObject -> 0x%08lx", hr);
  if (!factory) return 1;

  printf("APO object\n");
  IAudioProcessingObject* apo = nullptr;
  hr = factory->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), (void**)&apo);
  CHECK(SUCCEEDED(hr) && apo, "CreateInstance -> 0x%08lx", hr);
  if (!apo) return 1;
  IUnknown* sfx = nullptr;
  CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects), (void**)&sfx)), "IAudioSystemEffects");
  if (sfx) sfx->Release();
  IAudioSystemEffects3* fx3 = nullptr;
  CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects3), (void**)&fx3)) && fx3, "IAudioSystemEffects3");
  if (fx3) {
    LPGUID ids = nullptr;
    UINT n = 0;
    CHECK(fx3->GetEffectsList(&ids, &n, nullptr) == S_OK && n >= 1, "GetEffectsList reports %u effects", n);
    if (ids) CoTaskMemFree(ids);
    AUDIO_SYSTEMEFFECT* eff = nullptr;
    UINT ne = 7;
    CHECK(fx3->GetControllableSystemEffectsList(&eff, &ne, nullptr) == S_OK && ne == 0, "no OS-controllable effects");
    fx3->Release();
  }

  APO_REG_PROPERTIES* reg = nullptr;
  hr = apo->GetRegistrationProperties(&reg);
  CHECK(SUCCEEDED(hr) && reg && reg->clsid == CLSID_MicStudioApo && (reg->Flags & APO_FLAG_INPLACE),
        "registration properties (%ls)", reg ? reg->szFriendlyName : L"?");
  if (reg) CoTaskMemFree(reg);

  APOInitSystemEffects init = {};
  init.APOInit.cbSize = sizeof(init);
  init.APOInit.clsid = CLSID_MicStudioApo;
  CHECK(apo->Initialize(sizeof(init), (BYTE*)&init) == S_OK, "Initialize");

  printf("format negotiation\n");
  TestType* pcm = new TestType(Fmt(kPcm, 1, 2, 48000));
  IAudioMediaType* sup = nullptr;
  hr = apo->IsInputFormatSupported(nullptr, pcm, &sup);
  UNCOMPRESSEDAUDIOFORMAT u = {};
  if (sup) sup->GetUncompressedAudioFormat(&u);
  CHECK(hr == S_FALSE && sup && u.guidFormatType == kFloat && u.dwSamplesPerFrame == 1 && u.fFramesPerSecond == 48000,
        "16-bit PCM -> suggests 32-bit float (hr 0x%08lx)", hr);
  if (sup) sup->Release();
  pcm->Release();
  TestType* flt = new TestType(Fmt(kFloat, 2, 4, 44100));
  sup = nullptr;
  hr = apo->IsOutputFormatSupported(nullptr, flt, &sup);
  CHECK(hr == S_OK && sup == flt, "float stereo accepted as is");
  if (sup) sup->Release();
  flt->Release();

  printf("mono stream (Samson C01U Pro: 48 kHz mono)\n");
  RunStream(apo, 1, 48000.f);
  printf("stereo stream 44.1 kHz\n");
  RunStream(apo, 2, 44100.f);

  ULONG left = apo->Release();
  CHECK(left == 0, "released (refcount %lu)", left);
  TestAggregation(factory);
  factory->Release();
  typedef HRESULT(STDAPICALLTYPE * CanUnloadFn)();
  CanUnloadFn canUnload = (CanUnloadFn)GetProcAddress(dll, "DllCanUnloadNow");
  CHECK(canUnload && canUnload() == S_OK, "DllCanUnloadNow");
  FreeLibrary(dll);
  CoUninitialize();
  printf(g_failed ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_failed);
  return g_failed ? 1 : 0;
}
