#pragma once

// Reader for Trap Team's .arc / .bld files: Alchemy igArchive (IGA) version 11, header
// little-endian.
//
//   0x00 magic "IGA\x1a"   0x04 version (11)   0x08 table of contents size   0x0C file count
//   0x10 sector size (0x100)   0x14 / 0x18 hash search divider / slop   0x1C / 0x20 / 0x24 counts
//   of the large (u32) / medium (u16) / small (u8) block tables   0x28 name table offset (u64)
//   0x30 name table size (u64)
//   0x38 name hashes (u32 each), then 16-byte file entries (u64 ordinal << 40 | offset, u32 length,
//   u32 block index), then the three block tables. The name table starts with one u32 per file:
//   the offset of its name from the table's start.
// Otherwise as Giants' version 8 (src/alchemy/iga8_archive.h): 0x8000-byte
// blocks, block index 0xFFFFFFFF for a file stored as is, else the top nibble is the compression
// (2 = LZMA) and the rest the first block's entry; a block entry's top bit says compressed, the
// rest is its sector from the file's offset; a compressed block is its size as a u16, 5 LZMA
// property bytes and the raw stream. Swap Force's version 10 has the same entries
// (src/alchemy/iga_archive.h). Worked out on Trap Team's character archives
// (2026-10-21): the names list and the nested scenes decode.
//
// Only the table of contents is read at Open; Read decodes just the blocks a range touches and
// keeps them. Not thread-safe.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace skylanders::alchemy {

class Iga11Archive {
 public:
  static std::unique_ptr<Iga11Archive> Open(const std::filesystem::path& path);

  size_t size() const { return files_.size(); }
  // The stored path, e.g. "level.bld", "GERMAN.pak" or
  // "C:/tfb/build/360/models/.../airdragon_up7b.png/0x301c7a03.png.igb.tex.igz".
  const std::string& name(size_t index) const { return files_[index].name; }
  uint32_t length(size_t index) const { return files_[index].length; }
  std::optional<size_t> Find(std::string_view name) const;

  // Copies [offset, offset + size) of file `index` to `out`; false if the range is outside the
  // file, the archive is corrupt or a block can't be decoded.
  bool Read(size_t index, uint64_t offset, size_t size, uint8_t* out);

 private:
  struct File {
    std::string name;
    uint64_t offset = 0;
    uint32_t length = 0;
    uint32_t block = 0;
  };

  bool BlockEntry(uint32_t index, uint32_t length, bool* compressed, uint32_t* sector) const;
  const std::vector<uint8_t>* Block(size_t index, uint32_t block);
  bool ReadRaw(uint64_t offset, size_t size, uint8_t* out);

  std::ifstream in_;
  uint64_t file_size_ = 0;
  uint32_t sector_size_ = 0x100;
  std::vector<File> files_;
  std::vector<uint32_t> large_;
  std::vector<uint16_t> medium_;
  std::vector<uint8_t> small_;
  // Decoded blocks by (file << 20 | block); a scene reader touches a few dozen of them.
  std::unordered_map<uint64_t, std::vector<uint8_t>> blocks_;
};

}  // namespace skylanders::alchemy
