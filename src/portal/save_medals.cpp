#include "portal/save_medals.h"

namespace skylanders::portal {
namespace {

uint32_t Be32(std::span<const uint8_t> d, size_t at) {
  return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | d[at + 3];
}
uint16_t Be16(std::span<const uint8_t> d, size_t at) { return uint16_t((d[at] << 8) | d[at + 1]); }

}  // namespace

uint32_t NameCrc32(std::string_view name) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char c : name) {
    crc ^= uint8_t(c);
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint32_t GroupItem(std::span<const uint8_t> save, uint32_t group, uint32_t item) {
  for (size_t at = 0; at + 8 <= save.size(); at += 2) {
    if (Be32(save, at) != group) continue;
    const uint16_t count = Be16(save, at + 4);
    if (count == 0 || count > 512 || Be16(save, at + 6) != 0) continue;
    const size_t end = at + 8 + size_t(count) * 8;
    if (end > save.size()) continue;
    for (size_t i = at + 8; i < end; i += 8) {
      if (Be32(save, i) == item) return Be32(save, i + 4);
    }
  }
  return 0;
}

}  // namespace skylanders::portal
