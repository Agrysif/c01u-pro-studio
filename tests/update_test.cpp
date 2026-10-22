// Checks of the update core (src/app/update_core.*), the same code the installed app runs.
//   update_test.exe                          self-test: hashes, versions, release description, signatures
//   update_test.exe verify update.txt update.txt.sig     signature check with the key built into the app
//   update_test.exe online owner/repo        the latest GitHub release as installed copies will see it
#include <windows.h>
#include <bcrypt.h>

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/app/update_core.h"

using namespace app::upd;

static int g_failed = 0;
#define CHECK(cond, what)                                   \
  do {                                                      \
    const bool ok_ = (cond);                                \
    printf("  %s   %s\n", ok_ ? "ok  " : "FAIL", what);     \
    if (!ok_) ++g_failed;                                   \
  } while (0)

static bool ReadAll(const char* path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream s;
  s << f.rdbuf();
  *out = s.str();
  return true;
}

// A throwaway key pair signs a message the way release.ps1 does (SHA-256, r||s).
static bool SignWithNewKey(const std::string& msg, uint8_t sig[64], uint8_t pub[64]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return false;
  bool ok = BCryptGenerateKeyPair(alg, &key, 256, 0) == 0 && BCryptFinalizeKeyPair(key, 0) == 0;
  uint8_t hash[32];
  ULONG got = 0;
  ok = ok && Sha256(msg.data(), msg.size(), hash) && BCryptSignHash(key, nullptr, hash, 32, sig, 64, &got, 0) == 0 && got == 64;
  std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
  ok = ok && BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), (ULONG)blob.size(), &got, 0) == 0;
  if (ok) memcpy(pub, blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), 64);
  if (key) BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

static int SelfTest() {
  printf("update core self-test\n");
  uint8_t h[32];
  CHECK(Sha256("abc", 3, h) && ToHex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 of \"abc\"");
  std::vector<uint8_t> b;
  CHECK(Base64Decode("aGVsbG8=", &b) && std::string(b.begin(), b.end()) == "hello", "base64");
  CHECK(!Base64Decode("he$$o", &b), "base64 rejects junk");

  CHECK(CompareVersions("1.10.0", "1.9.3") > 0, "1.10.0 > 1.9.3");
  CHECK(CompareVersions("1.0.0", "1.0") == 0, "1.0.0 == 1.0");
  CHECK(CompareVersions("0.9.9", "1.0.0") < 0, "0.9.9 < 1.0.0");

  const std::string good =
      "name=c01u-pro-studio\nversion=1.2.3\nfile=C01UProStudio-1.2.3.exe\n"
      "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nsize=2000000\n"
      "page=https://github.com/x/y/releases/tag/v1.2.3\nnotes_ru=Строка 1\\nСтрока 2\nnotes_en=Line\n";
  Manifest m;
  CHECK(ParseManifest(good, &m) && m.version == "1.2.3" && m.size == 2000000 && m.notesRu == "Строка 1\nСтрока 2",
        "release description parsed, \\n in notes");
  std::string bad = good;
  bad.replace(bad.find("C01UProStudio-1.2.3.exe"), 23, "..\\evil.exe");
  CHECK(!ParseManifest(bad, &m), "file name with a path is refused");
  bad = good;
  bad.replace(bad.find("C01UProStudio-1.2.3.exe"), 23, "C01UProStudio-1.2.3.dll");
  CHECK(!ParseManifest(bad, &m), "only an .exe installer is accepted");
  bad = good;
  bad.replace(bad.find("version=1.2.3"), 13, "version=1.2.3/../x");
  CHECK(!ParseManifest(bad, &m), "odd version text is refused");

  uint8_t sig[64], pub[64];
  CHECK(SignWithNewKey(good, sig, pub), "test key signs");
  CHECK(VerifySignature(good, sig, pub), "valid signature accepted");
  std::string forged = good;
  forged.replace(forged.find("size=2000000"), 12, "size=2000001");
  CHECK(!VerifySignature(forged, sig, pub), "changed text rejected");
  sig[10] ^= 1;
  CHECK(!VerifySignature(good, sig, pub), "changed signature rejected");
  CHECK(!VerifyRelease(good, "AAAA"), "short signature rejected");

  printf(g_failed ? "%d check(s) FAILED\n" : "all checks passed\n", g_failed);
  return g_failed ? 1 : 0;
}

static int Verify(const char* manifestPath, const char* sigPath) {
  std::string man, sig;
  if (!ReadAll(manifestPath, &man) || !ReadAll(sigPath, &sig)) {
    printf("cannot read files\n");
    return 2;
  }
  Manifest m;
  const bool sigOk = VerifyRelease(man, sig);
  const bool parsed = ParseManifest(man, &m);
  printf("signature: %s, description: %s\n", sigOk ? "valid" : "INVALID", parsed ? "ok" : "BAD");
  return sigOk && parsed ? 0 : 1;
}

static int Online(const std::string& repo) {
  const std::string base = "https://github.com/" + repo + "/releases/";
  const std::string ua = "C01U Pro Studio update test";
  HttpResult man = HttpGet(base + "latest/download/update.txt", 64 * 1024, ua);
  HttpResult sig = HttpGet(base + "latest/download/update.txt.sig", 4096, ua);
  printf("update.txt: %s, update.txt.sig: %s\n", man.ok ? "ok" : man.error.c_str(), sig.ok ? "ok" : sig.error.c_str());
  if (!man.ok || !sig.ok) return 1;
  const bool sigOk = VerifyRelease(man.body, sig.body);
  Manifest m;
  const bool parsed = ParseManifest(man.body, &m);
  printf("signature: %s, version %s, file %s\n", sigOk ? "valid" : "INVALID", m.version.c_str(), m.file.c_str());
  if (!sigOk || !parsed) return 1;
  HttpResult bin = HttpGet(base + "download/v" + m.version + "/" + m.file, 64 << 20, ua);
  uint8_t h[32];
  const bool sumOk = bin.ok && bin.body.size() == m.size && Sha256(bin.body.data(), bin.body.size(), h) &&
                     ToHex(h, 32) == m.sha256;
  printf("installer: %s, %zu bytes, SHA-256 %s\n", bin.ok ? "downloaded" : bin.error.c_str(), bin.body.size(),
         sumOk ? "matches" : "DOES NOT MATCH");
  return sumOk ? 0 : 1;
}

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc >= 4 && !strcmp(argv[1], "verify")) return Verify(argv[2], argv[3]);
  if (argc >= 3 && !strcmp(argv[1], "online")) return Online(argv[2]);
  return SelfTest();
}
