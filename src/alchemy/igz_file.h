#pragma once

// Reader for Alchemy IGZ files (versions 7 to 9, big-endian, 32-bit pointers) as far as the
// games' UI textures, material instances, bitmap fonts and toy data need it: Swap Force's are
// version 7, SuperChargers' and Imaginators' version 9, which differs in two places seen here:
// RVTB has no +4 per object, and igImage2 has its size, format and pixel handle 4 bytes later
// (+0x34, +0x40, +0x4C; seen on the collection icons, 88x88 DXT5).
//
//   0x00 'IGZ\x01'  0x04 version  0x10 fixup count
//   0x14 section descriptors, 16 bytes each until offset 0: (pool name, offset, length, align);
//        section 0 holds the fixup records, the others are the memory pools objects live in.
//   Fixup record: fourcc (stored byte-reversed), count, length, data start, data. Used here:
//     TSTR strings, TMET type names, EXID external handles (name hash, namespace hash),
//     TMHN memory handles (u32 size | flags, u32 pointer), RVTB packed offsets of every object.
//   A serialized pointer is pool = value >> 27, offset = value & 0x07FFFFFF.
//   igImage2 on Xenon: +0x30 u16 width, height, depth, level count; +0x3C format (0x80000000 |
//   EXID index, whose name hash is FNV-1a of the format name, e.g. "dxt5_big_xenon"); +0x48 index
//   into TMHN for the pixel data.
// Worked out from the public igLibrary / igArchiveExtractor (NefariousTechSupport) and checked
// against the game's files. UI textures are DXT1/DXT5, one mip level, linear rows padded to 32
// blocks, with 16-bit words byte-swapped.

#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace skylanders::alchemy {

struct RgbaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width * height * 4, RGBA
};

// An igImage2's pixel data as stored, for formats DecodeFirstImage doesn't decode (a bitmap font's
// 16-bit atlas): its size, the format's name hash, and where its bytes are in the file.
struct RawImage {
  size_t object = 0;
  int width = 0;
  int height = 0;
  uint32_t format_hash = 0;
  size_t data = 0;  // offset into IgzFile::Bytes()
  uint32_t size = 0;
};

// Xbox 360 2D tiling (XGAddress2DTiledOffset): the byte offset of element (x, y) of a surface
// `pitch` elements wide, each 2^log2_bytes bytes (a DXT block or a texel).
uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2_bytes);

class IgzFile {
 public:
  static std::optional<IgzFile> Parse(std::vector<uint8_t> data);

  // The strings of a fixup record ("TSTR", "TMET"), empty if there is none. TDEP has two per entry
  // (the dependency's name and its file: "JuiceStandard_ChopChop_Up1_10",
  // "textures:/JuiceStandard_ChopChop_Up1_10.igz").
  std::vector<std::string> Strings(const char* tag, uint32_t per_entry = 1) const;

  // The first igImage2 in the file, decoded; nullopt if there is none or its format isn't DXT1/3/5.
  std::optional<RgbaImage> DecodeFirstImage() const;
  // Every igImage2 with its pixel data as stored, in object order.
  std::vector<RawImage> RawImages() const;
  // A serialized pointer as an offset into Bytes(), nullopt if it points outside the pools.
  std::optional<size_t> Pointer(uint32_t value) const { return Deserialize(value); }
  // The whole file, for fields that aren't u32.
  const std::vector<uint8_t>& Bytes() const { return data_; }

  // Every object whose class (TMET) name ends with `suffix`, with its class name.
  std::vector<std::pair<size_t, std::string>> Objects(const std::string& suffix) const;
  // A big-endian u32 field of an object.
  uint32_t Field(size_t object, uint32_t offset) const { return Be32(object + offset); }
  // The NUL-terminated string a serialized pointer points to (empty for none).
  std::string StringAt(uint32_t pointer) const;
  // The external name a handle (0x80000000 | index into EXNM) refers to, with its namespace:
  // {"Collection_ChopChop_Normal", "Collection_ChopChop_Normal"}, {"<texture>", "image"}.
  std::optional<std::pair<std::string, std::string>> ExternalName(uint32_t handle) const;
  // The same for a handle that counts only the named entries (those whose name word has the top
  // bit set), as the character data's handles do: there the named entries alternate with type
  // names ("metaobject"). Where every entry is named the two are the same.
  std::optional<std::pair<std::string, std::string>> NamedExternalName(uint32_t handle) const;

 private:
  struct Fixup {
    uint32_t count = 0;
    size_t start = 0;
    size_t end = 0;
  };

  uint32_t Be32(size_t at) const;
  uint16_t Be16(size_t at) const;
  std::optional<size_t> Deserialize(uint32_t value) const;
  std::vector<size_t> ObjectOffsets() const;

  std::vector<uint8_t> data_;
  uint32_t version_ = 0;
  std::vector<size_t> pools_;
  std::unordered_map<std::string, Fixup> fixups_;
};

}  // namespace skylanders::alchemy
