#include "alchemy/igz_file.h"

#include <algorithm>
#include <cstring>

namespace skylanders::alchemy {

uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2_bytes) {
  pitch = (pitch + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bytes + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bytes;
  const uint32_t off = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((off & ~0x1FFu) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3F);
}

namespace {

enum class Dxt { kDxt1, kDxt3, kDxt5 };

struct Format {
  uint32_t hash;  // FNV-1a of the format name
  Dxt kind;
  bool tiled;
};

// "dxt5_big_xenon" and friends; the tiled ones are "..._tile_..." variants. Hashes from the public
// igArchiveExtractor format list (IGZ/IGZ_Textureformat.cs).
constexpr Format kFormats[] = {
    {0x049D7FAA, Dxt::kDxt5, false}, {0x93566F0B, Dxt::kDxt5, false},
    {0xB9BCDC57, Dxt::kDxt5, true},  {0xD936ADEE, Dxt::kDxt1, false},
    {0xCC82E927, Dxt::kDxt1, false}, {0xDC78A2E3, Dxt::kDxt1, true},
    {0xCBBED02C, Dxt::kDxt3, false}, {0xEABABF45, Dxt::kDxt3, false},
    {0x50A19F21, Dxt::kDxt3, true},
};

void Rgb565(uint16_t c, uint8_t out[3]) {
  out[0] = uint8_t((((c >> 11) & 31) * 255 + 15) / 31);
  out[1] = uint8_t((((c >> 5) & 63) * 255 + 31) / 63);
  out[2] = uint8_t(((c & 31) * 255 + 15) / 31);
}

// One 4x4 colour block (little-endian after the byte swap).
void DecodeColorBlock(const uint8_t* b, bool dxt1, RgbaImage& img, int x0, int y0) {
  const uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
  const uint32_t indices = b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24);
  uint8_t pal[4][4];
  Rgb565(c0, pal[0]);
  Rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = 255;
  if (c0 > c1 || !dxt1) {
    for (int k = 0; k < 3; ++k) {
      pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
    }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
    pal[2][3] = 255;
    std::memset(pal[3], 0, 4);
  }
  for (int i = 0; i < 16; ++i) {
    const int x = x0 + i % 4, y = y0 + i / 4;
    if (x >= img.width || y >= img.height) continue;
    std::memcpy(&img.pixels[(size_t(y) * img.width + x) * 4], pal[(indices >> (2 * i)) & 3], 4);
  }
}

void DecodeAlphaBlock(const uint8_t* b, Dxt kind, RgbaImage& img, int x0, int y0) {
  uint8_t alpha[16];
  if (kind == Dxt::kDxt5) {
    const uint32_t a0 = b[0], a1 = b[1];
    uint8_t table[8] = {uint8_t(a0), uint8_t(a1)};
    if (a0 > a1) {
      for (int i = 0; i < 6; ++i) table[2 + i] = uint8_t(((6 - i) * a0 + (1 + i) * a1) / 7);
    } else {
      for (int i = 0; i < 4; ++i) table[2 + i] = uint8_t(((4 - i) * a0 + (1 + i) * a1) / 5);
      table[6] = 0;
      table[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) alpha[i] = table[(bits >> (3 * i)) & 7];
  } else {
    for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 0xF) * 17);
  }
  for (int i = 0; i < 16; ++i) {
    const int x = x0 + i % 4, y = y0 + i / 4;
    if (x >= img.width || y >= img.height) continue;
    img.pixels[(size_t(y) * img.width + x) * 4 + 3] = alpha[i];
  }
}

}  // namespace

std::optional<IgzFile> IgzFile::Parse(std::vector<uint8_t> data) {
  if (data.size() < 0x24 || std::memcmp(data.data(), "IGZ\x01", 4) != 0) return std::nullopt;
  IgzFile file;
  file.data_ = std::move(data);
  file.version_ = file.Be32(4);
  const uint32_t fixup_count = file.Be32(0x10);
  std::vector<size_t> sections;
  for (size_t i = 0; i < 0x20; ++i) {
    const size_t at = 0x14 + 16 * i;
    if (at + 16 > file.data_.size()) break;
    const uint32_t offset = file.Be32(at + 4);
    if (!offset) break;
    sections.push_back(offset);
  }
  if (sections.empty()) return std::nullopt;
  file.pools_.assign(sections.begin() + 1, sections.end());
  size_t p = sections[0];
  for (uint32_t i = 0; i < fixup_count; ++i) {
    if (p + 16 > file.data_.size()) return std::nullopt;
    const std::string tag = {char(file.data_[p + 3]), char(file.data_[p + 2]),
                             char(file.data_[p + 1]), char(file.data_[p])};
    const uint32_t count = file.Be32(p + 4), length = file.Be32(p + 8), start = file.Be32(p + 12);
    if (length < 16 || p + length > file.data_.size()) return std::nullopt;
    file.fixups_[tag] = Fixup{count, p + start, p + length};
    p += length;
  }
  return file;
}

uint32_t IgzFile::Be32(size_t at) const {
  if (at + 4 > data_.size()) return 0;
  return (uint32_t(data_[at]) << 24) | (uint32_t(data_[at + 1]) << 16) |
         (uint32_t(data_[at + 2]) << 8) | data_[at + 3];
}

uint16_t IgzFile::Be16(size_t at) const {
  if (at + 2 > data_.size()) return 0;
  return uint16_t((data_[at] << 8) | data_[at + 1]);
}

std::optional<size_t> IgzFile::Deserialize(uint32_t value) const {
  const uint32_t pool = value >> 27;
  if (pool >= pools_.size()) return std::nullopt;
  return pools_[pool] + (value & 0x07FFFFFF);
}

std::vector<std::string> IgzFile::Strings(const char* tag, uint32_t per_entry) const {
  std::vector<std::string> out;
  auto it = fixups_.find(tag);
  if (it == fixups_.end()) return out;
  size_t p = it->second.start;
  for (uint32_t i = 0; i < it->second.count * per_entry && p < it->second.end; ++i) {
    std::string s;
    while (p < it->second.end && data_[p]) s.push_back(char(data_[p++]));
    ++p;
    if (version_ > 7 && (p & 1)) ++p;  // later versions align strings to 2 bytes
    out.push_back(std::move(s));
  }
  return out;
}

// RVTB: object offsets as 4-bit nibbles (3 data bits + continue bit), delta encoded, times 4,
// plus 4 for versions before 9.
std::vector<size_t> IgzFile::ObjectOffsets() const {
  std::vector<size_t> out;
  auto it = fixups_.find("RVTB");
  if (it == fixups_.end()) return out;
  size_t pos = it->second.start;
  bool high = false;
  auto nibble = [&]() -> uint32_t {
    if (pos >= it->second.end) return 0;
    uint32_t v;
    if (!high) {
      v = data_[pos] & 0xF;
    } else {
      v = data_[pos] >> 4;
      ++pos;
    }
    high = !high;
    return v;
  };
  uint32_t previous = 0;
  for (uint32_t i = 0; i < it->second.count; ++i) {
    uint32_t cur = nibble();
    uint32_t value = cur & 7;
    int shift = 3;
    while (cur & 8) {
      cur = nibble();
      value |= (cur & 7) << shift;
      shift += 3;
    }
    previous += value * 4 + (version_ < 9 ? 4 : 0);
    if (auto at = Deserialize(previous)) out.push_back(*at);
  }
  return out;
}

std::vector<RawImage> IgzFile::RawImages() const {
  std::vector<RawImage> out;
  const std::vector<std::string> types = Strings("TMET");
  auto exid = fixups_.find("EXID");
  auto tmhn = fixups_.find("TMHN");
  if (exid == fixups_.end() || tmhn == fixups_.end()) return out;
  const size_t shift = version_ >= 9 ? 4 : 0;
  for (size_t object : ObjectOffsets()) {
    const uint32_t type = Be32(object);
    if (type >= types.size() || types[type] != "igImage2") continue;
    RawImage image;
    image.object = object;
    image.width = Be16(object + 0x30 + shift);
    image.height = Be16(object + 0x32 + shift);
    const uint32_t format_ref = Be32(object + 0x3C + shift);
    const uint32_t handle = Be32(object + 0x48 + shift);
    if (!(format_ref & 0x80000000) || (format_ref & 0x7FFFFFFF) >= exid->second.count ||
        handle >= tmhn->second.count) {
      continue;
    }
    image.format_hash = Be32(exid->second.start + 8 * (format_ref & 0x7FFFFFFF));
    image.size = Be32(tmhn->second.start + 8 * handle) & 0x0FFFFFFF;
    const auto pixels = Deserialize(Be32(tmhn->second.start + 8 * handle + 4));
    if (!pixels || *pixels + image.size > data_.size()) continue;
    image.data = *pixels;
    out.push_back(image);
  }
  return out;
}

std::optional<RgbaImage> IgzFile::DecodeFirstImage() const {
  const std::vector<std::string> types = Strings("TMET");
  auto exid = fixups_.find("EXID");
  auto tmhn = fixups_.find("TMHN");
  if (exid == fixups_.end() || tmhn == fixups_.end()) return std::nullopt;
  for (size_t object : ObjectOffsets()) {
    const uint32_t type = Be32(object);
    if (type >= types.size() || types[type] != "igImage2") continue;
    const size_t shift = version_ >= 9 ? 4 : 0;
    const int width = Be16(object + 0x30 + shift), height = Be16(object + 0x32 + shift);
    const uint32_t format_ref = Be32(object + 0x3C + shift);
    const uint32_t handle = Be32(object + 0x48 + shift);
    if (!(format_ref & 0x80000000) || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
      return std::nullopt;
    }
    const uint32_t format_index = format_ref & 0x7FFFFFFF;
    if (format_index >= exid->second.count || handle >= tmhn->second.count) return std::nullopt;
    const uint32_t hash = Be32(exid->second.start + 8 * format_index);
    const Format* format = nullptr;
    for (const Format& f : kFormats) {
      if (f.hash == hash) format = &f;
    }
    if (!format) return std::nullopt;
    const uint32_t size = Be32(tmhn->second.start + 8 * handle) & 0x0FFFFFFF;
    const auto pixels = Deserialize(Be32(tmhn->second.start + 8 * handle + 4));
    if (!pixels || *pixels + size > data_.size()) return std::nullopt;

    const uint32_t bpb = format->kind == Dxt::kDxt1 ? 8 : 16;
    const uint32_t bw = (uint32_t(width) + 3) / 4, bh = (uint32_t(height) + 3) / 4;
    const uint32_t pitch = (bw + 31) & ~31u;  // rows padded to 32 blocks
    RgbaImage img;
    img.width = width;
    img.height = height;
    img.pixels.assign(size_t(width) * height * 4, 0);
    uint8_t block[16];
    for (uint32_t by = 0; by < bh; ++by) {
      for (uint32_t bx = 0; bx < bw; ++bx) {
        const uint32_t src = format->tiled ? TiledOffset2D(bx, by, bw, bpb == 8 ? 3 : 4)
                                           : (by * pitch + bx) * bpb;
        if (src + bpb > size) return std::nullopt;
        // 16-bit words are big-endian.
        for (uint32_t i = 0; i < bpb; ++i) block[i] = data_[*pixels + src + (i ^ 1)];
        const int x0 = int(bx * 4), y0 = int(by * 4);
        if (format->kind == Dxt::kDxt1) {
          DecodeColorBlock(block, true, img, x0, y0);
        } else {
          DecodeColorBlock(block + 8, false, img, x0, y0);
          DecodeAlphaBlock(block, format->kind, img, x0, y0);
        }
      }
    }
    return img;
  }
  return std::nullopt;
}

std::vector<std::pair<size_t, std::string>> IgzFile::Objects(const std::string& suffix) const {
  std::vector<std::pair<size_t, std::string>> out;
  const std::vector<std::string> types = Strings("TMET");
  for (size_t object : ObjectOffsets()) {
    const uint32_t type = Be32(object);
    if (type >= types.size()) continue;
    const std::string& name = types[type];
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      out.emplace_back(object, name);
    }
  }
  return out;
}

std::string IgzFile::StringAt(uint32_t pointer) const {
  std::string out;
  const auto at = Deserialize(pointer);
  if (!pointer || !at) return out;
  for (size_t p = *at; p < data_.size() && data_[p] && out.size() < 512; ++p) {
    out.push_back(char(data_[p]));
  }
  return out;
}

std::optional<std::pair<std::string, std::string>> IgzFile::ExternalName(uint32_t handle) const {
  auto it = fixups_.find("EXNM");
  if (!(handle & 0x80000000) || it == fixups_.end()) return std::nullopt;
  const uint32_t index = handle & 0x7FFFFFFF;
  if (index >= it->second.count) return std::nullopt;
  // Each entry: the name's and the namespace's index into TSTR, both with the top bit set.
  const std::vector<std::string> strings = Strings("TSTR");
  const uint32_t name = Be32(it->second.start + 8 * size_t(index)) & 0x7FFFFFFF;
  const uint32_t space = Be32(it->second.start + 8 * size_t(index) + 4) & 0x7FFFFFFF;
  if (name >= strings.size() || space >= strings.size()) return std::nullopt;
  return std::make_pair(strings[name], strings[space]);
}

std::optional<std::pair<std::string, std::string>> IgzFile::NamedExternalName(uint32_t handle) const {
  auto it = fixups_.find("EXNM");
  if (!(handle & 0x80000000) || it == fixups_.end()) return std::nullopt;
  uint32_t left = handle & 0x7FFFFFFF;
  const std::vector<std::string> strings = Strings("TSTR");
  for (uint32_t k = 0; k < it->second.count; ++k) {
    const uint32_t name = Be32(it->second.start + 8 * size_t(k));
    if (!(name & 0x80000000)) continue;
    if (left--) continue;
    const uint32_t space = Be32(it->second.start + 8 * size_t(k) + 4) & 0x7FFFFFFF;
    if ((name & 0x7FFFFFFF) >= strings.size() || space >= strings.size()) return std::nullopt;
    return std::make_pair(strings[name & 0x7FFFFFFF], strings[space]);
  }
  return std::nullopt;
}

}  // namespace skylanders::alchemy
