#pragma once

// What an Xbox 360 default.xex says about itself in its header, read without decrypting anything:
// title ID, version, media ID, disc, regions, and the page table. The table has one descriptor per
// run of image pages, with the pages' kind (code, data, read-only data) and a SHA-1 of their
// loaded bytes. Two pressings of one game (SuperChargers' En/De and Es/It discs) have different
// files, keys and media IDs, but the same descriptors from the first code page to the end: the same
// code at the same addresses, only some read-only data in front of it differs. That part of the
// table, hashed, is a disc's "code pin": a pressing with the pin of a supported disc runs the same
// recompiled code. Header only: the launcher hashes with Windows' CNG, the games with the SDK's
// SHA-256.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace skylanders::xex {

struct XexHeader {
  uint32_t title_id = 0;
  uint32_t version = 0;  // major 4 bits, minor 4, build 16, qfe 8
  uint32_t media_id = 0;
  uint32_t region = 0;   // 0xFF North America, 0xFF00 Japan and Asia, 0xFF0000 Europe and Australia
  uint8_t disc = 0;
  uint8_t disc_count = 0;
  uint32_t code_address = 0;  // where the first code page loads; 0 without code pages
  // The page descriptors from the first code page to the end, as bytes into the data given to
  // ParseXexHeader; the code pin is "<code_address as 8 hex digits>:<their SHA-256>".
  size_t code_pages_offset = 0;
  size_t code_pages_size = 0;
};

namespace detail {
inline uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
}  // namespace detail

// The header of the XEX2 file starting at `data` (at least its header: the size at offset 8), or
// nullopt when it isn't one or is cut short.
inline std::optional<XexHeader> ParseXexHeader(const uint8_t* data, size_t size) {
  using detail::Be32;
  if (size < 0x18 || data[0] != 'X' || data[1] != 'E' || data[2] != 'X' || data[3] != '2') return std::nullopt;
  const size_t header_size = Be32(data + 0x08);
  const size_t security = Be32(data + 0x10);
  const size_t count = Be32(data + 0x14);
  if (header_size > size || count > header_size / 8 || 0x18 + count * 8 > header_size ||
      security > header_size || header_size - security < 0x184) {
    return std::nullopt;
  }
  XexHeader out;
  bool execution = false;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* entry = data + 0x18 + i * 8;
    if (Be32(entry) != 0x00040006) continue;  // the execution info
    const size_t at = Be32(entry + 4);
    if (at > header_size || header_size - at < 24) return std::nullopt;
    out.media_id = Be32(data + at);
    out.version = Be32(data + at + 4);
    out.title_id = Be32(data + at + 12);
    out.disc = data[at + 18];
    out.disc_count = data[at + 19];
    execution = true;
  }
  if (!execution) return std::nullopt;
  const uint8_t* sec = data + security;
  out.region = Be32(sec + 0x178);
  const uint32_t page_size = (Be32(sec + 0x10C) & 0x10000000) ? 0x1000 : 0x10000;
  const uint32_t load_address = Be32(sec + 0x110);
  const size_t pages = Be32(sec + 0x180);
  if (pages > (header_size - security - 0x184) / 24) return std::nullopt;
  uint64_t page = 0;
  for (size_t i = 0; i < pages; ++i) {
    const uint32_t value = Be32(sec + 0x184 + i * 24);
    if ((value & 0xF) == 1) {  // XEX_SECTION_CODE
      out.code_address = uint32_t(load_address + page * page_size);
      out.code_pages_offset = security + 0x184 + i * 24;
      out.code_pages_size = (pages - i) * 24;
      break;
    }
    page += value >> 4;
  }
  return out;
}

// "North America", "Europe and Australia", ..., or "all regions".
inline std::string XexRegionName(uint32_t region) {
  if (region == 0xFFFFFFFF) return "all regions";
  std::string out;
  auto add = [&](uint32_t mask, const char* name) {
    if (!(region & mask)) return;
    if (!out.empty()) out += ", ";
    out += name;
  };
  add(0x000000FF, "North America");
  add(0x0000FF00, "Japan and Asia");
  add(0x00FF0000, "Europe and Australia");
  add(0xFF000000, "other");
  return out.empty() ? "none" : out;
}

// "title 41560911, version 0.0.0.20, Europe and Australia (00FF0000), media 7A74A451, disc 1/1".
inline std::string DescribeXexHeader(const XexHeader& h) {
  char text[200];
  std::snprintf(text, sizeof(text), "title %08X, version %u.%u.%u.%u, %s (%08X), media %08X, disc %u/%u", h.title_id,
                h.version >> 28, (h.version >> 24) & 0xF, (h.version >> 8) & 0xFFFF, h.version & 0xFF,
                XexRegionName(h.region).c_str(), h.region, h.media_id, h.disc, h.disc_count);
  return text;
}

// The code pin from the code address and the SHA-256 (hex) of the code pages' descriptors.
inline std::string CodePin(const XexHeader& h, const std::string& code_pages_sha256) {
  char address[16];
  std::snprintf(address, sizeof(address), "%08X", h.code_address);
  return std::string(address) + ":" + code_pages_sha256;
}

}  // namespace skylanders::xex
