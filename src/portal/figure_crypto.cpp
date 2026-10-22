#include "portal/figure_crypto.h"

#include <cstring>
#include <string_view>

#if defined(SKYLANDERS_PORTAL_CRYPTO_CNG)
// Without the SDK (skylanders_portal_figures): MD5 and AES-128-ECB from Windows' CNG.
#include <windows.h>
#include <bcrypt.h>
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

#if defined(SKYLANDERS_PORTAL_CRYPTO_CNG)

// The block's key: MD5 of the tag's first 0x20 bytes, the block index and the constant.
std::array<uint8_t, 16> BlockKey(const FigureData& data, uint8_t block_index) {
  std::array<uint8_t, 0x20 + 1 + kKeyConstant.size()> key_material{};
  std::memcpy(key_material.data(), data.data(), 0x20);
  key_material[0x20] = block_index;
  std::memcpy(key_material.data() + 0x21, kKeyConstant.data(), kKeyConstant.size());
  std::array<uint8_t, 16> key{};
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0) {
    BCryptHash(alg, nullptr, 0, key_material.data(), ULONG(key_material.size()), key.data(), ULONG(key.size()));
    BCryptCloseAlgorithmProvider(alg, 0);
  }
  return key;
}

// One 16-byte block through AES-128-ECB with `key`.
FigureBlock Aes(const std::array<uint8_t, 16>& key, const uint8_t* in, bool encrypt) {
  FigureBlock out{};
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return out;
  BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                    sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
  BCRYPT_KEY_HANDLE handle = nullptr;
  if (BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()), ULONG(key.size()), 0) ==
      0) {
    ULONG n = 0;
    uint8_t block[kBlockSize];
    std::memcpy(block, in, kBlockSize);
    if (encrypt) {
      BCryptEncrypt(handle, block, kBlockSize, nullptr, nullptr, 0, out.data(), kBlockSize, &n, 0);
    } else {
      BCryptDecrypt(handle, block, kBlockSize, nullptr, nullptr, 0, out.data(), kBlockSize, &n, 0);
    }
    BCryptDestroyKey(handle);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

}  // namespace

FigureBlock DecryptFigureBlock(const FigureData& data, uint8_t block_index) {
  return Aes(BlockKey(data, block_index), data.data() + block_index * kBlockSize, false);
}

FigureBlock EncryptFigureBlock(const FigureData& data, uint8_t block_index, const FigureBlock& plain) {
  return Aes(BlockKey(data, block_index), plain.data(), true);
}

#else

std::array<uint8_t, AES_ROUND_KEY_SIZE> RoundKeys(const FigureData& data, uint8_t block_index) {
  std::array<uint8_t, 0x20 + 1 + kKeyConstant.size()> key_material{};
  std::memcpy(key_material.data(), data.data(), 0x20);
  key_material[0x20] = block_index;
  std::memcpy(key_material.data() + 0x21, kKeyConstant.data(), kKeyConstant.size());

  std::array<uint8_t, 16> key{};
  md5_buffer(reinterpret_cast<const char*>(key_material.data()), key_material.size(), key.data());

  std::array<uint8_t, AES_ROUND_KEY_SIZE> round_keys{};
  aes_key_schedule_128(key.data(), round_keys.data());
  return round_keys;
}

}  // namespace

FigureBlock DecryptFigureBlock(const FigureData& data, uint8_t block_index) {
  const auto round_keys = RoundKeys(data, block_index);
  FigureBlock out{};
  aes_decrypt_128(round_keys.data(), data.data() + block_index * kBlockSize, out.data());
  return out;
}

FigureBlock EncryptFigureBlock(const FigureData& data, uint8_t block_index, const FigureBlock& plain) {
  const auto round_keys = RoundKeys(data, block_index);
  FigureBlock out{};
  aes_encrypt_128(round_keys.data(), plain.data(), out.data());
  return out;
}

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
