#pragma once

// Reader for the .pak files of Swap Force (Alchemy igArchive version 0x0A), SuperChargers and
// Imaginators (0x0B, the same layout; checked 2026-10-21 on SuperChargers' permanent.pak and
// ToyCollectionMaterials.pak: names and files decode), big-endian.
//
//   0x00 magic 0x1A414749   0x04 version (0x0A)   0x0C file count   0x10 sector size (0x800)
//   0x1C/0x20/0x24 counts of the large (u32) / medium (u16) / small (u8) block tables
//   0x28 name table offset (u64)
//   0x38 name hashes (u32 each), then 16-byte file entries (u64 ordinal<<40 | offset, u32 length,
//   u32 block index), then the three block tables.
// A file is stored in 0x8000-byte blocks. The top nibble of its block index is the compression
// (2 = LZMA, 1 = raw deflate) or the whole index is 0xFFFFFFFF for an uncompressed file. A block
// table entry's top bit says whether that block is compressed, the rest is its sector relative to
// the file offset. A compressed block starts with its size as a little-endian u16; LZMA blocks
// then have 5 property bytes and a raw stream. Each name table entry is one name and a u32 time.
// Worked out from the public igLibrary (NefariousTechSupport/igRewrite8, Core/igArchive.cs, which
// reads version 0x0B with the same layout) and checked against the game's own paks.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace skylanders::alchemy {

class IgaArchive {
 public:
  static std::optional<IgaArchive> Open(const std::filesystem::path& path);

  size_t size() const { return files_.size(); }
  // The stored path, e.g. "materialinstances\toycollection\collection_chopchop_normal.igz".
  const std::string& name(size_t index) const { return files_[index].name; }
  // The file's contents; nullopt for a corrupt file or an unknown compression.
  std::optional<std::vector<uint8_t>> Read(size_t index) const;

 private:
  struct File {
    std::string name;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t block = 0;
  };

  bool BlockEntry(uint32_t index, uint32_t length, bool* compressed, uint32_t* sector) const;

  std::vector<uint8_t> data_;
  uint32_t sector_size_ = 0x800;
  std::vector<File> files_;
  std::vector<uint32_t> large_;
  std::vector<uint16_t> medium_;
  std::vector<uint8_t> small_;
};

}  // namespace skylanders::alchemy
