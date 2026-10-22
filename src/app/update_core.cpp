#include "update_core.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <sstream>

#include "update_key.h"

namespace app {
namespace upd {

namespace {

struct WinHttpHandle {
  HINTERNET h;
  explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
  ~WinHttpHandle() {
    if (h) WinHttpCloseHandle(h);
  }
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;
};

std::wstring Wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

// "\n" inside a value stands for a line break (notes are one line in update.txt)
std::string Unescape(const std::string& v) {
  std::string out;
  out.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == 'n') {
      out += '\n';
      ++i;
    } else {
      out += v[i];
    }
  }
  return out;
}

}  // namespace

HttpResult HttpGet(const std::string& url, size_t maxBytes, const std::string& userAgent) {
  HttpResult r;
  const std::wstring wurl = Wide(url);
  URL_COMPONENTS uc = {sizeof(uc)};
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
    r.error = "bad url";
    return r;
  }
  const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
  std::wstring object(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.lpszExtraInfo) object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

  WinHttpHandle session(WinHttpOpen(Wide(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session.h) {  // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY needs Windows 8.1
    session.h = WinHttpOpen(Wide(userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session.h) {
    r.error = "winhttp";
    return r;
  }
  WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
    protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
  }
  WinHttpHandle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
  if (!connect.h) {
    r.error = "connect";
    return r;
  }
  WinHttpHandle request(WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request.h) {
    r.error = "request";
    return r;
  }
  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    r.error = "network error " + std::to_string(GetLastError());
    return r;
  }
  DWORD status = 0, len = sizeof(status);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                      &len, WINHTTP_NO_HEADER_INDEX);
  r.status = (int)status;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(request.h, &avail)) {
      r.error = "read error";
      return r;
    }
    if (avail == 0) break;
    if (r.body.size() + avail > maxBytes) {
      r.error = "too large";
      return r;
    }
    const size_t old = r.body.size();
    r.body.resize(old + avail);
    DWORD got = 0;
    if (!WinHttpReadData(request.h, &r.body[old], avail, &got)) {
      r.error = "read error";
      return r;
    }
    r.body.resize(old + got);
  }
  r.ok = status == 200;
  if (!r.ok) r.error = "http " + std::to_string(status);
  return r;
}

bool Sha256(const void* data, size_t size, uint8_t out[32]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  ok = ok && BCryptHashData(hash, (PUCHAR)data, (ULONG)size, 0) == 0;
  ok = ok && BCryptFinishHash(hash, out, 32, 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

std::string ToHex(const uint8_t* data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string s;
  s.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    s += digits[data[i] >> 4];
    s += digits[data[i] & 15];
  }
  return s;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>* out) {
  out->clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    else return false;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back((uint8_t)((acc >> bits) & 0xFF));
    }
  }
  return true;
}

bool VerifySignature(const std::string& message, const uint8_t signature[64], const uint8_t pubkey[64]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return false;
  std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
  BCRYPT_ECCKEY_BLOB* hdr = (BCRYPT_ECCKEY_BLOB*)blob.data();
  hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
  hdr->cbKey = 32;
  memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), pubkey, 64);
  BCRYPT_KEY_HANDLE key = nullptr;
  bool ok = false;
  if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), (ULONG)blob.size(), 0) == 0) {
    uint8_t hash[32];
    if (Sha256(message.data(), message.size(), hash))
      ok = BCryptVerifySignature(key, nullptr, hash, 32, (PUCHAR)signature, 64, 0) == 0;
    BCryptDestroyKey(key);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

bool VerifyRelease(const std::string& manifest, const std::string& signatureBase64) {
  std::vector<uint8_t> sig;
  return Base64Decode(signatureBase64, &sig) && sig.size() == 64 &&
         VerifySignature(manifest, sig.data(), kUpdatePublicKey);
}

int CompareVersions(const std::string& a, const std::string& b) {
  std::istringstream sa(a), sb(b);
  for (int i = 0; i < 4; ++i) {
    std::string pa, pb;
    long va = 0, vb = 0;
    if (std::getline(sa, pa, '.')) va = strtol(pa.c_str(), nullptr, 10);
    if (std::getline(sb, pb, '.')) vb = strtol(pb.c_str(), nullptr, 10);
    if (va != vb) return va < vb ? -1 : 1;
  }
  return 0;
}

bool ParseManifest(const std::string& text, Manifest* m) {
  std::map<std::string, std::string> kv;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    kv[line.substr(0, eq)] = Unescape(line.substr(eq + 1));
  }
  m->name = kv["name"];
  m->version = kv["version"];
  m->file = kv["file"];
  m->sha256 = kv["sha256"];
  m->size = strtoull(kv["size"].c_str(), nullptr, 10);
  m->page = kv["page"];
  m->notesRu = kv["notes_ru"];
  m->notesEn = kv["notes_en"];
  // the file name and version go into a URL and a path: safe characters only
  for (char c : m->file)
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
  for (char c : m->version)
    if (!(isdigit((unsigned char)c) || c == '.')) return false;
  if (m->file.find("..") != std::string::npos || m->file.size() < 5 ||
      _stricmp(m->file.c_str() + m->file.size() - 4, ".exe") != 0)
    return false;
  return !m->name.empty() && !m->version.empty() && m->sha256.size() == 64 && m->size > 0;
}

}  // namespace upd
}  // namespace app
