#include "alchemy/igz6_file.h"

#include <algorithm>
#include <cstring>

namespace skylanders::alchemy {
namespace {

constexpr uint32_t kVersion = 6;
constexpr uint32_t kHeaderSize = 0x800;  // magic, version, hash, 0, then the section table
constexpr uint32_t kMaxSections = 16;
constexpr uint32_t kFixupTypes = 0;
constexpr uint32_t kFixupExternalIds = 2;
constexpr uint32_t kFixupObjects = 5;
constexpr uint32_t kFixupHandles = 0xA;
constexpr uint32_t kImageSize = 0x60;
constexpr uint32_t kMaxSide = 4096;

struct Format {
  uint32_t hash;  // FNV-1a of the format name ("dxt5_tile_xenon" and friends)
  Dxt kind;
  bool tiled;
};

// The same hashes as Swap Force's (src/alchemy/igz_file.cpp, from the public
// igArchiveExtractor format list); Giants' portraits are 0xB9BCDC57 (DXT5, tiled).
constexpr Format kFormats[] = {
    {0x049D7FAA, Dxt::kDxt5, false}, {0x93566F0B, Dxt::kDxt5, false},
    {0xB9BCDC57, Dxt::kDxt5, true},  {0xD936ADEE, Dxt::kDxt1, false},
    {0xCC82E927, Dxt::kDxt1, false}, {0xDC78A2E3, Dxt::kDxt1, true},
    {0xCBBED02C, Dxt::kDxt3, false}, {0xEABABF45, Dxt::kDxt3, false},
    {0x50A19F21, Dxt::kDxt3, true},
};

uint32_t ReadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

}  // namespace

std::optional<Igz6File> Igz6File::Parse(Reader reader, uint64_t length) {
  if (length < kHeaderSize) return std::nullopt;
  uint8_t h[kHeaderSize];
  if (!reader(0, sizeof(h), h) || std::memcmp(h, "IGZ\x01", 4) != 0 ||
      ReadBe32(h + 4) != kVersion) {
    return std::nullopt;
  }
  Igz6File file;
  file.reader_ = std::move(reader);
  file.length_ = length;
  // Sections: {offset, length, alignment, name}, until an offset of 0.
  std::vector<std::pair<uint64_t, uint64_t>> sections;
  for (uint32_t i = 0; i < kMaxSections; ++i) {
    const uint8_t* s = h + 0x10 + 16 * i;
    if (!ReadBe32(s)) break;
    sections.emplace_back(ReadBe32(s), ReadBe32(s + 4));
  }
  if (sections.size() < 2 || sections[0].first + sections[0].second > length ||
      sections[0].second < 0x1C || sections[0].second > 0x1000000) {
    return std::nullopt;
  }
  for (size_t i = 1; i < sections.size(); ++i) file.pools_.push_back(sections[i].first);
  file.fixup_base_ = sections[0].first;
  file.fixup_data_.resize(size_t(sections[0].second));
  if (!file.reader_(file.fixup_base_, file.fixup_data_.size(), file.fixup_data_.data())) {
    return std::nullopt;
  }
  const std::vector<uint8_t>& f = file.fixup_data_;
  // Records follow the section's own header, whose length is at +0x0C.
  for (size_t p = ReadBe32(f.data() + 0x0C); p + 24 <= f.size();) {
    const uint8_t* r = f.data() + p;
    const uint32_t type = ReadBe32(r), count = ReadBe32(r + 12), size = ReadBe32(r + 16),
                   start = ReadBe32(r + 20);
    if (size < 24 || p + size > f.size() || start > size) break;
    file.fixups_[type] = Fixup{count, file.fixup_base_ + p + start, file.fixup_base_ + p + size};
    p += size;
  }
  // The type names, one NUL-terminated string each.
  if (const Fixup* types = file.fixup(kFixupTypes)) {
    uint64_t p = types->start;
    for (uint32_t i = 0; i < types->count && p < types->end; ++i) {
      std::string name = file.String(p);
      p += name.size() + 1;
      file.types_.push_back(std::move(name));
    }
  }
  if (file.types_.empty() || !file.fixup(kFixupObjects)) return std::nullopt;
  return file;
}

const Igz6File::Fixup* Igz6File::fixup(uint32_t type) const {
  auto it = fixups_.find(type);
  return it == fixups_.end() ? nullptr : &it->second;
}

uint32_t Igz6File::Be32(uint64_t at) const {
  uint8_t b[4];
  if (at >= fixup_base_ && at + 4 <= fixup_base_ + fixup_data_.size()) {
    std::memcpy(b, fixup_data_.data() + (at - fixup_base_), 4);
  } else if (at + 4 > length_ || !reader_(at, 4, b)) {
    return 0;
  }
  return ReadBe32(b);
}

uint16_t Igz6File::Be16(uint64_t at) const {
  const uint32_t v = Be32(at & ~uint64_t(3));
  return uint16_t((at & 2) ? v : v >> 16);
}

std::string Igz6File::String(uint64_t at) const {
  std::string out;
  constexpr size_t kChunk = 64, kMax = 512;
  uint8_t chunk[kChunk];
  while (out.size() < kMax && at < length_) {
    const size_t n = size_t(std::min<uint64_t>(kChunk, length_ - at));
    if (at >= fixup_base_ && at + n <= fixup_base_ + fixup_data_.size()) {
      std::memcpy(chunk, fixup_data_.data() + (at - fixup_base_), n);
    } else if (!reader_(at, n, chunk)) {
      break;
    }
    for (size_t i = 0; i < n; ++i) {
      if (!chunk[i]) return out;
      out.push_back(char(chunk[i]));
    }
    at += n;
  }
  return out;
}

std::optional<uint64_t> Igz6File::Deserialize(uint32_t value) const {
  const uint32_t pool = value >> 24;
  if (pool >= pools_.size()) return std::nullopt;
  return pools_[pool] + (value & 0x00FFFFFF);
}

// The object offsets: 4-bit nibbles (3 data bits + continue bit), delta encoded, times 4, plus 4
// (as version 7's RVTB before version 9).
std::vector<uint64_t> Igz6File::ObjectOffsets() const {
  std::vector<uint64_t> out;
  const Fixup* objects = fixup(kFixupObjects);
  if (!objects) return out;
  const uint8_t* data = fixup_data_.data() + (objects->start - fixup_base_);
  const size_t size = size_t(objects->end - objects->start);
  size_t pos = 0;
  bool high = false;
  auto nibble = [&]() -> uint32_t {
    if (pos >= size) return 0;
    uint32_t v;
    if (!high) {
      v = data[pos] & 0xF;
    } else {
      v = data[pos] >> 4;
      ++pos;
    }
    high = !high;
    return v;
  };
  uint32_t previous = 0;
  for (uint32_t i = 0; i < objects->count && pos < size; ++i) {
    uint32_t cur = nibble();
    uint32_t value = cur & 7;
    int shift = 3;
    while (cur & 8) {
      cur = nibble();
      value |= (cur & 7) << shift;
      shift += 3;
    }
    previous += value * 4 + 4;
    if (auto at = Deserialize(previous)) out.push_back(*at);
  }
  return out;
}

std::vector<uint64_t> Igz6File::Objects(std::string_view type) const {
  std::vector<uint64_t> out;
  for (uint64_t object : ObjectOffsets()) {
    const uint32_t index = Be32(object);
    if (index < types_.size() && types_[index] == type) out.push_back(object);
  }
  return out;
}

std::string Igz6File::StringField(uint64_t object, uint32_t field) const {
  const auto at = Deserialize(Be32(object + field));
  return at ? String(*at) : std::string();
}

std::optional<std::vector<uint8_t>> Igz6File::MemoryField(uint64_t object,
                                                          uint32_t size_field) const {
  constexpr uint32_t kMaxSize = 0x1000000;
  const uint32_t size = Be32(object + size_field) & 0x0FFFFFFF;
  const auto at = Deserialize(Be32(object + size_field + 4));
  if (!at || size > kMaxSize || *at + size > length_) return std::nullopt;
  std::vector<uint8_t> out(size);
  if (size && !reader_(*at, out.size(), out.data())) return std::nullopt;
  return out;
}

std::vector<Igz6File::Image> Igz6File::Images() const {
  std::vector<Image> out;
  for (uint64_t object : ObjectOffsets()) {
    const uint32_t type = Be32(object);
    if (type >= types_.size() || types_[type] != "igImage2") continue;
    Image image;
    image.object = object;
    image.width = Be16(object + 0x30);
    image.height = Be16(object + 0x32);
    if (auto name = Deserialize(Be32(object + 0x08))) image.name = String(*name);
    out.push_back(std::move(image));
  }
  return out;
}

std::optional<RgbaImage> Igz6File::Decode(const Image& image) const {
  const Fixup* ids = fixup(kFixupExternalIds);
  const Fixup* handles = fixup(kFixupHandles);
  if (!ids || !handles || image.width <= 0 || image.height <= 0 || image.width > int(kMaxSide) ||
      image.height > int(kMaxSide)) {
    return std::nullopt;
  }
  const uint32_t format_ref = Be32(image.object + 0x3C);
  const uint32_t handle = Be32(image.object + 0x48);
  if (!(format_ref & 0x80000000) || (format_ref & 0x7FFFFFFF) >= ids->count ||
      handle >= handles->count) {
    return std::nullopt;
  }
  const uint32_t hash = Be32(ids->start + 8 * uint64_t(format_ref & 0x7FFFFFFF));
  const Format* format = nullptr;
  for (const Format& f : kFormats) {
    if (f.hash == hash) format = &f;
  }
  if (!format) return std::nullopt;
  const uint32_t size = Be32(handles->start + 8 * uint64_t(handle)) & 0x0FFFFFFF;
  const auto pixels = Deserialize(Be32(handles->start + 8 * uint64_t(handle) + 4));
  if (!pixels || !size || *pixels + size > length_) return std::nullopt;
  std::vector<uint8_t> data(size);
  if (!reader_(*pixels, data.size(), data.data())) return std::nullopt;
  return DecodeXenosDxt(data.data(), data.size(), image.width, image.height, format->kind,
                        format->tiled);
}

}  // namespace skylanders::alchemy
