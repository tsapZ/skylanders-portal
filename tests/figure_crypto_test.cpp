#include "portal/figure_crypto.h"
#include "test_util.h"

using namespace skylanders::portal;

int main() {
  FigureData tag{};
  for (size_t i = 0; i < 0x20; ++i) tag[i] = static_cast<uint8_t>(i * 11 + 3);

  const FigureBlock plain = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                             0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};

  // Round-trip encrypt then decrypt across multiple sectors/blocks.
  for (uint8_t block : {uint8_t(8), uint8_t(9), uint8_t(12), uint8_t(36), uint8_t(62)}) {
    const auto enc = EncryptFigureBlock(tag, block, plain);
    REQUIRE_OR_RETURN(enc.has_value());
    CHECK(*enc != plain);
    FigureData with_block = tag;
    for (size_t i = 0; i < kBlockSize; ++i) with_block[block * kBlockSize + i] = (*enc)[i];
    const auto dec = DecryptFigureBlock(with_block, block);
    REQUIRE_OR_RETURN(dec.has_value());
    CHECK(*dec == plain);
  }

  // Known MD5+AES-128-ECB ciphertext vector for block 8 of the synthetic header above:
  // must match bit-for-bit across tiny-aes, CNG, and OpenSSL.
  const auto enc8 = EncryptFigureBlock(tag, 8, plain);
  REQUIRE_OR_RETURN(enc8.has_value());
  const FigureBlock expected8 = {0x56, 0x3A, 0xCF, 0x19, 0x25, 0xAA, 0x55, 0x4B,
                                 0x83, 0x79, 0xBE, 0x7C, 0x6F, 0x29, 0x9E, 0x57};
  if (*enc8 != expected8) {
    std::fprintf(stderr, "enc8 =");
    for (uint8_t b : *enc8) std::fprintf(stderr, " 0x%02X,", b);
    std::fprintf(stderr, "\n");
  }
  CHECK(*enc8 == expected8);

  // Different blocks with the same plaintext must produce distinct ciphertexts (per-block MD5 key).
  const auto enc9 = EncryptFigureBlock(tag, 9, plain);
  REQUIRE_OR_RETURN(enc9.has_value());
  CHECK(*enc8 != *enc9);

  // Changing a header byte changes the derived key.
  FigureData other_tag = tag;
  other_tag[0x10] ^= 0x01;
  const auto other_enc = EncryptFigureBlock(other_tag, 8, plain);
  REQUIRE_OR_RETURN(other_enc.has_value());
  CHECK(*other_enc != *enc8);

  return Finish("figure_crypto");
}
