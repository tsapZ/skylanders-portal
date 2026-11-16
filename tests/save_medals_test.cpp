// save_medals: the Soul Gem item in a level's group of Spyro's Adventure's slot save (Core of Light #71).
// The bytes are built here from the layout (no game file): the groups as they stood before and after
// Chop Chop's Soul Gem was picked up in Level_027, 2026-11-16.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "portal/save_medals.h"

namespace {

int g_failed = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++g_failed;
  }
}

void Put32(std::vector<uint8_t>& d, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) d.push_back(uint8_t(v >> s));
}
void Put16(std::vector<uint8_t>& d, uint16_t v) {
  d.push_back(uint8_t(v >> 8));
  d.push_back(uint8_t(v));
}
void Group(std::vector<uint8_t>& d, uint32_t key, std::vector<std::pair<uint32_t, uint32_t>> items) {
  Put32(d, key);
  Put16(d, uint16_t(items.size()));
  Put16(d, 0);
  for (const auto& [k, v] : items) {
    Put32(d, k);
    Put32(d, v);
  }
}

}  // namespace

int main() {
  using namespace skylanders::portal;
  Check(NameCrc32("Level_027") == 0x2F2FA0CC, "crc32 Level_027");
  Check(NameCrc32("champion upgrade medal") == kSsaSoulGemItem, "crc32 of the medal");
  Check(NameCrc32("SpyroLevelCollectionGameDataSet") == 0x22F3E0FF, "crc32 of the collection set");
  const uint32_t level = NameCrc32("Level_027"), intro = NameCrc32("Sc09_IntroEon");

  // Before: the level's group has the intro only. A float block in front, as in the real file.
  std::vector<uint8_t> before = {0x3F, 0x80, 0x00, 0x00, 0x00, 0x02};
  Group(before, level, {{intro, 1}});
  Group(before, 0xC5767A38, {{0x0FA54217, 1}});
  Check(GroupItem(before, level, kSsaSoulGemItem) == 0, "no gem before");
  Check(GroupItem(before, level, intro) == 1, "the intro before");

  // After: the collection set and the level both have the medal.
  std::vector<uint8_t> after = {0x3F, 0x80, 0x00, 0x00, 0x00, 0x03};
  Group(after, 0x22F3E0FF, {{kSsaSoulGemItem, 1}});
  Group(after, level, {{kSsaSoulGemItem, 1}, {intro, 1}});
  Check(GroupItem(after, level, kSsaSoulGemItem) == 1, "the gem after");
  Check(GroupItem(after, NameCrc32("Level_021"), kSsaSoulGemItem) == 0, "another level has none");

  // The key's bytes inside other data, with a header that doesn't fit: passed over.
  std::vector<uint8_t> noise;
  Put32(noise, level);
  Put16(noise, 9999);
  Put16(noise, 7);
  Group(noise, level, {{kSsaSoulGemItem, 1}});
  Check(GroupItem(noise, level, kSsaSoulGemItem) == 1, "a bad header first, the real group after");
  std::vector<uint8_t> cut;
  Put32(cut, level);
  Put16(cut, 3);
  Put16(cut, 0);
  Put32(cut, kSsaSoulGemItem);
  Check(GroupItem(cut, level, kSsaSoulGemItem) == 0, "a group cut off by the end");
  Check(GroupItem({}, level, kSsaSoulGemItem) == 0, "an empty save");

  if (g_failed) return EXIT_FAILURE;
  std::printf("all save_medals tests passed\n");
  return EXIT_SUCCESS;
}
