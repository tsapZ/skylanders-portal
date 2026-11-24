#include "portal/trap_villain.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "portal/figure_catalog.h"
#include "portal/figure_crypto.h"
#include "portal/figure_file.h"

// Trap save data layout. Documented by the GPL-3 Modified_SkyEditGUI trap editor
// (github.com/brianccwork/Modified_SkyEditGUI), read for these facts only, no code copied, and
// checked against real trap dumps:
//
// A trap has two save areas, each the 21 data blocks of 7 sectors (area 0: blocks 8-35, area 1:
// blocks 36-63, skipping every sector's trailer block), 336 bytes decrypted. The game writes the
// area it didn't write last; byte 9 is a sequence number, and the valid area with the newer one is
// current. In that area:
//   [0]      1 if the current villain is its variant form, else 0
//   [1]      how many different villains the trap has held
//   [7]      the villain id again when [0] is 1, else 0
//   [9]      sequence number
//   [10..11] CRC-16 of bytes 64..335 (villains held before, little-endian)
//   [12..13] CRC-16 of bytes 16..63 (the current villain's record)
//   [14..15] CRC-16 of bytes 0..15 with [14..15] taken as 05 00
//   [16]     current villain id (0: empty)
//   [17]     1 if the villain is evolved

namespace skylanders::portal {

namespace {

constexpr std::array<TrapVillain, 52> kVillains = {{
    {1, "Chompy Mage", 217, false},
    {2, "Dr. Krankcase", 214, false},
    {3, "Wolfgang", 213, false},
    {4, "Chef Pepper Jack", 215, false},
    {5, "Nightshade", 218, false},
    {6, "Luminous", 219, false},
    {7, "Golden Queen", 216, false},
    {8, "Dreamcatcher", 212, false},
    {9, "Gulper", 211, false},
    {10, "Kaos", 220, false},
    {11, "Cuckoo Clocker", 217, false},
    {12, "Buzzer Beak", 212, false},
    {13, "Shield Shredder", 217, false},
    {13, "Riot Shield Shredder", 217, true},
    {14, "Cross Crow", 211, false},
    {15, "Bone Chompy", 213, false},
    {16, "Brawl and Chain", 211, false},
    {16, "Outlaw Brawl and Chain", 211, true},
    {17, "Bomb Shell", 210, false},
    {18, "Masker Mind", 213, false},
    {19, "Chill Bill", 211, false},
    {20, "Sheep Creep", 217, false},
    {21, "Shrednaught", 214, false},
    {21, "Steampunk Shrednaught", 214, true},
    {22, "Chomp Chest", 216, false},
    {23, "Broccoli Guy", 217, false},
    {23, "Steamed Broccoli Guy", 217, true},
    {24, "Rage Mage", 210, false},
    {25, "Lob Goblin", 219, false},
    {25, "Rebel Lob Goblin", 219, true},
    {26, "Chompy", 217, false},
    {27, "Fisticuffs", 218, false},
    {28, "Trolling Thunder", 214, false},
    {29, "Hood Sickle", 213, false},
    {30, "Bruiser Cruiser", 214, false},
    {31, "Brawlrus", 214, false},
    {32, "Tussle Sprout", 216, false},
    {32, "Red Hot Tussle Sprout", 216, true},
    {33, "Krankenstein", 212, false},
    {34, "Scrap Shooter", 215, false},
    {35, "Slobber Trap", 211, false},
    {36, "Grinnade", 215, false},
    {37, "Bad Juju", 212, false},
    {38, "Blaster-Tron", 219, false},
    {39, "Tae Kwon Crow", 218, false},
    {40, "Pain-Yatta", 210, false},
    {41, "Smoke Scream", 215, false},
    {42, "Eye Five", 219, false},
    {43, "Grave Clobber", 216, false},
    {44, "Threatpack", 211, false},
    {45, "Mab Lobs", 214, false},
    {46, "Eye Scream", 218, false},
}};

constexpr uint16_t kFirstTrapId = 210;
constexpr uint16_t kLastTrapId = 220;
constexpr size_t kAreaBlocks = 21;
constexpr size_t kAreaSize = kAreaBlocks * kBlockSize;  // 336
using Area = std::array<uint8_t, kAreaSize>;

std::array<uint8_t, kAreaBlocks> AreaBlockIndices(int area) {
  std::array<uint8_t, kAreaBlocks> out{};
  size_t n = 0;
  for (int b = area == 0 ? 8 : 36; n < kAreaBlocks; ++b) {
    if (b % 4 != 3) out[n++] = static_cast<uint8_t>(b);
  }
  return out;
}

std::optional<Area> DecryptArea(const FigureData& data, int area) {
  Area out{};
  const auto blocks = AreaBlockIndices(area);
  for (size_t i = 0; i < kAreaBlocks; ++i) {
    const auto b = DecryptFigureBlock(data, blocks[i]);
    if (!b) return std::nullopt;
    std::memcpy(out.data() + i * kBlockSize, b->data(), kBlockSize);
  }
  return out;
}

bool AreaIsBlank(const FigureData& data, int area) {
  for (uint8_t b : AreaBlockIndices(area)) {
    const uint8_t* p = data.data() + b * kBlockSize;
    if (std::any_of(p, p + kBlockSize, [](uint8_t v) { return v != 0; })) return false;
  }
  return true;
}

uint16_t ReadU16(const Area& a, size_t at) { return uint16_t(a[at] | (a[at + 1] << 8)); }
void WriteU16(Area& a, size_t at, uint16_t v) {
  a[at] = uint8_t(v);
  a[at + 1] = uint8_t(v >> 8);
}

uint16_t HeaderCrc(const Area& a) {
  std::array<uint8_t, kBlockSize> header{};
  std::memcpy(header.data(), a.data(), kBlockSize);
  header[14] = 0x05;
  header[15] = 0x00;
  return FigureCrc16(header.data(), header.size());
}

bool AreaValid(const Area& a) {
  return ReadU16(a, 14) == HeaderCrc(a) && ReadU16(a, 12) == FigureCrc16(a.data() + 16, 48) &&
         ReadU16(a, 10) == FigureCrc16(a.data() + 64, kAreaSize - 64);
}

bool IsTrap(uint16_t id) { return id >= kFirstTrapId && id <= kLastTrapId; }

}  // namespace

std::span<const TrapVillain> AllTrapVillains() { return kVillains; }

const TrapVillain* FindTrapVillain(uint8_t id, bool variant) {
  for (const TrapVillain& v : kVillains) {
    if (v.id == id && v.variant == variant) return &v;
  }
  return nullptr;
}

std::optional<TrapContents> ReadTrapVillain(const FigureData& data) {
  if (!IsTrap(ReadFigureId(data))) return std::nullopt;
  if (AreaIsBlank(data, 0) && AreaIsBlank(data, 1)) return TrapContents{0, false, false};

  const auto a = DecryptArea(data, 0);
  const auto b = DecryptArea(data, 1);
  if (!a || !b) return std::nullopt;  // provider failure: no usable save data
  const bool good_a = AreaValid(*a);
  const bool good_b = AreaValid(*b);
  if (!good_a && !good_b) return std::nullopt;
  // Area b is current when it's the only valid one, or its sequence number follows a's.
  const bool use_b = good_b && (!good_a || uint8_t((*a)[9] + 1) == (*b)[9]);
  const Area& area = use_b ? *b : *a;

  TrapContents c{};
  c.villain_id = area[16];
  c.variant = area[0] == 1 && area[7] == area[16];
  c.evolved = area[17] == 1;
  return c;
}

std::optional<FigureData> CreateTrapWithVillain(uint16_t trap_id, uint16_t trap_variant,
                                                const TrapVillain& villain, bool evolved) {
  if (!IsTrap(trap_id) || villain.trap_id != trap_id) return std::nullopt;
  FigureData data = CreateBlankFigure(trap_id, trap_variant);

  Area area{};
  area[0] = villain.variant ? 1 : 0;
  area[1] = 1;  // one villain held so far
  area[7] = villain.variant ? villain.id : 0;
  area[9] = 1;  // sequence number
  area[16] = villain.id;
  area[17] = evolved ? 1 : 0;
  WriteU16(area, 10, FigureCrc16(area.data() + 64, kAreaSize - 64));
  WriteU16(area, 12, FigureCrc16(area.data() + 16, 48));
  WriteU16(area, 14, HeaderCrc(area));

  // Written to area 0; area 1 stays blank, as on a trap that has only been saved once.
  const auto blocks = AreaBlockIndices(0);
  for (size_t i = 0; i < kAreaBlocks; ++i) {
    FigureBlock plain{};
    std::memcpy(plain.data(), area.data() + i * kBlockSize, kBlockSize);
    const auto enc = EncryptFigureBlock(data, blocks[i], plain);
    if (!enc) return std::nullopt;  // provider failure: no half-filled trap
    std::memcpy(data.data() + blocks[i] * kBlockSize, enc->data(), kBlockSize);
  }
  return data;
}

uint16_t DefaultTrapVariant(uint16_t trap_id) {
  // Regular crystal trap shapes have variants 0x30xx; legendary and special editions use others.
  for (const SkylanderInfo& s : AllSkylanders()) {
    if (s.id == trap_id && (s.variant >> 8) == 0x30) return s.variant;
  }
  return 0;
}

}  // namespace skylanders::portal
