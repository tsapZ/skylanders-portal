#include "alchemy/iga_archive.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "alchemy/inflate.h"
#include "alchemy/lzma_decoder.h"

namespace skylanders::alchemy {
namespace {

constexpr uint32_t kMagic = 0x1A414749;
constexpr uint32_t kBlockSize = 0x8000;

uint32_t Be32(const std::vector<uint8_t>& d, size_t at) {
  return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) |
         d[at + 3];
}
uint16_t Be16(const std::vector<uint8_t>& d, size_t at) {
  return uint16_t((d[at] << 8) | d[at + 1]);
}

}  // namespace

std::optional<IgaArchive> IgaArchive::Open(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const std::streamsize length = in.tellg();
  if (length < 0x38) return std::nullopt;
  IgaArchive archive;
  archive.data_.resize(size_t(length));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(archive.data_.data()), length)) return std::nullopt;
  const auto& d = archive.data_;
  if (Be32(d, 0) != kMagic || (Be32(d, 4) != 0x0A && Be32(d, 4) != 0x0B)) return std::nullopt;

  const uint32_t count = Be32(d, 0x0C);
  archive.sector_size_ = Be32(d, 0x10);
  const uint32_t n_large = Be32(d, 0x1C), n_medium = Be32(d, 0x20), n_small = Be32(d, 0x24);
  const uint64_t name_table = (uint64_t(Be32(d, 0x28)) << 32) | Be32(d, 0x2C);
  const size_t entries = 0x38 + size_t(4) * count;
  const size_t tables = entries + size_t(16) * count;
  const size_t tables_end = tables + 4 * size_t(n_large) + 2 * size_t(n_medium) + n_small;
  if (tables_end > d.size() || name_table + 4 * uint64_t(count) > d.size()) return std::nullopt;

  archive.files_.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    File& f = archive.files_[i];
    const size_t e = entries + size_t(16) * i;
    f.offset = Be32(d, e + 4);  // the low 32 bits of ordinal<<40 | offset
    f.length = Be32(d, e + 8);
    f.block = Be32(d, e + 12);
    size_t at = size_t(name_table) + Be32(d, size_t(name_table) + 4 * size_t(i));
    while (at < d.size() && d[at]) f.name.push_back(char(d[at++]));
  }
  size_t at = tables;
  archive.large_.resize(n_large);
  for (auto& v : archive.large_) v = Be32(d, at), at += 4;
  archive.medium_.resize(n_medium);
  for (auto& v : archive.medium_) v = Be16(d, at), at += 2;
  archive.small_.assign(d.begin() + ptrdiff_t(at), d.begin() + ptrdiff_t(at + n_small));
  return archive;
}

// Which table a file's block entries are in depends on how many sectors the file can span.
bool IgaArchive::BlockEntry(uint32_t index, uint32_t length, bool* compressed,
                            uint32_t* sector) const {
  if (uint64_t(0x7F) * sector_size_ < length) {
    if (uint64_t(0x7FFF) * sector_size_ < length) {
      if (index >= large_.size()) return false;
      *compressed = large_[index] >> 31;
      *sector = large_[index] & 0x7FFFFFFF;
    } else {
      if (index >= medium_.size()) return false;
      *compressed = medium_[index] >> 15;
      *sector = medium_[index] & 0x7FFF;
    }
  } else {
    if (index >= small_.size()) return false;
    *compressed = small_[index] >> 7;
    *sector = small_[index] & 0x7F;
  }
  return true;
}

std::optional<std::vector<uint8_t>> IgaArchive::Read(size_t index) const {
  if (index >= files_.size()) return std::nullopt;
  const File& f = files_[index];
  if (f.block == 0xFFFFFFFF) {
    if (uint64_t(f.offset) + f.length > data_.size()) return std::nullopt;
    return std::vector<uint8_t>(data_.begin() + f.offset, data_.begin() + f.offset + f.length);
  }
  const uint32_t compression = f.block >> 28;
  const uint32_t first = f.block & 0x0FFFFFFF;
  std::vector<uint8_t> out;
  out.reserve(f.length);
  const uint32_t blocks = (f.length + kBlockSize - 1) / kBlockSize;
  for (uint32_t b = 0; b < blocks; ++b) {
    bool compressed = false;
    uint32_t sector = 0;
    if (!BlockEntry(first + b, f.length, &compressed, &sector)) return std::nullopt;
    const uint32_t want = std::min(kBlockSize, f.length - b * kBlockSize);
    const uint64_t at = uint64_t(f.offset) + uint64_t(sector) * sector_size_;
    if (!compressed) {
      if (at + want > data_.size()) return std::nullopt;
      out.insert(out.end(), data_.begin() + ptrdiff_t(at), data_.begin() + ptrdiff_t(at + want));
      continue;
    }
    if (at + 2 > data_.size()) return std::nullopt;
    if (compression == 1) {  // raw deflate after the size (the character archives)
      const uint32_t packed = data_[at] | (uint32_t(data_[at + 1]) << 8);
      if (at + 2 + packed > data_.size()) return std::nullopt;
      auto block = InflateRaw(data_.data() + at + 2, packed, want);
      if (!block) return std::nullopt;
      out.insert(out.end(), block->begin(), block->end());
      continue;
    }
    if (compression != 2) return std::nullopt;
    // The size counts the LZMA stream after the 5 property bytes.
    const uint32_t packed = (data_[at] | (uint32_t(data_[at + 1]) << 8)) + 5;
    if (at + 2 + packed > data_.size()) return std::nullopt;
    auto block = LzmaDecode(data_.data() + at + 2, packed, want);
    if (!block) return std::nullopt;
    out.insert(out.end(), block->begin(), block->end());
  }
  return out;
}

}  // namespace skylanders::alchemy
