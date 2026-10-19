#include <array>
#include <cstring>

#include "portal/figure_crypto.h"
#include "portal/figure_file.h"
#include "portal/figure_stats.h"
#include "test_util.h"

using namespace skylanders::portal;

namespace {

using Area = std::array<uint8_t, 0x70>;

void Put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

void Put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

// Writes a save area as the game would: header CRC filled in, every block but the sector trailer
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
    const FigureBlock enc = EncryptFigureBlock(d, block, plain);
    std::memcpy(d.data() + size_t(block) * kBlockSize, enc.data(), kBlockSize);
  }
}

void WriteBlock(FigureData& d, uint8_t block, const FigureBlock& plain) {
  const FigureBlock enc = EncryptFigureBlock(d, block, plain);
  std::memcpy(d.data() + size_t(block) * kBlockSize, enc.data(), kBlockSize);
}

Area MakeArea(uint32_t xp, uint16_t gold, uint32_t play, uint8_t sequence) {
  Area a{};
  a[0x00] = uint8_t(xp);
  a[0x01] = uint8_t(xp >> 8);
  a[0x02] = uint8_t(xp >> 16);
  Put16(&a[0x03], gold);
  Put32(&a[0x05], play);
  a[0x09] = sequence;
  a[0x13] = 0x02;  // Xbox 360
  return a;
}

}  // namespace

int main() {
  // A figure with no save data at all (e.g. --portal_test_figure) has nothing to report.
  {
    FigureData d{};
    CHECK(!ParseFigureStats(d).has_value());
  }

  // A freshly created blank figure (real id/variant/CRC, but never played -- its save-data
  // region is still zero) also has no stats yet, not zeros presented as real data.
  {
    FigureData d = CreateBlankFigure(112, 0, {0x11, 0x22, 0x33, 0x44});
    CHECK(!ParseFigureStats(d).has_value());
  }

  // Its first save goes to the second area; the first stays empty and must not hide it.
  {
    FigureData d = CreateBlankFigure(30, 0, {0x01, 0x02, 0x03, 0x04});
    Area a = MakeArea(5614, 455, 3151, 1);
    a[0x10] = 0x08;  // one upgrade
    const char16_t nick[] = u"Chopper";
    for (size_t i = 0; nick[i]; ++i) Put16(&a[0x20 + 2 * i], uint16_t(nick[i]));
    a[0x50] = 49;
    a[0x51] = 11;
    a[0x52] = 18;
    a[0x53] = 10;
    Put16(&a[0x54], 2026);
    Put16(&a[0x5A], 7);
    WriteArea(d, 0x24, a);
    const auto stats = ParseFigureStats(d);
    REQUIRE_OR_RETURN(stats.has_value());
    CHECK(stats->xp == 5614);
    CHECK(stats->level == 4);  // 3800 <= 5614 < 6000
    CHECK(stats->level_xp == 3800);
    CHECK(stats->next_xp == 6000);
    CHECK(stats->gold == 455);
    CHECK(stats->play_seconds == 3151);
    CHECK(stats->nickname == "Chopper");
    CHECK(stats->hero_points == 7);
    CHECK(stats->platforms == 0x02);
    CHECK(UpgradeCount(*stats) == 1);
    CHECK(UpgradePath(*stats) == 0);
    REQUIRE_OR_RETURN(stats->last_played.has_value());
    CHECK(stats->last_played->year == 2026 && stats->last_played->month == 10 &&
          stats->last_played->day == 18 && stats->last_played->hour == 11 &&
          stats->last_played->minute == 49);
  }

  // Both areas written: the one whose sequence is one more is the newer, also across 255 -> 0.
  {
    const struct {
      uint8_t seq0, seq1;
      uint16_t want_gold;
    } cases[] = {{4, 5, 2}, {94, 93, 1}, {255, 0, 2}, {0, 255, 1}};
    for (const auto& c : cases) {
      FigureData d = CreateBlankFigure(7, 0, {0x09, 0x08, 0x07, 0x06});
      WriteArea(d, 0x08, MakeArea(100, 1, 10, c.seq0));
      WriteArea(d, 0x24, MakeArea(200, 2, 20, c.seq1));
      const auto stats = ParseFigureStats(d);
      REQUIRE_OR_RETURN(stats.has_value());
      CHECK(stats->gold == c.want_gold);
    }
  }

  // An area whose header CRC doesn't match is ignored even if its sequence says newer.
  {
    FigureData d = CreateBlankFigure(7, 0, {0x05, 0x05, 0x05, 0x05});
    WriteArea(d, 0x08, MakeArea(100, 1, 10, 4));
    WriteArea(d, 0x24, MakeArea(200, 2, 20, 5));
    d[0x24 * kBlockSize + 3] ^= 0xFF;  // corrupt the second header
    const auto stats = ParseFigureStats(d);
    REQUIRE_OR_RETURN(stats.has_value());
    CHECK(stats->gold == 1);
  }

  // Giants and Swap Force add their parts of the XP in the second pair of areas, counted only
  // with the flag at +0x16; the newest game's hat slot wins; path bits.
  {
    FigureData d = CreateBlankFigure(2008, 0, {0x0A, 0x0B, 0x0C, 0x0D});
    Area a = MakeArea(33000, 0, 0, 1);
    a[0x14] = 14;    // a Spyro's Adventure hat
    a[0x16] = 0x01;  // later games' XP counts
    Put16(&a[0x10], 0x03FD);  // first path, all upgrades
    WriteArea(d, 0x08, a);
    FigureBlock extra{};
    extra[0x02] = 1;
    Put16(&extra[0x03], 63500);
    Put32(&extra[0x08], 101000);
    extra[0x0C] = 22;  // a Swap Force hat
    WriteBlock(d, 0x11, extra);
    auto stats = ParseFigureStats(d);
    REQUIRE_OR_RETURN(stats.has_value());
    CHECK(stats->xp == 197500);
    CHECK(stats->level == 20);
    CHECK(stats->next_xp == 0);
    CHECK(stats->hat == 22);
    CHECK(UpgradePath(*stats) == 1);
    CHECK(UpgradeCount(*stats) == 8);  // 0x03FC: bits 2-9

    a[0x16] = 0x00;  // reset by an older game: only the first part counts
    WriteArea(d, 0x08, a);
    stats = ParseFigureStats(d);
    REQUIRE_OR_RETURN(stats.has_value());
    CHECK(stats->xp == 33000);
    CHECK(stats->level == 10);
  }

  return Finish("figure_stats");
}
