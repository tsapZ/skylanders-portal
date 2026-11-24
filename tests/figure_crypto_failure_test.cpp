#include <array>
#include <cstring>

#include "portal/figure_crypto.h"
#include "portal/figure_file.h"
#include "portal/figure_stats.h"
#include "test_util.h"

namespace skylanders::portal {
// Defined by figure_crypto.cpp only in the fault-injection test build
// (SKYLANDERS_PORTAL_FIGURE_CRYPTO_TESTING).
void SetFigureCryptoProviderFailureForTesting(bool failed);
}  // namespace skylanders::portal

using namespace skylanders::portal;

namespace {

using Area = std::array<uint8_t, 0x70>;

void Put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

// A save area as the game writes it: header CRC filled in, every block but the sector trailer
// encrypted with its own key.
void WriteArea(FigureData& d, uint8_t start, Area area) {
  FigureBlock header{};
  std::memcpy(header.data(), area.data(), kBlockSize);
  header[0x0E] = 0x05;
  header[0x0F] = 0x00;
  Put16(&area[0x0E], FigureCrc16(header.data(), header.size()));
  for (uint8_t i = 0; i < 7; ++i) {
    const uint8_t block = uint8_t(start + i);
    if ((block + 1) % 4 == 0) continue;
    FigureBlock plain{};
    std::memcpy(plain.data(), area.data() + i * kBlockSize, kBlockSize);
    const auto enc = EncryptFigureBlock(d, block, plain);
    CHECK(enc.has_value());
    if (!enc) return;
    std::memcpy(d.data() + size_t(block) * kBlockSize, enc->data(), kBlockSize);
  }
}

}  // namespace

int main() {
  FigureData tag{};
  for (size_t i = 0; i < 0x20; ++i) tag[i] = static_cast<uint8_t>(i * 13 + 5);
  const FigureBlock plain = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                             0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};

  // A provider failure must be reported, not turned into a zero key or an all-zero block that a
  // caller would then treat as real save data.
  SetFigureCryptoProviderFailureForTesting(true);
  CHECK(!DecryptFigureBlock(tag, 8).has_value());
  CHECK(!EncryptFigureBlock(tag, 8, plain).has_value());

  // A figure with both save areas valid (made while the provider works).
  SetFigureCryptoProviderFailureForTesting(false);
  FigureData figure = CreateBlankFigure(30, 0, {0x01, 0x02, 0x03, 0x04});
  Area area0{};
  area0[0x09] = 1;
  Put16(&area0[0x03], 455);
  WriteArea(figure, 0x08, area0);
  Area area1{};
  area1[0x09] = 2;
  Put16(&area1[0x03], 300);
  WriteArea(figure, 0x24, area1);
  REQUIRE_OR_RETURN(ParseFigureStats(figure).has_value());

  // Under a provider failure the readers and the mutator refuse, and the figure is not touched.
  const FigureData before = figure;
  SetFigureCryptoProviderFailureForTesting(true);
  CHECK(!ParseFigureStats(figure).has_value());
  CHECK(!SetFigureGold(figure, kMaxFigureGold));
  CHECK(figure == before);

  // Once the provider works again, the same operations succeed.
  SetFigureCryptoProviderFailureForTesting(false);
  CHECK(SetFigureGold(figure, kMaxFigureGold));
  const auto stats = ParseFigureStats(figure);
  REQUIRE_OR_RETURN(stats.has_value());
  CHECK(stats->gold == kMaxFigureGold);

  return Finish("figure_crypto_failure");
}
