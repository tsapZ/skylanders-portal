#include "alchemy/igz8_file.h"

#include <algorithm>
#include <cstring>

namespace skylanders::alchemy {
namespace {

constexpr uint32_t kMinVersion = 7;
constexpr uint32_t kMaxVersion = 8;
constexpr uint32_t kHeaderSize = 0x800;  // magic, version, hash, platform, fixup count, sections
constexpr uint32_t kMaxSections = 0x20;
constexpr uint32_t kMaxSide = 4096;

struct Format {
  uint32_t hash;  // FNV-1a of the format name ("dxt5_tile_xenon" and friends)
  Dxt kind;
  bool tiled;
};

// The same hashes as Swap Force's and Giants' (from the public igArchiveExtractor format list);
// Trap Team's portraits are 0xB9BCDC57 (DXT5, tiled).
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

std::optional<Igz8File> Igz8File::Parse(Reader reader, uint64_t length) {
  if (length < kHeaderSize) return std::nullopt;
  uint8_t h[kHeaderSize];
  if (!reader(0, sizeof(h), h) || std::memcmp(h, "IGZ\x01", 4) != 0) return std::nullopt;
  Igz8File file;
  file.version_ = ReadBe32(h + 4);
  if (file.version_ < kMinVersion || file.version_ > kMaxVersion) return std::nullopt;
  file.reader_ = std::move(reader);
  file.length_ = length;
  const uint32_t fixup_count = ReadBe32(h + 0x10);
  // Sections: {pool name, offset, length, alignment}, until an offset of 0.
  std::vector<std::pair<uint64_t, uint64_t>> sections;
  for (uint32_t i = 0; i < kMaxSections && 0x14 + 16 * (i + 1) <= kHeaderSize; ++i) {
    const uint8_t* s = h + 0x14 + 16 * i;
    if (!ReadBe32(s + 4)) break;
    sections.emplace_back(ReadBe32(s + 4), ReadBe32(s + 8));
  }
  if (sections.size() < 2 || sections[0].first + sections[0].second > length ||
      sections[0].second < 16 || sections[0].second > 0x1000000) {
    return std::nullopt;
  }
  for (size_t i = 1; i < sections.size(); ++i) file.pools_.push_back(sections[i].first);
  file.fixup_base_ = sections[0].first;
  file.fixup_data_.resize(size_t(sections[0].second));
  if (!file.reader_(file.fixup_base_, file.fixup_data_.size(), file.fixup_data_.data())) {
    return std::nullopt;
  }
  const std::vector<uint8_t>& f = file.fixup_data_;
  size_t p = 0;
  for (uint32_t i = 0; i < fixup_count && p + 16 <= f.size(); ++i) {
    const uint8_t* r = f.data() + p;
    const std::string tag = {char(r[3]), char(r[2]), char(r[1]), char(r[0])};
    const uint32_t count = ReadBe32(r + 4), size = ReadBe32(r + 8), start = ReadBe32(r + 12);
    if (size < 16 || p + size > f.size() || start > size) break;
    file.fixups_[tag] = Fixup{count, file.fixup_base_ + p + start, file.fixup_base_ + p + size};
    p += size;
  }
  // The type names, one NUL-terminated string each, 2-byte aligned after version 7.
  if (const Fixup* types = file.fixup("TMET")) {
    uint64_t at = types->start;
    for (uint32_t i = 0; i < types->count && at < types->end; ++i) {
      std::string name = file.String(at);
      at += name.size() + 1;
      if (file.version_ > 7 && (at & 1)) ++at;
      file.types_.push_back(std::move(name));
    }
  }
  if (file.types_.empty() || !file.fixup("RVTB")) return std::nullopt;
  return file;
}

const Igz8File::Fixup* Igz8File::fixup(const char* tag) const {
  auto it = fixups_.find(tag);
  return it == fixups_.end() ? nullptr : &it->second;
}

uint32_t Igz8File::Be32(uint64_t at) const {
  uint8_t b[4];
  if (at >= fixup_base_ && at + 4 <= fixup_base_ + fixup_data_.size()) {
    std::memcpy(b, fixup_data_.data() + (at - fixup_base_), 4);
  } else if (at + 4 > length_ || !reader_(at, 4, b)) {
    return 0;
  }
  return ReadBe32(b);
}

uint16_t Igz8File::Be16(uint64_t at) const {
  const uint32_t v = Be32(at & ~uint64_t(3));
  return uint16_t((at & 2) ? v : v >> 16);
}

std::string Igz8File::String(uint64_t at) const {
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

std::string Igz8File::StringField(uint64_t object, uint32_t field) const {
  const uint32_t value = Be32(object + field);
  const auto at = value ? Deserialize(value) : std::nullopt;
  return at ? String(*at) : std::string();
}

std::optional<std::vector<uint8_t>> Igz8File::MemoryField(uint64_t object, uint32_t size_field) const {
  constexpr uint32_t kMaxSize = 0x1000000;
  const uint32_t size = Be32(object + size_field) & 0x0FFFFFFF;
  const auto at = Deserialize(Be32(object + size_field + 4));
  if (!at || size > kMaxSize || *at + size > length_) return std::nullopt;
  std::vector<uint8_t> out(size);
  if (size && !reader_(*at, out.size(), out.data())) return std::nullopt;
  return out;
}

std::optional<uint64_t> Igz8File::Deserialize(uint32_t value) const {
  const uint32_t pool = value >> 27;
  if (pool >= pools_.size()) return std::nullopt;
  return pools_[pool] + (value & 0x07FFFFFF);
}

// The object offsets: 4-bit nibbles (3 data bits + continue bit), delta encoded, times 4, plus 4
// before version 9.
std::vector<uint64_t> Igz8File::ObjectOffsets() const {
  std::vector<uint64_t> out;
  const Fixup* objects = fixup("RVTB");
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

std::vector<Igz8File::Image> Igz8File::Images() const {
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

std::vector<uint64_t> Igz8File::Objects(std::string_view type) const {
  std::vector<uint64_t> out;
  for (uint64_t object : ObjectOffsets()) {
    const uint32_t t = Be32(object);
    if (t < types_.size() && types_[t] == type) out.push_back(object);
  }
  return out;
}

std::optional<Igz8File::RawPixels> Igz8File::Raw(const Image& image) const {
  const Fixup* ids = fixup("EXID");
  const Fixup* handles = fixup("TMHN");
  if (!ids || !handles) return std::nullopt;
  const uint32_t format_ref = Be32(image.object + 0x3C);
  const uint32_t handle = Be32(image.object + 0x48);
  if (!(format_ref & 0x80000000) || (format_ref & 0x7FFFFFFF) >= ids->count ||
      handle >= handles->count) {
    return std::nullopt;
  }
  RawPixels raw;
  raw.format_hash = Be32(ids->start + 8 * uint64_t(format_ref & 0x7FFFFFFF));
  raw.size = Be32(handles->start + 8 * uint64_t(handle)) & 0x0FFFFFFF;
  const auto pixels = Deserialize(Be32(handles->start + 8 * uint64_t(handle) + 4));
  if (!pixels || !raw.size || *pixels + raw.size > length_) return std::nullopt;
  raw.offset = *pixels;
  return raw;
}

std::optional<RgbaImage> Igz8File::Decode(const Image& image) const {
  const Fixup* ids = fixup("EXID");
  const Fixup* handles = fixup("TMHN");
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
