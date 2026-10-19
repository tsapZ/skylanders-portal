#include "portal/figure_stats.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "portal/figure_crypto.h"

namespace skylanders::portal {

namespace {

using Block = FigureBlock;

// Offsets from an area's first block; each block is 16 bytes and every fourth block (a sector
// trailer) holds no save data, so an area spans blocks 0..6 of its start.
//   +0x00 u24 XP (Spyro's Adventure part, up to 33000)  +0x03 u16 gold  +0x05 u32 play time (s)
//   +0x09 u8 sequence  +0x0E u16 header CRC
//   +0x10 u16 upgrades  +0x13 u8 platforms  +0x14 u8 hat (Spyro's Adventure)
//   +0x16 bit 0: the second area pair's XP counts
//   +0x20, +0x40 nickname, UTF-16LE, 8 characters each
//   +0x50 last placed: u8 minute, hour, day, month, u16 year  +0x5A u16 hero points
// Second pair, from its first block:
//   +0x02 u8 sequence  +0x03 u16 XP (Giants part, up to 63500)  +0x05 u8 hat (Giants)
//   +0x08 u32 XP (Swap Force part)  +0x0C u8 hat (Swap Force, Trap Team)
//   +0x0E u8 hat (SuperChargers, its number minus 255)
constexpr uint8_t kAreas[2] = {0x08, 0x24};
constexpr uint8_t kExtraAreas[2] = {0x11, 0x2D};

uint16_t ReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadU32LE(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool RawBlockEmpty(const FigureData& data, uint8_t block) {
  const uint8_t* p = data.data() + size_t(block) * kBlockSize;
  return std::all_of(p, p + kBlockSize, [](uint8_t b) { return b == 0; });
}

// The bytes of an area from its first block on (16 bytes per block, 0x70 in all), decrypted.
std::array<uint8_t, 0x70> ReadArea(const FigureData& data, uint8_t start) {
  std::array<uint8_t, 0x70> area{};
  for (uint8_t i = 0; i < 7; ++i) {
    const Block block = DecryptFigureBlock(data, uint8_t(start + i));
    std::memcpy(area.data() + i * kBlockSize, block.data(), kBlockSize);
  }
  return area;
}

bool HeaderValid(const FigureData& data, uint8_t start) {
  if (RawBlockEmpty(data, start)) return false;
  Block header = DecryptFigureBlock(data, start);
  const uint16_t stored = ReadU16LE(&header[0x0E]);
  header[0x0E] = 0x05;
  header[0x0F] = 0x00;
  return stored == FigureCrc16(header.data(), header.size());
}

// Of two copies, the one whose sequence byte is one more than the other's is the newer.
int Newer(uint8_t sequence0, uint8_t sequence1) {
  return uint8_t(sequence0 + 1) == sequence1 ? 1 : 0;
}

// The Swap Force level table (supersedes earlier games' lower per-tier caps); total XP is the sum
// of every generation's part once the figure has been in a game new enough to add it.
constexpr std::array<uint32_t, 20> kLevelXpTable = {
    0,      1000,   2200,   3800,   6000,   9000,   13000,  18200,  24800,  33000,
    42700,  53900,  66600,  80800,  96500,  113700, 132400, 152600, 174300, 197500,
};

uint8_t LevelForXp(uint32_t total_xp) {
  uint8_t level = 1;
  for (size_t i = 0; i < kLevelXpTable.size(); ++i) {
    if (total_xp >= kLevelXpTable[i]) level = static_cast<uint8_t>(i + 1);
  }
  return level;
}

std::string Utf16ToUtf8(const uint8_t* p, size_t max_chars) {
  std::string out;
  for (size_t i = 0; i < max_chars; ++i) {
    const char16_t c = static_cast<char16_t>(ReadU16LE(p + 2 * i));
    if (c == 0) break;
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

}  // namespace

int UpgradeCount(const FigureStats& stats) { return std::popcount(uint16_t(stats.upgrades & ~3u)); }

int UpgradePath(const FigureStats& stats) {
  if (!(stats.upgrades & 1)) return 0;
  return (stats.upgrades & 2) ? 2 : 1;
}

std::optional<FigureStats> ParseFigureStats(const FigureData& data) {
  const bool valid0 = HeaderValid(data, kAreas[0]);
  const bool valid1 = HeaderValid(data, kAreas[1]);
  if (!valid0 && !valid1) return std::nullopt;
  int which = valid0 ? 0 : 1;
  if (valid0 && valid1) {
    which = Newer(DecryptFigureBlock(data, kAreas[0])[0x09],
                  DecryptFigureBlock(data, kAreas[1])[0x09]);
  }
  const auto area = ReadArea(data, kAreas[which]);

  FigureStats stats{};
  stats.gold = ReadU16LE(&area[0x03]);
  stats.play_seconds = ReadU32LE(&area[0x05]);
  stats.upgrades = ReadU16LE(&area[0x10]);
  stats.platforms = area[0x13];
  stats.hat = area[0x14];
  stats.hero_points = ReadU16LE(&area[0x5A]);
  // The nickname's two halves sit in blocks +2 and +4 (block +3 is the sector trailer).
  std::array<uint8_t, 0x20> nick{};
  std::memcpy(nick.data(), &area[0x20], 0x10);
  std::memcpy(nick.data() + 0x10, &area[0x40], 0x10);
  stats.nickname = Utf16ToUtf8(nick.data(), 16);
  const FigureDate date{ReadU16LE(&area[0x54]), area[0x53], area[0x52], area[0x51], area[0x50]};
  if (date.year >= 2011 && date.year < 2100 && date.month >= 1 && date.month <= 12 &&
      date.day >= 1 && date.day <= 31 && date.hour < 24 && date.minute < 60) {
    stats.last_played = date;
  }

  uint32_t xp = uint32_t(area[0x00]) | (uint32_t(area[0x01]) << 8) | (uint32_t(area[0x02]) << 16);
  // The second pair has its own sequence; an empty copy (raw zeros) never counts.
  const bool extra0 = !RawBlockEmpty(data, kExtraAreas[0]);
  const bool extra1 = !RawBlockEmpty(data, kExtraAreas[1]);
  if (extra0 || extra1) {
    const Block e0 = DecryptFigureBlock(data, kExtraAreas[0]);
    const Block e1 = DecryptFigureBlock(data, kExtraAreas[1]);
    const Block& extra = !extra0 ? e1 : !extra1 ? e0 : (Newer(e0[0x02], e1[0x02]) ? e1 : e0);
    if (area[0x16] & 1) xp += ReadU16LE(&extra[0x03]) + ReadU32LE(&extra[0x08]);
    // The newest game's hat slot that is set wins.
    if (extra[0x0E]) {
      stats.hat = uint16_t(extra[0x0E] + 255);
    } else if (extra[0x0C]) {
      stats.hat = extra[0x0C];
    } else if (extra[0x05]) {
      stats.hat = extra[0x05];
    }
  }
  stats.xp = xp;
  stats.level = LevelForXp(xp);
  stats.level_xp = kLevelXpTable[stats.level - 1];
  stats.next_xp = stats.level < kLevelXpTable.size() ? kLevelXpTable[stats.level] : 0;
  return stats;
}

}  // namespace skylanders::portal
