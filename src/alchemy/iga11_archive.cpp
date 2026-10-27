#include "alchemy/iga11_archive.h"

#include <algorithm>
#include <cstring>

#include "alchemy/lzma_decoder.h"

namespace skylanders::alchemy {
namespace {

constexpr uint32_t kMagic = 0x1A414749;  // "IGA\x1a" read little-endian
constexpr uint32_t kVersion = 11;
constexpr uint32_t kHeaderSize = 0x38;
constexpr uint32_t kEntrySize = 16;
constexpr uint32_t kBlockSize = 0x8000;
constexpr uint32_t kStored = 0xFFFFFFFF;
constexpr uint32_t kLzma = 2;
constexpr uint64_t kOffsetMask = 0xFFFFFFFFFFull;  // below the ordinal in the entry's first u64

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p)) | (uint64_t(Le32(p + 4)) << 32); }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

}  // namespace

std::unique_ptr<Iga11Archive> Iga11Archive::Open(const std::filesystem::path& path) {
  auto archive = std::make_unique<Iga11Archive>();
  archive->in_.open(path, std::ios::binary | std::ios::ate);
  if (!archive->in_) return nullptr;
  archive->file_size_ = uint64_t(archive->in_.tellg());
  uint8_t h[kHeaderSize];
  if (!archive->ReadRaw(0, sizeof(h), h) || Le32(h) != kMagic || Le32(h + 4) != kVersion) {
    return nullptr;
  }
  const uint32_t count = Le32(h + 0x0C);
  archive->sector_size_ = Le32(h + 0x10);
  const uint32_t n_large = Le32(h + 0x1C), n_medium = Le32(h + 0x20), n_small = Le32(h + 0x24);
  const uint64_t names_offset = Le64(h + 0x28), names_size = Le64(h + 0x30);
  const uint64_t toc_size = uint64_t(count) * (4 + kEntrySize) + uint64_t(n_large) * 4 +
                            uint64_t(n_medium) * 2 + n_small;
  if (!archive->sector_size_ || count > 0x100000 || toc_size > 0x4000000 ||
      names_offset + names_size > archive->file_size_ || names_size > 0x4000000 ||
      names_size < uint64_t(count) * 4) {
    return nullptr;
  }
  std::vector<uint8_t> toc(static_cast<size_t>(toc_size)), names(static_cast<size_t>(names_size));
  if (!archive->ReadRaw(kHeaderSize, toc.size(), toc.data()) ||
      !archive->ReadRaw(names_offset, names.size(), names.data())) {
    return nullptr;
  }
  const uint8_t* entries = toc.data() + size_t(count) * 4;  // after the name hashes
  archive->files_.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    File& f = archive->files_[i];
    const uint8_t* e = entries + size_t(kEntrySize) * i;
    f.offset = Le64(e) & kOffsetMask;
    f.length = Le32(e + 8);
    f.block = Le32(e + 12);
    for (size_t at = Le32(names.data() + 4 * i); at < names.size() && names[at]; ++at) {
      f.name.push_back(char(names[at]));
    }
  }
  const uint8_t* tables = entries + size_t(count) * kEntrySize;
  archive->large_.resize(n_large);
  for (uint32_t i = 0; i < n_large; ++i) archive->large_[i] = Le32(tables + 4 * i);
  tables += size_t(n_large) * 4;
  archive->medium_.resize(n_medium);
  for (uint32_t i = 0; i < n_medium; ++i) archive->medium_[i] = Le16(tables + 2 * i);
  tables += size_t(n_medium) * 2;
  archive->small_.assign(tables, tables + n_small);
  return archive;
}

std::optional<size_t> Iga11Archive::Find(std::string_view name) const {
  for (size_t i = 0; i < files_.size(); ++i) {
    if (files_[i].name == name) return i;
  }
  return std::nullopt;
}

bool Iga11Archive::ReadRaw(uint64_t offset, size_t size, uint8_t* out) {
  if (offset + size > file_size_) return false;
  in_.clear();
  in_.seekg(std::streamoff(offset));
  return bool(in_.read(reinterpret_cast<char*>(out), std::streamsize(size)));
}

// Which table a file's block entries are in depends on how many sectors the file can span.
bool Iga11Archive::BlockEntry(uint32_t index, uint32_t length, bool* compressed,
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

const std::vector<uint8_t>* Iga11Archive::Block(size_t index, uint32_t block) {
  const uint64_t key = (uint64_t(index) << 20) | block;
  if (auto it = blocks_.find(key); it != blocks_.end()) return &it->second;
  const File& f = files_[index];
  if ((f.block >> 28) != kLzma) return nullptr;
  bool compressed = false;
  uint32_t sector = 0;
  if (!BlockEntry((f.block & 0x0FFFFFFF) + block, f.length, &compressed, &sector)) return nullptr;
  const uint32_t want = std::min(kBlockSize, f.length - block * kBlockSize);
  const uint64_t at = f.offset + uint64_t(sector) * sector_size_;
  std::vector<uint8_t> data;
  if (!compressed) {
    data.resize(want);
    if (!ReadRaw(at, want, data.data())) return nullptr;
  } else {
    uint8_t size_bytes[2];
    if (!ReadRaw(at, 2, size_bytes)) return nullptr;
    // The size counts the LZMA stream after the 5 property bytes.
    std::vector<uint8_t> packed(size_t(Le16(size_bytes)) + 5);
    if (!ReadRaw(at + 2, packed.size(), packed.data())) return nullptr;
    auto decoded = LzmaDecode(packed.data(), packed.size(), want);
    if (!decoded) return nullptr;
    data = std::move(*decoded);
  }
  return &blocks_.emplace(key, std::move(data)).first->second;
}

bool Iga11Archive::Read(size_t index, uint64_t offset, size_t size, uint8_t* out) {
  if (index >= files_.size()) return false;
  const File& f = files_[index];
  if (offset + size > f.length) return false;
  if (f.block == kStored) return ReadRaw(f.offset + offset, size, out);
  while (size) {
    const uint32_t block = uint32_t(offset / kBlockSize);
    const std::vector<uint8_t>* data = Block(index, block);
    if (!data) return false;
    const size_t in_block = size_t(offset % kBlockSize);
    const size_t n = std::min(size, data->size() - std::min(in_block, data->size()));
    if (!n) return false;
    std::memcpy(out, data->data() + in_block, n);
    out += n;
    offset += n;
    size -= n;
  }
  return true;
}

}  // namespace skylanders::alchemy
