#pragma once

// Reader for Giants' .arc / .bld files: Alchemy igArchive (IGA) version 8, little-endian.
//
//   0x00 magic "IGA\x1a"   0x04 version (8)   0x08 table of contents size   0x0C file count
//   0x10 sector size (0x100)   0x14 / 0x18 hash search divider / slop   0x1C name table offset
//   0x20 name table size   0x24 / 0x28 / 0x2C counts of the large (u32) / medium (u16) / small (u8)
//   block tables
//   0x34 name hashes (u32 each), then 12-byte file entries (offset, length, block index), then the
//   three block tables. The name table starts with one u32 per file: the offset of its name from
//   the table's start.
// The blocks work as in Swap Force's version 10 (src/alchemy/iga_archive.h):
// a file is stored in 0x8000-byte blocks; its block index is 0xFFFFFFFF for a file stored as is,
// else the top nibble is the compression (2 = LZMA, the only one on Giants' disc) and the rest the
// first block's entry in the table for its size. A block table entry's top bit says whether that
// block is compressed, the rest is its sector from the file's offset; a compressed block starts
// with its size as a u16, then 5 LZMA property bytes and the raw stream. Worked out on Giants'
// files (2026-10-20): every archive's names list, and the nested scenes and language packs decode.
//
// Only the table of contents is read at Open; Read decodes just the blocks a range touches and
// keeps them, so the parts of a large scene that a reader needs cost little. Not thread-safe.

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

class Iga8Archive {
 public:
  static std::unique_ptr<Iga8Archive> Open(const std::filesystem::path& path);

  size_t size() const { return files_.size(); }
  // The stored path, e.g. "level.bld", "GERMAN.pak" or
  // "c:/tfb/build/360/levels/.../airdragon_up7b.png/0xdfd227a1.png.igb.tex.igz".
  const std::string& name(size_t index) const { return files_[index].name; }
  uint32_t length(size_t index) const { return files_[index].length; }
  std::optional<size_t> Find(std::string_view name) const;

  // Copies [offset, offset + size) of file `index` to `out`; false if the range is outside the
  // file, the archive is corrupt or a block can't be decoded.
  bool Read(size_t index, uint64_t offset, size_t size, uint8_t* out);
  // The whole file.
  std::optional<std::vector<uint8_t>> ReadAll(size_t index);

 private:
  struct File {
    std::string name;
    uint32_t offset = 0;
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
