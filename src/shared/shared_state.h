#pragma once
// Memory block shared between the APO inside audiodg.exe and the control app.
// It is a view of %ProgramData%\C01U Pro Studio\shared.bin opened by both sides: unlike a
// Global\ named section this needs no special privilege (audiodg's token may not have
// SeCreateGlobalPrivilege). Views of one file are coherent, so it behaves like shared memory;
// the file is "temporary" so the cache manager keeps it in RAM.
// Parameters travel app -> APO through a seqlock, telemetry travels APO -> app.
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include "params.h"
#include "version.h"

namespace ms {

constexpr LONG kSharedMagic = 0x53433155;  // "U1CS"
constexpr uint32_t kSharedVersion = 2;
constexpr DWORD kSharedFileSize = 64 * 1024;

#pragma pack(push, 8)
struct Telemetry {
  volatile LONG64 blocks;         // processed APO buffers
  volatile LONG64 lastTickMs;     // GetTickCount64() of the last processed buffer
  volatile LONG activeStreams;    // APO instances locked for processing
  volatile LONG sampleRate;
  volatile LONG channels;
  volatile LONG latencySamples;
  volatile LONG voice;            // voice activity detected
  volatile LONG bypass;           // echo of the applied bypass flag
  float inPeakDb, inRmsDb;
  float outPeakDb, outRmsDb;
  float gateGainDb, nsReductionDb, compGainDb, limGainDb, agcGainDb, deessGainDb, popGainDb;
  float noiseFloorDb;
  float dspLoad;                  // processing time / real time, 0..1
  uint32_t reserved[16];
};

struct SharedState {
  volatile LONG magic;            // written last, after initialization
  uint32_t version;
  uint32_t size;
  volatile LONG paramsSeq;        // seqlock counter, odd while a write is in progress
  Params params;
  Telemetry tele;
  volatile LONG relearnSeq;       // app increments it to make the noise suppressor relearn
  uint32_t reserved[15];
};
#pragma pack(pop)

static_assert(sizeof(SharedState) < kSharedFileSize, "shared file too small");

inline void DataDirPath(wchar_t* out, DWORD cch) {
  wchar_t base[MAX_PATH];
  DWORD n = ExpandEnvironmentStringsW(L"%ProgramData%", base, MAX_PATH);
  if (n == 0 || n > MAX_PATH || base[0] == L'%') wcscpy_s(base, L"C:\\ProgramData");
  swprintf_s(out, cch, L"%s\\%s", base, MS_DIR_NAME);
}

// Opens (creating if needed) the shared file and maps it. Returns nullptr on failure
// with *error set; on success the caller owns *file and *mapping.
inline SharedState* MapSharedFile(HANDLE* file, HANDLE* mapping, DWORD* error) {
  wchar_t path[MAX_PATH];
  DataDirPath(path, MAX_PATH);
  wcscat_s(path, L"\\shared.bin");
  *mapping = nullptr;
  *file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
  if (*file == INVALID_HANDLE_VALUE) {
    *error = GetLastError();
    *file = nullptr;
    return nullptr;
  }
  *mapping = CreateFileMappingW(*file, nullptr, PAGE_READWRITE, 0, kSharedFileSize, nullptr);  // grows the file
  void* view = *mapping ? MapViewOfFile(*mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedState)) : nullptr;
  if (!view) {
    *error = GetLastError();
    if (*mapping) CloseHandle(*mapping);
    CloseHandle(*file);
    *mapping = *file = nullptr;
    return nullptr;
  }
  *error = 0;
  return (SharedState*)view;
}

inline void UnmapSharedFile(SharedState* s, HANDLE file, HANDLE mapping) {
  if (s) UnmapViewOfFile(s);
  if (mapping) CloseHandle(mapping);
  if (file) CloseHandle(file);
}

// Single writer (the control app or the APO's file watcher).
inline void WriteParamsSeq(SharedState* s, const Params& p) {
  LONG seq = s->paramsSeq;
  if (seq & 1) ++seq;  // a previous writer died mid-write
  InterlockedExchange(&s->paramsSeq, seq + 1);
  memcpy((void*)&s->params, &p, sizeof(Params));
  InterlockedExchange(&s->paramsSeq, seq + 2);
}

// Lock-free read, safe on the real-time thread. Returns false while a write is in progress.
inline bool ReadParamsSeq(const SharedState* s, Params* out, LONG* seqOut) {
  LONG s1 = s->paramsSeq;
  _ReadWriteBarrier();
  if (s1 & 1) return false;
  memcpy(out, (const void*)&s->params, sizeof(Params));
  _ReadWriteBarrier();
  MemoryBarrier();
  LONG s2 = s->paramsSeq;
  if (s1 != s2) return false;
  *seqOut = s1;
  return true;
}

}  // namespace ms
