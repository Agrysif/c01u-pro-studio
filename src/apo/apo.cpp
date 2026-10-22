// System effect APO (Audio Processing Object) loaded by audiodg.exe for the microphone
// endpoint. Registered as SFX (Windows 10/11) and LFX (Windows 8/8.1), so every application
// that records from the microphone gets the processed voice.
//
// Real-time rules: APOProcess never allocates, locks, waits or calls into the kernel.
#include <windows.h>
#include <unknwn.h>
#include <objbase.h>
#include <mmreg.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#include <audiomediatype.h>
#include <audioenginebaseapo.h>
#include <audioengineextensionapo.h>
#include <sddl.h>
#include <xmmintrin.h>
#include <new>

#include "../dsp/chain.h"
#include "../shared/guids.h"
#include "../shared/params.h"
#include "../shared/shared_state.h"
#include "../shared/version.h"

using ms::Params;
using ms::SharedState;

namespace {

HMODULE g_module = nullptr;
volatile LONG g_objects = 0;
volatile LONG g_serverLocks = 0;

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// ------------------------------------------------------------------ params file
void ParamsFilePath(wchar_t* out, DWORD cch) {
  wchar_t base[MAX_PATH];
  DWORD n = ExpandEnvironmentStringsW(L"%ProgramData%", base, MAX_PATH);
  if (n == 0 || n > MAX_PATH || base[0] == L'%') wcscpy_s(base, L"C:\\ProgramData");
  swprintf_s(out, cch, L"%s\\%s\\params.bin", base, MS_DIR_NAME);
}

void ParamsDirPath(wchar_t* out, DWORD cch) {
  ParamsFilePath(out, cch);
  wchar_t* slash = wcsrchr(out, L'\\');
  if (slash) *slash = 0;
}

// Small diagnostic log (never called on the real-time thread): stream start/stop and errors.
// Goes to the data folder, or to the Windows temp folder if audiodg may not write there.
// When it grows past 256 KB the previous half is kept as apo.old.log.
void ApoLog(const char* fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[640];
  const int n = sprintf_s(line, "%04d-%02d-%02d %02d:%02d:%02d [%lu] %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                          t.wMinute, t.wSecond, GetCurrentProcessId(), msg);
  wchar_t dir[MAX_PATH];
  ms::DataDirPath(dir, MAX_PATH);
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt == 1) {
      GetWindowsDirectoryW(dir, MAX_PATH);
      wcscat_s(dir, L"\\Temp");
    }
    wchar_t path[MAX_PATH], old[MAX_PATH];
    swprintf_s(path, L"%s\\apo.log", dir);
    swprintf_s(old, L"%s\\apo.old.log", dir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 256 * 1024))
      MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) continue;
    DWORD w = 0;
    WriteFile(f, line, (DWORD)n, &w, nullptr);
    CloseHandle(f);
    return;
  }
}

bool LoadParamsFile(Params* p) {
  wchar_t path[MAX_PATH];
  ParamsFilePath(path, MAX_PATH);
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  Params tmp;
  DWORD read = 0;
  BOOL ok = ReadFile(f, &tmp, sizeof(tmp), &read, nullptr);
  CloseHandle(f);
  if (!ok || read != sizeof(tmp) || tmp.magic != ms::kParamsMagic || tmp.size != sizeof(Params)) return false;
  ms::SanitizeParams(&tmp);
  *p = tmp;
  return true;
}

// ------------------------------------------------------------------ IAudioMediaType
class MediaType final : public IAudioMediaType {
 public:
  explicit MediaType(const UNCOMPRESSEDAUDIOFORMAT& u) : ref_(1), u_(u) {
    ZeroMemory(&wfx_, sizeof(wfx_));
    wfx_.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx_.Format.nChannels = (WORD)u.dwSamplesPerFrame;
    wfx_.Format.nSamplesPerSec = (DWORD)u.fFramesPerSecond;
    wfx_.Format.wBitsPerSample = (WORD)(u.dwBytesPerSampleContainer * 8);
    wfx_.Format.nBlockAlign = (WORD)(u.dwBytesPerSampleContainer * u.dwSamplesPerFrame);
    wfx_.Format.nAvgBytesPerSec = wfx_.Format.nSamplesPerSec * wfx_.Format.nBlockAlign;
    wfx_.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx_.Samples.wValidBitsPerSample = (WORD)u.dwValidBitsPerSample;
    wfx_.dwChannelMask = u.dwChannelMask;
    wfx_.SubFormat = u.guidFormatType;
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioMediaType)) {
      *ppv = static_cast<IAudioMediaType*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  STDMETHODIMP IsCompressedFormat(BOOL* compressed) override {
    if (!compressed) return E_POINTER;
    *compressed = FALSE;
    return S_OK;
  }
  STDMETHODIMP IsEqual(IAudioMediaType* other, DWORD* flags) override {
    if (!other || !flags) return E_POINTER;
    *flags = 0;
    UNCOMPRESSEDAUDIOFORMAT o;
    if (FAILED(other->GetUncompressedAudioFormat(&o))) return S_OK;
    if (o.guidFormatType == u_.guidFormatType) *flags |= AUDIOMEDIATYPE_EQUAL_FORMAT_TYPES;
    if (o.guidFormatType == u_.guidFormatType && o.dwSamplesPerFrame == u_.dwSamplesPerFrame &&
        o.dwBytesPerSampleContainer == u_.dwBytesPerSampleContainer && o.dwValidBitsPerSample == u_.dwValidBitsPerSample &&
        o.fFramesPerSecond == u_.fFramesPerSecond && o.dwChannelMask == u_.dwChannelMask)
      *flags |= AUDIOMEDIATYPE_EQUAL_FORMAT_DATA | AUDIOMEDIATYPE_EQUAL_FORMAT_USER_DATA;
    return S_OK;
  }
  STDMETHODIMP_(const WAVEFORMATEX*) GetAudioFormat() override { return &wfx_.Format; }
  STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* out) override {
    if (!out) return E_POINTER;
    *out = u_;
    return S_OK;
  }

 private:
  volatile ULONG ref_;
  UNCOMPRESSEDAUDIOFORMAT u_;
  WAVEFORMATEXTENSIBLE wfx_;
};

HRESULT CheckFormat(IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported) {
  if (!requested || !supported) return E_POINTER;
  *supported = nullptr;
  UNCOMPRESSEDAUDIOFORMAT req;
  if (FAILED(requested->GetUncompressedAudioFormat(&req))) return APOERR_FORMAT_NOT_SUPPORTED;
  UNCOMPRESSEDAUDIOFORMAT want = req;
  UNCOMPRESSEDAUDIOFORMAT opp;
  if (opposite && SUCCEEDED(opposite->GetUncompressedAudioFormat(&opp))) want = opp;
  want.guidFormatType = kSubtypeFloat;
  want.dwBytesPerSampleContainer = 4;
  want.dwValidBitsPerSample = 32;
  if (want.dwSamplesPerFrame < 1 || want.dwSamplesPerFrame > 64 || want.fFramesPerSecond < 8000.f ||
      want.fFramesPerSecond > 384000.f)
    return APOERR_FORMAT_NOT_SUPPORTED;
  const bool same = req.guidFormatType == want.guidFormatType && req.dwBytesPerSampleContainer == 4 &&
                    req.dwValidBitsPerSample == 32 && req.dwSamplesPerFrame == want.dwSamplesPerFrame &&
                    req.fFramesPerSecond == want.fFramesPerSecond;
  if (same) {
    requested->AddRef();
    *supported = requested;
    return S_OK;
  }
  MediaType* mt = new (std::nothrow) MediaType(want);
  if (!mt) return E_OUTOFMEMORY;
  *supported = mt;
  return S_FALSE;
}

// ------------------------------------------------------------------ shared state link
// Maps the shared file used with the control app. If that is impossible a private block is
// used and a watcher thread reloads params.bin whenever it changes.
class SharedLink {
 public:
  ~SharedLink() { close(); }

  bool shared() const { return state_ && state_ != local_; }

  void open() {
    if (state_) return;
    DWORD err = 0;
    state_ = ms::MapSharedFile(&file_, &map_, &err);
    if (state_) {
      if (state_->magic != ms::kSharedMagic || state_->version != ms::kSharedVersion) initialize(state_);
      return;
    }
    ApoLog("shared file unavailable (error %lu), falling back to params.bin watching", err);
    local_ = new (std::nothrow) SharedState;
    if (!local_) return;
    ZeroMemory(local_, sizeof(SharedState));
    initialize(local_);
    state_ = local_;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_) thread_ = CreateThread(nullptr, 0, &SharedLink::WatchThread, this, 0, nullptr);
  }

  void close() {
    if (thread_) {
      SetEvent(stop_);
      WaitForSingleObject(thread_, 5000);
      CloseHandle(thread_);
      thread_ = nullptr;
    }
    if (stop_) {
      CloseHandle(stop_);
      stop_ = nullptr;
    }
    if (state_ && state_ != local_) ms::UnmapSharedFile(state_, file_, map_);
    state_ = nullptr;
    map_ = file_ = nullptr;
    delete local_;
    local_ = nullptr;
  }

  SharedState* state() const { return state_; }

 private:
  static void initialize(SharedState* s) {
    Params p;
    if (!LoadParamsFile(&p)) ms::DefaultParams(&p);
    s->version = ms::kSharedVersion;
    s->size = sizeof(SharedState);
    ms::WriteParamsSeq(s, p);
    InterlockedExchange(&s->magic, ms::kSharedMagic);
  }

  static DWORD WINAPI WatchThread(LPVOID arg) {
    SharedLink* self = (SharedLink*)arg;
    wchar_t dir[MAX_PATH];
    ParamsDirPath(dir, MAX_PATH);
    HANDLE change = FindFirstChangeNotificationW(dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    for (;;) {
      if (change == INVALID_HANDLE_VALUE) {
        // directory may not exist yet: poll slowly
        if (WaitForSingleObject(self->stop_, 2000) == WAIT_OBJECT_0) break;
        change = FindFirstChangeNotificationW(dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
        continue;
      }
      HANDLE h[2] = {self->stop_, change};
      DWORD w = WaitForMultipleObjects(2, h, FALSE, INFINITE);
      if (w != WAIT_OBJECT_0 + 1) break;
      Sleep(50);  // let the writer finish
      Params p;
      if (LoadParamsFile(&p)) ms::WriteParamsSeq(self->local_, p);
      if (!FindNextChangeNotification(change)) {
        FindCloseChangeNotification(change);
        change = INVALID_HANDLE_VALUE;
      }
    }
    if (change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change);
    return 0;
  }

  HANDLE file_ = nullptr;
  HANDLE map_ = nullptr;
  SharedState* state_ = nullptr;
  SharedState* local_ = nullptr;
  HANDLE stop_ = nullptr;
  HANDLE thread_ = nullptr;
};

// ------------------------------------------------------------------ the APO
const APO_REG_PROPERTIES kRegProps = {
    CLSID_MicStudioApo,
    (APO_FLAG)(APO_FLAG_INPLACE | APO_FLAG_DEFAULT),
    L"C01U Pro Studio",
    L"Unofficial driver for Samson C01U Pro",
    MS_VERSION_MAJOR,
    MS_VERSION_MINOR,
    1, 1, 1, 1,
    0xFFFFFFFF,
    1,
    {__uuidof(IAudioSystemEffects)},
};

// The audio engine creates system effect APOs aggregated (CoCreateInstance with an outer
// IUnknown), so the object follows the COM aggregation rules: the interfaces delegate
// IUnknown to the outer object, and `inner_` is the non-delegating IUnknown that owns the
// reference count. Without an outer object `inner_` plays both roles.
// Effect types reported to Windows (ksmedia.h AUDIO_EFFECT_TYPE_*), so apps can see that
// noise suppression etc. already happen in the system path.
const GUID kEffectNoiseSuppression = {0x6f64adbf, 0x8211, 0x11e2, {0x8c, 0x70, 0x2c, 0x27, 0xd7, 0xf0, 0x01, 0xfa}};
const GUID kEffectAgc = {0x6f64adc0, 0x8211, 0x11e2, {0x8c, 0x70, 0x2c, 0x27, 0xd7, 0xf0, 0x01, 0xfa}};
const GUID kEffectEqualizer = {0x6f64adc3, 0x8211, 0x11e2, {0x8c, 0x70, 0x2c, 0x27, 0xd7, 0xf0, 0x01, 0xfa}};
const GUID kEffectDrc = {0x6f64adce, 0x8211, 0x11e2, {0x8c, 0x70, 0x2c, 0x27, 0xd7, 0xf0, 0x01, 0xfa}};

class MicApo final : public IAudioProcessingObject,
                     public IAudioProcessingObjectRT,
                     public IAudioProcessingObjectConfiguration,
                     public IAudioSystemEffects3 {
 public:
  explicit MicApo(IUnknown* outer) : inner_(this), outer_(outer ? outer : &inner_) { InterlockedIncrement(&g_objects); }
  ~MicApo() {
    if (locked_) UnlockForProcess();
    link_.close();
    InterlockedDecrement(&g_objects);
  }
  IUnknown* nonDelegating() { return &inner_; }

  // IUnknown (delegating)
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { return outer_->QueryInterface(riid, ppv); }
  STDMETHODIMP_(ULONG) AddRef() override { return outer_->AddRef(); }
  STDMETHODIMP_(ULONG) Release() override { return outer_->Release(); }

  // IAudioProcessingObject
  STDMETHODIMP Reset() override {
    InterlockedExchange(&resetPending_, 1);
    return S_OK;
  }
  STDMETHODIMP GetLatency(HNSTIME* time) override {
    if (!time) return E_POINTER;
    *time = (locked_ && rate_ > 0) ? (HNSTIME)((double)chain_.latency() * 1e7 / rate_) : 0;
    return S_OK;
  }
  STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** props) override {
    if (!props) return E_POINTER;
    *props = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
    if (!*props) return E_OUTOFMEMORY;
    memcpy(*props, &kRegProps, sizeof(APO_REG_PROPERTIES));
    return S_OK;
  }
  STDMETHODIMP Initialize(UINT32 cbDataSize, BYTE* data) override {
    if (cbDataSize > 0 && !data) return E_POINTER;
    // APOInitSystemEffects (Windows 8), ...2 (Windows 8.1/10) or ...3 (80 bytes, Windows 11).
    // Nothing heavy happens here, so a discovery-only instance costs nothing either way.
    if (cbDataSize == sizeof(APOInitSystemEffects2) && ((const APOInitSystemEffects2*)data)->InitializeForDiscoveryOnly)
      return S_OK;
    initialized_ = true;
    return S_OK;
  }
  STDMETHODIMP IsInputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* requested,
                                      IAudioMediaType** supported) override {
    const HRESULT hr = CheckFormat(opposite, requested, supported);
    if (FAILED(hr)) LogFormat("IsInputFormatSupported", requested, hr);
    return hr;
  }
  STDMETHODIMP IsOutputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* requested,
                                       IAudioMediaType** supported) override {
    const HRESULT hr = CheckFormat(opposite, requested, supported);
    if (FAILED(hr)) LogFormat("IsOutputFormatSupported", requested, hr);
    return hr;
  }
  STDMETHODIMP GetInputChannelCount(UINT32* count) override {
    if (!count) return E_POINTER;
    *count = channels_;
    return S_OK;
  }

  // IAudioSystemEffects2: which effects are active (Windows 8.1+ effect discovery)
  STDMETHODIMP GetEffectsList(LPGUID* ids, UINT* count, HANDLE /*event*/) override {
    if (!ids || !count) return E_POINTER;
    const Params& p = chain_.params();
    GUID list[4];
    UINT n = 0;
    if (!p.bypass) {
      if (p.nsOn) list[n++] = kEffectNoiseSuppression;
      if (p.agcOn) list[n++] = kEffectAgc;
      if (p.eqOn) list[n++] = kEffectEqualizer;
      if (p.compOn || p.limOn) list[n++] = kEffectDrc;
    }
    *count = n;
    *ids = nullptr;
    if (n) {
      *ids = (LPGUID)CoTaskMemAlloc(n * sizeof(GUID));
      if (!*ids) return E_OUTOFMEMORY;
      memcpy(*ids, list, n * sizeof(GUID));
    }
    return S_OK;
  }
  // IAudioSystemEffects3 (Windows 11): nothing for the OS to toggle, the panel controls it all
  STDMETHODIMP GetControllableSystemEffectsList(AUDIO_SYSTEMEFFECT** effects, UINT* numEffects, HANDLE) override {
    if (!effects || !numEffects) return E_POINTER;
    *effects = nullptr;
    *numEffects = 0;
    return S_OK;
  }
  STDMETHODIMP SetAudioSystemEffectState(GUID, AUDIO_SYSTEMEFFECT_STATE) override { return E_NOTFOUND; }

  // IAudioProcessingObjectConfiguration
  STDMETHODIMP LockForProcess(UINT32 nIn, APO_CONNECTION_DESCRIPTOR** in, UINT32 nOut,
                              APO_CONNECTION_DESCRIPTOR** out) override {
    if (locked_) return APOERR_APO_LOCKED;
    if (nIn != 1 || nOut != 1 || !in || !out || !in[0] || !out[0] || !in[0]->pFormat) return E_INVALIDARG;
    UNCOMPRESSEDAUDIOFORMAT f;
    HRESULT hr = in[0]->pFormat->GetUncompressedAudioFormat(&f);
    if (FAILED(hr)) return hr;
    if (f.guidFormatType != kSubtypeFloat || f.dwBytesPerSampleContainer != 4 || f.dwSamplesPerFrame < 1)
      return APOERR_FORMAT_NOT_SUPPORTED;
    try {
      channels_ = f.dwSamplesPerFrame;
      rate_ = f.fFramesPerSecond;
      maxFrames_ = std::max<UINT32>(in[0]->u32MaxFrameCount, out[0]->u32MaxFrameCount);
      mono_.assign(maxFrames_ + 16, 0.f);
      chain_.prepare(rate_);
      link_.open();
      Params p;
      LONG seq = 0;
      SharedState* s = link_.state();
      if (s && ms::ReadParamsSeq(s, &p, &seq)) {
        ms::SanitizeParams(&p);
        chain_.setParams(p);
        lastSeq_ = seq;
      } else {
        lastSeq_ = -1;
      }
      lastRelearn_ = s ? s->relearnSeq : 0;
    } catch (...) {
      return E_OUTOFMEMORY;
    }
    if (SharedState* s = link_.state()) {
      InterlockedIncrement(&s->tele.activeStreams);
      InterlockedExchange(&s->tele.sampleRate, (LONG)rate_);
      InterlockedExchange(&s->tele.channels, (LONG)channels_);
      InterlockedExchange(&s->tele.latencySamples, (LONG)chain_.latency());
    }
    QueryPerformanceFrequency(&qpf_);
    load_ = 0.f;
    resetPending_ = 0;
    blocks_ = 0;
    peakIn_ = peakOut_ = 0.f;
    locked_ = true;
    ApoLog("LockForProcess: %.0f Hz, %u ch, max %u frames, shared=%d, bypass=%d, ns=%d", rate_, channels_, maxFrames_,
           link_.shared() ? 1 : 0, chain_.params().bypass, chain_.params().nsOn);
    return S_OK;
  }
  STDMETHODIMP UnlockForProcess() override {
    if (!locked_) return APOERR_ALREADY_UNLOCKED;
    locked_ = false;
    ApoLog("UnlockForProcess: %lld blocks, peak in %.1f dB, out %.1f dB", (long long)blocks_,
           ms::dsp::LinToDb(peakIn_), ms::dsp::LinToDb(peakOut_));
    if (SharedState* s = link_.state()) {
      if (InterlockedDecrement(&s->tele.activeStreams) < 0) InterlockedExchange(&s->tele.activeStreams, 0);
    }
    return S_OK;
  }

  // IAudioProcessingObjectRT
  STDMETHODIMP_(void) APOProcess(UINT32 nIn, APO_CONNECTION_PROPERTY** ppIn, UINT32 nOut,
                                 APO_CONNECTION_PROPERTY** ppOut) override {
    if (nIn < 1 || nOut < 1 || !ppIn || !ppOut || !ppIn[0] || !ppOut[0]) return;
    APO_CONNECTION_PROPERTY* in = ppIn[0];
    APO_CONNECTION_PROPERTY* out = ppOut[0];
    const float* src = (const float*)in->pBuffer;
    float* dst = (float*)out->pBuffer;
    UINT32 frames = in->u32ValidFrameCount;
    if (!locked_ || !src || !dst) return;
    if (frames > maxFrames_) frames = maxFrames_;
    const UINT32 ch = channels_;

    const unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040);  // flush denormals to zero
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);

    if (resetPending_) {
      InterlockedExchange(&resetPending_, 0);
      chain_.reset();
    }
    pollParams();

    float* m = mono_.data();
    if (in->u32BufferFlags == BUFFER_SILENT) {
      memset(m, 0, frames * sizeof(float));
    } else if (ch == 1) {
      memcpy(m, src, frames * sizeof(float));
    } else {
      const float inv = 1.f / ch;
      for (UINT32 i = 0; i < frames; ++i) {
        float s = 0.f;
        for (UINT32 c = 0; c < ch; ++c) s += src[i * ch + c];
        m[i] = s * inv;
      }
    }
    chain_.process(m, (int)frames);
    ++blocks_;
    if (chain_.stats().inPeak > peakIn_) peakIn_ = chain_.stats().inPeak;
    if (chain_.stats().outPeak > peakOut_) peakOut_ = chain_.stats().outPeak;
    if (ch == 1) {
      memcpy(dst, m, frames * sizeof(float));
    } else {
      for (UINT32 i = 0; i < frames; ++i)
        for (UINT32 c = 0; c < ch; ++c) dst[i * ch + c] = m[i];
    }
    out->u32ValidFrameCount = frames;
    out->u32BufferFlags = BUFFER_VALID;

    QueryPerformanceCounter(&t1);
    if (frames > 0 && rate_ > 0 && qpf_.QuadPart > 0) {
      const float spent = (float)(t1.QuadPart - t0.QuadPart) / (float)qpf_.QuadPart;
      const float budget = frames / rate_;
      load_ += (spent / budget - load_) * 0.05f;
    }
    publish();
    _mm_setcsr(csr);
  }
  STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 n) override { return n; }
  STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 n) override { return n; }

 private:
  void pollParams() {
    SharedState* s = link_.state();
    if (!s) return;
    if (s->relearnSeq != lastRelearn_) {
      lastRelearn_ = s->relearnSeq;
      chain_.relearnNoise();
    }
    if (s->paramsSeq == lastSeq_) return;
    LONG seq = 0;
    if (ms::ReadParamsSeq(s, &scratch_, &seq)) {
      ms::SanitizeParams(&scratch_);
      chain_.setParams(scratch_);
      lastSeq_ = seq;
    }
  }

  void publish() {
    SharedState* s = link_.state();
    if (!s) return;
    const ms::dsp::ChainStats& st = chain_.stats();
    ms::Telemetry& t = s->tele;
    InterlockedIncrement64(&t.blocks);
    t.lastTickMs = (LONG64)GetTickCount64();
    t.inPeakDb = ms::dsp::LinToDb(st.inPeak);
    t.inRmsDb = ms::dsp::PowToDb(st.inMs);
    t.outPeakDb = ms::dsp::LinToDb(st.outPeak);
    t.outRmsDb = ms::dsp::PowToDb(st.outMs);
    t.gateGainDb = ms::dsp::LinToDb(st.gateGain);
    t.nsReductionDb = st.nsReductionDb;
    t.compGainDb = st.compGainDb;
    t.limGainDb = ms::dsp::LinToDb(st.limGain);
    t.agcGainDb = st.agcGainDb;
    t.deessGainDb = ms::dsp::LinToDb(st.deessGain);
    t.popGainDb = ms::dsp::LinToDb(st.popGain);
    t.noiseFloorDb = st.inputFloorDb;
    t.voice = st.voice ? 1 : 0;
    t.bypass = chain_.params().bypass;
    t.dspLoad = load_;
  }

  HRESULT NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (riid == __uuidof(IAudioProcessingObject))
      *ppv = static_cast<IAudioProcessingObject*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectRT))
      *ppv = static_cast<IAudioProcessingObjectRT*>(this);
    else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
      *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
    else if (riid == __uuidof(IAudioSystemEffects) || riid == __uuidof(IAudioSystemEffects2) ||
             riid == __uuidof(IAudioSystemEffects3))
      *ppv = static_cast<IAudioSystemEffects3*>(this);
    else {
      *ppv = nullptr;
      return E_NOINTERFACE;
    }
    static_cast<IUnknown*>(*ppv)->AddRef();  // goes to the outer object when aggregated
    return S_OK;
  }

  class Inner final : public IUnknown {
   public:
    explicit Inner(MicApo* owner) : owner_(owner) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
      if (!ppv) return E_POINTER;
      if (riid == __uuidof(IUnknown)) {
        *ppv = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
      }
      return owner_->NonDelegatingQueryInterface(riid, ppv);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&owner_->ref_); }
    STDMETHODIMP_(ULONG) Release() override {
      const ULONG r = InterlockedDecrement(&owner_->ref_);
      if (!r) delete owner_;
      return r;
    }

   private:
    MicApo* owner_;
  };

  volatile ULONG ref_ = 1;
  Inner inner_;
  IUnknown* outer_;
  bool initialized_ = false;
  bool locked_ = false;
  UINT32 channels_ = 1;
  float rate_ = 48000.f;
  UINT32 maxFrames_ = 0;
  volatile LONG resetPending_ = 0;
  LONG lastSeq_ = -1;
  LONG lastRelearn_ = 0;
  LONG64 blocks_ = 0;
  float peakIn_ = 0.f, peakOut_ = 0.f;

  static void LogFormat(const char* what, IAudioMediaType* t, HRESULT hr) {
    UNCOMPRESSEDAUDIOFORMAT u = {};
    if (t) t->GetUncompressedAudioFormat(&u);
    ApoLog("%s: %lu ch, %lu bytes/sample, %.0f Hz, float=%d -> 0x%08lx", what, u.dwSamplesPerFrame,
           u.dwBytesPerSampleContainer, u.fFramesPerSecond, u.guidFormatType == kSubtypeFloat ? 1 : 0, hr);
  }
  float load_ = 0.f;
  LARGE_INTEGER qpf_ = {};
  Params scratch_;
  std::vector<float> mono_;
  ms::dsp::Chain chain_;
  SharedLink link_;
};

// ------------------------------------------------------------------ class factory
class Factory final : public IClassFactory {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    // an aggregating caller must ask for the inner IUnknown (COM rule)
    if (outer && riid != __uuidof(IUnknown)) return CLASS_E_NOAGGREGATION;
    MicApo* apo = new (std::nothrow) MicApo(outer);
    if (!apo) return E_OUTOFMEMORY;
    HRESULT hr = apo->nonDelegating()->QueryInterface(riid, ppv);
    apo->nonDelegating()->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock)
      InterlockedIncrement(&g_serverLocks);
    else
      InterlockedDecrement(&g_serverLocks);
    return S_OK;
  }
};

Factory g_factory;

// ------------------------------------------------------------------ registration helpers
LSTATUS SetStr(HKEY key, const wchar_t* name, const wchar_t* value) {
  return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}
LSTATUS SetDword(HKEY key, const wchar_t* name, DWORD value) {
  return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
}

}  // namespace

// ------------------------------------------------------------------ exports
extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = inst;
    DisableThreadLibraryCalls(inst);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (clsid != CLSID_MicStudioApo) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return (g_objects == 0 && g_serverLocks == 0) ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH];
  if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
  const REGSAM sam = KEY_WRITE | KEY_WOW64_64KEY;
  HKEY key = nullptr;
  LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" MS_APO_CLSID_STR, 0, nullptr, 0, sam,
                               nullptr, &key, nullptr);
  if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
  SetStr(key, nullptr, L"C01U Pro Studio APO");
  RegCloseKey(key);
  st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" MS_APO_CLSID_STR L"\\InprocServer32", 0,
                       nullptr, 0, sam, nullptr, &key, nullptr);
  if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
  SetStr(key, nullptr, path);
  SetStr(key, L"ThreadingModel", L"Both");
  RegCloseKey(key);

  st = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                       L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" MS_APO_CLSID_STR, 0, nullptr, 0,
                       sam, nullptr, &key, nullptr);
  if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
  SetStr(key, L"FriendlyName", kRegProps.szFriendlyName);
  SetStr(key, L"Copyright", kRegProps.szCopyrightInfo);
  SetDword(key, L"MajorVersion", kRegProps.u32MajorVersion);
  SetDword(key, L"MinorVersion", kRegProps.u32MinorVersion);
  SetDword(key, L"Flags", (DWORD)kRegProps.Flags);
  SetDword(key, L"MinInputConnections", kRegProps.u32MinInputConnections);
  SetDword(key, L"MaxInputConnections", kRegProps.u32MaxInputConnections);
  SetDword(key, L"MinOutputConnections", kRegProps.u32MinOutputConnections);
  SetDword(key, L"MaxOutputConnections", kRegProps.u32MaxOutputConnections);
  SetDword(key, L"MaxInstances", kRegProps.u32MaxInstances);
  SetDword(key, L"NumAPOInterfaces", 1);
  SetStr(key, L"APOInterface0", MS_IID_AUDIO_SYSTEM_EFFECTS_STR);
  RegCloseKey(key);
  return S_OK;
}

STDAPI DllUnregisterServer() {
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" MS_APO_CLSID_STR);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" MS_APO_CLSID_STR);
  return S_OK;
}
