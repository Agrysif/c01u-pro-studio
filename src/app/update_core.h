#pragma once
// Update core with no other app dependencies (the update test links it alone): HTTPS download,
// SHA-256, ECDSA P-256 signature check, version compare and the release description.
//
// A release on GitHub carries (see tools/release.ps1):
//   update.txt                    key=value lines: version, installer name, size, SHA-256, notes
//   update.txt.sig                ECDSA P-256 (SHA-256) signature of update.txt, base64 of r||s
//   C01UProStudio-<version>.exe   the installer named in update.txt
// The private key stays on the author's computer, so even someone with access to the GitHub
// page cannot publish an update that installed copies would accept.
#include <stdint.h>

#include <string>
#include <vector>

namespace app {
namespace upd {

struct HttpResult {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
};

// GET over HTTPS only (WinHTTP, follows GitHub's redirects to its download host).
HttpResult HttpGet(const std::string& url, size_t maxBytes, const std::string& userAgent);

bool Sha256(const void* data, size_t size, uint8_t out[32]);
std::string ToHex(const uint8_t* data, size_t size);
bool Base64Decode(const std::string& text, std::vector<uint8_t>* out);

// pubkey: 64 bytes X||Y; signature: 64 bytes r||s
bool VerifySignature(const std::string& message, const uint8_t signature[64], const uint8_t pubkey[64]);
// update.txt.sig as release.ps1 writes it, checked with the key built into the app
bool VerifyRelease(const std::string& manifest, const std::string& signatureBase64);

int CompareVersions(const std::string& a, const std::string& b);  // "1.10.0" > "1.9.3": <0, 0, >0

struct Manifest {
  std::string name, version, file, sha256, page, notesRu, notesEn;
  uint64_t size = 0;
};
bool ParseManifest(const std::string& text, Manifest* m);

}  // namespace upd
}  // namespace app
