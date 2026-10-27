#pragma once

// Reader for Alchemy IGZ version 8 (Trap Team's scenes and textures), big-endian, 32-bit, as far
// as the game's pictures need it. The layout is Swap Force's version 7 (igz_file.h, whose
// reader already allows for version 8's 2-byte aligned strings):
//   0x00 'IGZ\x01'  0x04 version  0x10 fixup count
//   0x14 section descriptors, 16 bytes each until offset 0: (pool name, offset, length, align);
//        section 0 holds the fixup records, the others are the memory pools objects live in.
//   Fixup record: fourcc (stored byte-reversed), count, length, data start, data. Used here: TMET
//     type names, EXID external ids (name hash, namespace hash; the texture formats), TMHN memory
//     handles (u32 size | flags << 28, u32 pointer), RVTB packed offsets of every object.
//   A serialized pointer is pool = value >> 27, offset = value & 0x07FFFFFF; pool n is section
//   n + 1.
//   igImage2 has a pointer to its name at +0x08, the source path such as
//   "ui_sky4/hud/portraits/2014/256x128/target_2/ssa_whirlwind_portrait.png" (as in Giants'
//   version 6, src/alchemy/igz6_file.h), +0x30 u16 width and height, +0x3C the
//   format (0x80000000 | EXID index, whose name hash is FNV-1a of the format name), +0x48 the TMHN
//   index of its pixels.
// Worked out on Trap Team's character scenes (2026-10-21): Whirlwind's four HUD portraits decode.
//
// The reader works through a callback, so a scene inside an archive is read only where it is
// needed (Iga11Archive decodes just those blocks).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "alchemy/xenos_dxt.h"

namespace skylanders::alchemy {

class Igz8File {
 public:
  // Copies [offset, offset + size) of the IGZ file to `out`; false if it can't.
  using Reader = std::function<bool(uint64_t offset, size_t size, uint8_t* out)>;

  static std::optional<Igz8File> Parse(Reader reader, uint64_t length);

  struct Image {
    std::string name;  // the source path
    uint64_t object = 0;
    int width = 0;
    int height = 0;
  };
  // Every igImage2 in the file.
  std::vector<Image> Images() const;
  // nullopt for an unknown format or pixels that can't be read.
  std::optional<RgbaImage> Decode(const Image& image) const;

  // For other objects (the bitmap font, src/game/game_fonts.cpp): every object of a class, a u32
  // field, a serialized pointer as a file offset, an image's pixels as stored, and raw bytes.
  std::vector<uint64_t> Objects(std::string_view type) const;
  uint32_t Field(uint64_t object, uint32_t offset) const { return Be32(object + offset); }
  std::optional<uint64_t> Pointer(uint32_t value) const { return Deserialize(value); }
  // The NUL-terminated string a pointer field points to (empty if none), and the bytes of a
  // memory reference field (u32 size | flags << 28, then the pointer), as the scenes' texts need
  // them (src/game/toy_hats.cpp).
  std::string StringField(uint64_t object, uint32_t field) const;
  std::optional<std::vector<uint8_t>> MemoryField(uint64_t object, uint32_t size_field) const;
  struct RawPixels {
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t format_hash = 0;  // FNV-1a of the format's name
  };
  std::optional<RawPixels> Raw(const Image& image) const;
  bool Read(uint64_t offset, size_t size, uint8_t* out) const { return reader_(offset, size, out); }

 private:
  struct Fixup {
    uint32_t count = 0;
    uint64_t start = 0;
    uint64_t end = 0;
  };

  uint32_t Be32(uint64_t at) const;
  uint16_t Be16(uint64_t at) const;
  std::string String(uint64_t at) const;
  std::optional<uint64_t> Deserialize(uint32_t value) const;
  std::vector<uint64_t> ObjectOffsets() const;
  const Fixup* fixup(const char* tag) const;

  Reader reader_;
  uint64_t length_ = 0;
  uint32_t version_ = 0;
  std::vector<uint64_t> pools_;
  std::vector<uint8_t> fixup_data_;  // section 0, read whole
  uint64_t fixup_base_ = 0;          // its offset in the file
  std::unordered_map<std::string, Fixup> fixups_;
  std::vector<std::string> types_;
};

}  // namespace skylanders::alchemy
