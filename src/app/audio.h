#pragma once
// WASAPI side of the app: device helpers, the capture engine (meters, spectrum, loudness,
// pitch, recording, measurements, in-app processing) and render sinks (monitor / routing).
#include <windows.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../dsp/chain.h"
#include "../shared/params.h"

namespace app {

// ------------------------------------------------------------------ devices
struct DeviceInfo {
  std::wstring id;
  std::wstring name;        // "Microphone (Samson C01U Pro Mic)"
  std::wstring hwid;        // "{1}.USB\VID_17A0&PID_0303&MI_00\..."
  DWORD state = 0;          // DEVICE_STATE_*
  bool isDefault = false;   // default console device
  bool isDefaultComm = false;
  bool samson = false;      // Samson USB vendor id 17A0
  bool c01uPro = false;     // ... and product id 0303
};

std::vector<DeviceInfo> ListDevices(EDataFlow flow, bool includeInactive);
bool FindDevice(const std::wstring& id, DeviceInfo* out);
// Picks the microphone to use: the configured one, else a Samson, else the default.
bool ResolveMic(const std::wstring& configuredId, DeviceInfo* out);
// "{0.0.1.00000000}.{GUID}" -> "{GUID}" (the MMDevices registry key name)
std::wstring EndpointGuid(const std::wstring& id);

struct EndpointDetails {
  bool ok = false;
  float volume = 0.f;          // 0..1
  bool mute = false;
  float minDb = 0.f, maxDb = 0.f;
  int rate = 0, bits = 0, channels = 0;  // device (hardware) format
};
bool GetEndpointDetails(const std::wstring& id, EndpointDetails* out);
bool SetEndpointVolume(const std::wstring& id, float scalar);
bool SetEndpointMute(const std::wstring& id, bool mute);
bool SetDefaultDevice(const std::wstring& id);  // console + multimedia + communications
// "Audio enhancements" of an endpoint, read/written through the audio service like the
// Sound control panel does (no admin rights needed). Windows turns them off by itself
// when an effect fails to load, so the installer and diagnostics switch them back on.
bool GetEnhancementsDisabled(const std::wstring& id, bool* disabled);
bool SetEnhancementsEnabled(const std::wstring& id);

// ------------------------------------------------------------------ analysis results
constexpr int kSpectrumBands = 64;

struct Meters {
  unsigned long long seq = 0;
  bool running = false;
  float inPeakDb = -120.f, inRmsDb = -120.f;
  float outPeakDb = -120.f, outRmsDb = -120.f;
  bool clip = false;
  float lufsM = -70.f, lufsS = -70.f;
  float pitchHz = 0.f;
  float spectrum[kSpectrumBands] = {};
  // in-app chain state (valid while the app itself processes the voice)
  bool chainValid = false;
  ms::dsp::ChainStats chain;
};

struct Measurement {
  bool ok = false;
  double seconds = 0;
  int rate = 0;
  bool silent = true;          // only digital zeros
  float peakDb = -120.f, rmsDb = -120.f, dcOffset = 0.f;
  int clipped = 0;
  int glitches = 0;
  float noiseFloorDb = -120.f; // 10th percentile of 50 ms blocks
  float speechDb = -120.f;     // 90th percentile of 50 ms blocks
  float activeRatio = 0.f;     // share of blocks 10 dB above the floor
  float loudnessDb = -70.f;    // K-weighted level of the active blocks (LUFS-like)
  float bandDb[10] = {};       // 31.5 Hz .. 16 kHz octave bands
  float hum50Db = 0.f, hum60Db = 0.f;  // prominence of mains peaks over neighbours
  float sibilanceDb = -60.f;   // 4.5-9 kHz vs 300-3000 Hz
  float lowDb = -60.f;         // 40-150 Hz vs 300-3000 Hz
  float pitchHz = 0.f;         // median voice pitch
};

// ------------------------------------------------------------------ render sink
class SpscRing {
 public:
  explicit SpscRing(size_t pow2 = 1u << 17) : buf_(pow2), mask_(pow2 - 1) {}
  void push(const float* x, size_t n);
  bool pop(float* v);
  size_t size() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }
  void skip(size_t n);
  void clear() { r_.store(w_.load()); }

 private:
  std::vector<float> buf_;
  size_t mask_;
  std::atomic<size_t> w_{0}, r_{0};
};

class RenderSink {
 public:
  ~RenderSink() { stop(); }
  void start(const std::wstring& deviceId, int srcRate);
  void stop();
  bool running() const { return thread_.joinable(); }
  bool ok() const { return ok_.load(); }
  const std::wstring& deviceId() const { return device_; }
  int srcRate() const { return srcRate_; }
  void setVolume(float v) { volume_.store(v); }
  void push(const float* x, size_t n) {
    if (thread_.joinable()) ring_.push(x, n);
  }

 private:
  void run();
  std::thread thread_;
  HANDLE stop_ = nullptr;
  std::wstring device_;
  int srcRate_ = 48000;
  std::atomic<float> volume_{1.f};
  std::atomic<bool> ok_{false};
  SpscRing ring_;
};

// ------------------------------------------------------------------ capture engine
class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  void setDemand(bool want);                  // capture runs only while somebody needs it
  void setDevice(const std::wstring& id);     // "" = automatic
  void setParams(const ms::Params& p);        // parameters for the in-app chain
  void setAppProcessing(bool on);             // process here (no APO) or pass through
  void setMonitor(bool on, const std::wstring& device, float volume, bool raw);
  void setRoute(bool on, const std::wstring& device);
  void relearnNoise();

  bool running() const { return running_.load(); }
  bool deviceOk() const { return deviceOk_.load(); }
  std::wstring deviceId();
  std::wstring deviceName();
  int sampleRate() const { return rate_.load(); }
  int glitches() const { return glitches_.load(); }
  bool monitorOk() const { return monitor_.ok(); }
  bool routeOk() const { return route_.ok(); }
  void snapshot(Meters* m);

  // A/B recording: raw and processed side by side (max 30 s).
  bool startRecording(double seconds, double warmupSeconds);
  void stopRecording();
  int recordState(double* pos, double* len);  // 0 idle, 1 warm-up, 2 recording, 3 done
  bool recordingWav(bool processed, std::string* wav);

  // Blocking measurement of the captured signal.
  bool measure(double seconds, Measurement* out);

 private:
  void run();
  void tickDemand();
  void onBlock(const float* raw, int n, bool discontinuity);
  void analyse(const float* post, int n);
  void finishMeasurement();

  std::mutex mu_;
  std::thread thread_;
  HANDLE stopEvent_ = nullptr;
  std::atomic<bool> running_{false}, deviceOk_{false}, want_{false};
  std::atomic<int> rate_{0}, glitches_{0};
  std::wstring configuredId_, currentId_, currentName_;
  unsigned long long lastWant_ = 0;

  // processing
  ms::dsp::Chain chain_;
  ms::Params pendingParams_;
  std::atomic<bool> paramsDirty_{true};
  std::atomic<bool> appProcessing_{false};
  std::atomic<bool> relearn_{false};
  std::vector<float> post_;

  // monitor / route
  bool monitorOn_ = false, monitorRaw_ = false, routeOn_ = false;
  float monitorVolume_ = 0.8f;
  std::wstring monitorDev_, routeDev_;
  RenderSink monitor_, route_;
  void applySinks();

  // analysis (capture thread)
  Meters meters_;                 // published under mu_
  float acc_inPeak_ = 0, acc_outPeak_ = 0;
  double acc_inSq_ = 0, acc_outSq_ = 0;
  int acc_n_ = 0;
  bool acc_clip_ = false;
  std::vector<float> specRing_;
  size_t specPos_ = 0;
  int specCountdown_ = 0;
  ms::dsp::Fft specFft_;
  std::vector<float> specRe_, specIm_, specWin_;
  ms::dsp::Biquad kShelf_, kHp_;
  double lufsBlock_ = 0;
  int lufsN_ = 0;
  std::vector<double> lufsHist_;
  size_t lufsPos_ = 0;
  float lufsM_ = -70.f, lufsS_ = -70.f;
  std::vector<float> pitchBuf_;
  int pitchCountdown_ = 0;
  float pitch_ = 0.f;
  float estimatePitch(const float* x, int n, int rate);

  // recording
  int recState_ = 0;
  size_t recWarmup_ = 0, recTarget_ = 0;
  std::vector<float> recRaw_, recPost_;

  // measurement
  bool measActive_ = false;
  size_t measTarget_ = 0, measCount_ = 0;
  Measurement measResult_;
  std::condition_variable measCv_;
  std::vector<float> measBlocks_, measLoud_;
  double measSum_ = 0, measSq_ = 0, measBlockSq_ = 0, measBlockK_ = 0;
  float measPeak_ = 0;
  int measBlockN_ = 0, measClip_ = 0, measGlitch0_ = 0;
  bool measNonZero_ = false;
  std::vector<float> measFftBuf_;
  size_t measFftPos_ = 0;
  std::vector<double> measPow_;
  int measFrames_ = 0;
  std::vector<float> measPitches_;
  ms::dsp::Fft measFft_;
  ms::dsp::Biquad measKShelf_, measKHp_;
};

}  // namespace app
