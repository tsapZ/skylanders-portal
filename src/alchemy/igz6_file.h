#pragma once

// Reader for Alchemy IGZ version 6 (Giants' scenes and textures), big-endian, as far as the
// game's pictures need it. It differs from Swap Force's version 7 (igz_file.h) in three places:
//   - The fixup section (section 0) starts with its own header ("IGZ\x01", version, ?, the header's
//     length), and each fixup record is {u32 type, u32, u32, u32 count, u32 length, u32 data start}
//     with a number for the type instead of a fourcc. Used here: 0 type names (version 7's TMET),
//     2 external ids (EXID: u32 name hash, u32 namespace hash; the texture formats), 5 the object
//     offsets (RVTB, the same nibble packing), 0xA memory handles (TMHN: u32 size | flags << 28,
//     u32 pointer).
//   - A serialized pointer has its pool in the top 8 bits (version 7: 5), the offset in the rest;
//     pool n is section n + 1.
//   - igImage2 (0x60 bytes) has a pointer to its name at +0x08, the source path such as
//     "levels/includes/ui_main/sprites/collections/sky2/portraits/cynder.png"; the rest as in
//     version 7: +0x30 u16 width and height, +0x3C format (0x80000000 | EXID index, whose name hash
//     is FNV-1a of the format name), +0x48 the TMHN index of its pixels.
// Worked out on Giants' character scenes (2026-10-20); their portraits decode to the pictures
// the collection screen shows.
//
// The reader works through a callback, so a scene inside an archive is read only where it is
// needed (Iga8Archive decodes just those blocks).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "alchemy/xenos_dxt.h"

namespace skylanders::alchemy {

class Igz6File {
 public:
  // Copies [offset, offset + size) of the IGZ file to `out`; false if it can't.
  using Reader = std::function<bool(uint64_t offset, size_t size, uint8_t* out)>;

  static std::optional<Igz6File> Parse(Reader reader, uint64_t length);

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

  // Every object of class `type`, in the file's order.
  std::vector<uint64_t> Objects(std::string_view type) const;
  // The string a pointer field of `object` points to (empty for none).
  std::string StringField(uint64_t object, uint32_t field) const;
  // A memory reference: a u32 size | flags << 28 at `size_field`, the pointer at `size_field` + 4.
  std::optional<std::vector<uint8_t>> MemoryField(uint64_t object, uint32_t size_field) const;
  // For the bitmap fonts (src/game/game_fonts.cpp): a u32 field and a serialized pointer as a file
  // offset.
  uint32_t Field(uint64_t object, uint32_t offset) const { return Be32(object + offset); }
  std::optional<uint64_t> Pointer(uint32_t value) const { return Deserialize(value); }

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
  const Fixup* fixup(uint32_t type) const;

  Reader reader_;
  uint64_t length_ = 0;
  std::vector<uint64_t> pools_;
  std::vector<uint8_t> fixup_data_;  // section 0, read whole
  uint64_t fixup_base_ = 0;          // its offset in the file
  std::unordered_map<uint32_t, Fixup> fixups_;
  std::vector<std::string> types_;
};

}  // namespace skylanders::alchemy
