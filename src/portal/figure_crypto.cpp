#include "portal/figure_crypto.h"

#include <cstring>
#include <string_view>

#if defined(SKYLANDERS_PORTAL_CRYPTO_CNG)
// Without the SDK (skylanders_portal_figures): MD5 and AES-128-ECB from Windows' CNG.
#include <windows.h>
#include <bcrypt.h>
#elif defined(SKYLANDERS_PORTAL_CRYPTO_OPENSSL)
// Without the SDK on POSIX (skylanders_portal_figures): MD5 and AES-128-ECB from OpenSSL.
#include <memory>
#include <openssl/evp.h>
#else
extern "C" {
#include <aes.h>
}

// md5.h's own prototypes use a K&R-compatibility macro (__P) that collapses to an empty
// parameter list under C++ compilation, so its declaration of this function doesn't match its
// actual (C-linkage) definition -- declared directly here instead of including that header.
extern "C" void* md5_buffer(const char* buffer, size_t len, void* resblock);
#endif

namespace skylanders::portal {

namespace {

// The trailing space is part of the constant: dropping it silently produces a wrong key.
constexpr std::string_view kKeyConstant = " Copyright (C) 2010 Activision. All Rights Reserved. ";

std::array<uint8_t, 0x20 + 1 + kKeyConstant.size()> BlockKeyMaterial(const FigureData& data, uint8_t block_index) {
  std::array<uint8_t, 0x20 + 1 + kKeyConstant.size()> key_material{};
  std::memcpy(key_material.data(), data.data(), 0x20);
  key_material[0x20] = block_index;
  std::memcpy(key_material.data() + 0x21, kKeyConstant.data(), kKeyConstant.size());
  return key_material;
}

// Test seam: only the fault-injection test build (tests/figure_crypto_failure_test.cpp) defines
// SKYLANDERS_PORTAL_FIGURE_CRYPTO_TESTING; the library itself never does. It makes the provider
// report an error so the test can verify that failures propagate instead of becoming zero blocks.
#if defined(SKYLANDERS_PORTAL_FIGURE_CRYPTO_TESTING)
bool g_crypto_provider_failed = false;
#endif

#if defined(SKYLANDERS_PORTAL_CRYPTO_CNG) || defined(SKYLANDERS_PORTAL_CRYPTO_OPENSSL)
// The bundled tiny-AES/MD5 path cannot fail, so only the provider backends consult this.
bool CryptoProviderFailed() {
#if defined(SKYLANDERS_PORTAL_FIGURE_CRYPTO_TESTING)
  return g_crypto_provider_failed;
#else
  return false;
#endif
}
#endif

#if defined(SKYLANDERS_PORTAL_CRYPTO_CNG)

// The block's key: MD5 of the tag's first 0x20 bytes, the block index and the constant.
std::optional<std::array<uint8_t, 16>> BlockKey(const FigureData& data, uint8_t block_index) {
  if (CryptoProviderFailed()) return std::nullopt;
  const auto key_material = BlockKeyMaterial(data, block_index);
  std::array<uint8_t, 16> key{};
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) return std::nullopt;
  const NTSTATUS status = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(key_material.data()),
                                     ULONG(key_material.size()), key.data(), ULONG(key.size()));
  BCryptCloseAlgorithmProvider(alg, 0);
  if (!BCRYPT_SUCCESS(status)) return std::nullopt;
  return key;
}

// One 16-byte block through AES-128-ECB with `key`.
std::optional<FigureBlock> Aes(const std::array<uint8_t, 16>& key, const uint8_t* in, bool encrypt) {
  if (CryptoProviderFailed()) return std::nullopt;
  FigureBlock out{};
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return std::nullopt;
  if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                        sizeof(BCRYPT_CHAIN_MODE_ECB), 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return std::nullopt;
  }
  BCRYPT_KEY_HANDLE handle = nullptr;
  if (BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()), ULONG(key.size()), 0) !=
      0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return std::nullopt;
  }
  ULONG n = 0;
  uint8_t block[kBlockSize];
  std::memcpy(block, in, kBlockSize);
  NTSTATUS status;
  if (encrypt) {
    status = BCryptEncrypt(handle, block, kBlockSize, nullptr, nullptr, 0, out.data(), kBlockSize, &n, 0);
  } else {
    status = BCryptDecrypt(handle, block, kBlockSize, nullptr, nullptr, 0, out.data(), kBlockSize, &n, 0);
  }
  BCryptDestroyKey(handle);
  BCryptCloseAlgorithmProvider(alg, 0);
  if (!BCRYPT_SUCCESS(status) || n != kBlockSize) return std::nullopt;
  return out;
}

}  // namespace

std::optional<FigureBlock> DecryptFigureBlock(const FigureData& data, uint8_t block_index) {
  const auto key = BlockKey(data, block_index);
  if (!key) return std::nullopt;
  return Aes(*key, data.data() + block_index * kBlockSize, false);
}

std::optional<FigureBlock> EncryptFigureBlock(const FigureData& data, uint8_t block_index,
                                              const FigureBlock& plain) {
  const auto key = BlockKey(data, block_index);
  if (!key) return std::nullopt;
  return Aes(*key, plain.data(), true);
}

#elif defined(SKYLANDERS_PORTAL_CRYPTO_OPENSSL)

std::optional<std::array<uint8_t, 16>> BlockKey(const FigureData& data, uint8_t block_index) {
  if (CryptoProviderFailed()) return std::nullopt;
  const auto key_material = BlockKeyMaterial(data, block_index);
  std::array<uint8_t, 16> key{};
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  unsigned int md_len = 0;
  if (!md) return std::nullopt;
  if (EVP_DigestInit_ex(md.get(), EVP_md5(), nullptr) != 1) return std::nullopt;
  if (EVP_DigestUpdate(md.get(), key_material.data(), key_material.size()) != 1) return std::nullopt;
  if (EVP_DigestFinal_ex(md.get(), key.data(), &md_len) != 1) return std::nullopt;
  if (md_len != key.size()) return std::nullopt;
  return key;
}

std::optional<FigureBlock> Aes(const std::array<uint8_t, 16>& key, const uint8_t* in, bool encrypt) {
  if (CryptoProviderFailed()) return std::nullopt;
  FigureBlock out{};
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx) return std::nullopt;
  int len = 0;
  int final_len = 0;
  if (EVP_CipherInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr, encrypt ? 1 : 0) != 1) {
    return std::nullopt;
  }
  if (EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1) return std::nullopt;
  if (EVP_CipherUpdate(ctx.get(), out.data(), &len, in, int(kBlockSize)) != 1) return std::nullopt;
  if (EVP_CipherFinal_ex(ctx.get(), out.data() + len, &final_len) != 1) return std::nullopt;
  if (len + final_len != int(kBlockSize)) return std::nullopt;
  return out;
}

}  // namespace

std::optional<FigureBlock> DecryptFigureBlock(const FigureData& data, uint8_t block_index) {
  const auto key = BlockKey(data, block_index);
  if (!key) return std::nullopt;
  return Aes(*key, data.data() + block_index * kBlockSize, false);
}

std::optional<FigureBlock> EncryptFigureBlock(const FigureData& data, uint8_t block_index,
                                              const FigureBlock& plain) {
  const auto key = BlockKey(data, block_index);
  if (!key) return std::nullopt;
  return Aes(*key, plain.data(), true);
}

#else

std::array<uint8_t, AES_ROUND_KEY_SIZE> RoundKeys(const FigureData& data, uint8_t block_index) {
  const auto key_material = BlockKeyMaterial(data, block_index);

  std::array<uint8_t, 16> key{};
  md5_buffer(reinterpret_cast<const char*>(key_material.data()), key_material.size(), key.data());

  std::array<uint8_t, AES_ROUND_KEY_SIZE> round_keys{};
  aes_key_schedule_128(key.data(), round_keys.data());
  return round_keys;
}

}  // namespace

std::optional<FigureBlock> DecryptFigureBlock(const FigureData& data, uint8_t block_index) {
  const auto round_keys = RoundKeys(data, block_index);
  FigureBlock out{};
  aes_decrypt_128(round_keys.data(), data.data() + block_index * kBlockSize, out.data());
  return out;
}

std::optional<FigureBlock> EncryptFigureBlock(const FigureData& data, uint8_t block_index,
                                              const FigureBlock& plain) {
  const auto round_keys = RoundKeys(data, block_index);
  FigureBlock out{};
  aes_encrypt_128(round_keys.data(), plain.data(), out.data());
  return out;
}

#endif

#if defined(SKYLANDERS_PORTAL_FIGURE_CRYPTO_TESTING)
// Fault-injection test build only (see the seam above).
void SetFigureCryptoProviderFailureForTesting(bool failed) { g_crypto_provider_failed = failed; }
#endif

uint16_t FigureCrc16(const uint8_t* data, size_t size) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                            : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace skylanders::portal
