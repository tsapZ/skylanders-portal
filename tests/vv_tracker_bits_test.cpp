// vv_save's tracker bits (the Soul Gem run's signal in Swap Force, 2026-11-18). The save is built here from the
// layout in vv_save.h (no game file): a CCollectibleTrackerSystemSaveData pointing at an igBitMask whose 24 words
// are the second memory block, after a handle block (two 8-byte elements although its entry says 4), as in Swap
// Force's slot. A pickup sets one bit; VvTrackerBitsNew names its place in a range.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "portal/vv_save.h"
#include "test_util.h"

using skylanders::portal::ReadVvTrackerBits;
using skylanders::portal::VvTrackerBitsNew;

namespace {

void Put32(std::vector<uint8_t>& d, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) d.push_back(uint8_t(v >> s));
}
void Put16(std::vector<uint8_t>& d, uint16_t v) {
  d.push_back(uint8_t(v >> 8));
  d.push_back(uint8_t(v));
}
void PutStr(std::vector<uint8_t>& d, const std::string& s) {
  d.insert(d.end(), s.begin(), s.end());
  d.push_back(0);
}
void Sized(std::vector<uint8_t>& d, const std::vector<uint8_t>& body) {
  Put32(d, uint32_t(body.size() + 4));
  d.insert(d.end(), body.begin(), body.end());
}

const std::vector<std::string> kMetaFields = {"igObjectRefMetaField", "igBoolMetaField", "igIntMetaField",
                                              "igUnsignedIntMetaField", "igMemoryRefMetaField"};
enum Mf : uint16_t { kRef, kBool, kInt, kUInt, kMem };

struct Class {
  std::string name;
  int32_t parent;
  std::vector<std::pair<uint16_t, uint16_t>> fields;  // (meta field, slot)
};

// A memory entry's words after (class, size): 0, element count, type, flags, element size, 0x31.
void MemoryEntry(std::vector<uint8_t>& e, uint32_t count, uint32_t type) {
  Put32(e, 2);
  Put32(e, 8 + 24);
  Put32(e, 0);
  Put32(e, count);
  Put32(e, type);
  Put32(e, 0x01000000);
  Put32(e, 4);
  Put32(e, 0x31);
}

// classes: 0 igObject, 1 igObjectDirEntry, 2 igMemoryDirEntry, 3 igBitMask, 4 CCollectibleTrackerSystemSaveData
std::vector<uint8_t> Build(const std::set<uint32_t>& bits, bool tracker = true) {
  const std::vector<Class> classes = {
      {"igObject", -1, {}},
      {"igObjectDirEntry", 0, {{kInt, 0}}},
      {"igMemoryDirEntry", 0, {{kInt, 0}}},
      {"igBitMask", 0, {{kInt, 0}, {kInt, 1}, {kMem, 2}, {kUInt, 3}}},
      {tracker ? "CCollectibleTrackerSystemSaveData" : "CSomethingElse", 0, {{kRef, 0}, {kRef, 1}, {kBool, 2}}},
  };
  // refs: 0 the handle block, 1 the mask's words, 2 the tracker data (object 0), 3 the mask (object 1)
  const std::vector<uint16_t> refs = {1, 2, 0, 0};
  std::vector<uint8_t> tracker_fields, mask_fields, objects_body;
  Put32(tracker_fields, 3);           // the mask
  Put32(tracker_fields, 0xFFFFFFFF);  // none
  Put32(tracker_fields, 0x01000010);  // a true bool with junk in its padding
  Put32(mask_fields, 24);
  Put32(mask_fields, 24);
  Put32(mask_fields, 1);  // its words: ref 1
  Put32(mask_fields, 738);
  for (const auto& [cls, f] : {std::pair{4u, tracker_fields}, std::pair{3u, mask_fields}}) {
    Put32(objects_body, cls);
    Put32(objects_body, uint32_t(f.size() + 8));
    objects_body.insert(objects_body.end(), f.begin(), f.end());
  }
  std::vector<uint8_t> memory;
  for (int i = 0; i < 16; ++i) memory.push_back(uint8_t(0xA0 + i));  // two handles
  std::vector<uint32_t> words(24, 0);
  for (const uint32_t b : bits) words[b / 32] |= 1u << (b % 32);
  for (const uint32_t w : words) Put32(memory, w);

  std::vector<uint8_t> strings_body, metafields, metaobjects, entries, refs_body;
  Put32(strings_body, 1);
  Put32(strings_body, 0);
  for (const std::string& n : kMetaFields) {
    Put32(metafields, uint32_t(n.size() + 1));
    Put32(metafields, 1);
    Put32(metafields, 0);
  }
  for (const std::string& n : kMetaFields) PutStr(metafields, n);
  for (const Class& c : classes) {
    Put32(metaobjects, uint32_t(c.name.size() + 2));
    Put32(metaobjects, 1);
    Put32(metaobjects, 0);
    Put32(metaobjects, uint32_t(c.fields.size()));
    Put32(metaobjects, c.parent < 0 ? 0xFFFFFFFF : uint32_t(c.parent));
    Put32(metaobjects, uint32_t(c.fields.size()));
  }
  for (const Class& c : classes) {
    PutStr(metaobjects, c.name);
    metaobjects.push_back('x');
    for (const auto& [mf, slot] : c.fields) {
      Put16(metaobjects, mf);
      Put16(metaobjects, slot);
      Put16(metaobjects, 4);
    }
  }
  // entries: 0 an object entry, 1 the handle block (type 0x1B), 2 the mask's words (type 5)
  Put32(entries, 1);
  Put32(entries, 12);
  Put32(entries, 0);
  MemoryEntry(entries, 2, 0x1B);
  MemoryEntry(entries, 24, 5);
  Put32(refs_body, uint32_t(refs.size()));
  for (const uint16_t r : refs) Put16(refs_body, r);

  std::vector<uint8_t> d;
  Put32(d, uint32_t(entries.size()));
  Put32(d, 3);
  Put32(d, uint32_t(metaobjects.size()));
  Put32(d, uint32_t(classes.size()));
  Put32(d, uint32_t(objects_body.size()));
  Put32(d, 2);
  Put32(d, uint32_t(memory.size()));
  Put32(d, 2);
  Put32(d, uint32_t(metafields.size()));
  Put32(d, uint32_t(kMetaFields.size()));
  Put32(d, 0xFADA);
  Put32(d, 0xB000000C);
  Put32(d, 3);
  Put32(d, 12);
  Put32(d, 0);
  Put32(d, 0);
  Put32(d, 0);
  Put32(d, 13);
  PutStr(d, "save_version");
  d.push_back(0);
  Put32(d, 5193);
  d.insert(d.end(), 3, 0);
  Sized(d, strings_body);
  d.insert(d.end(), metafields.begin(), metafields.end());
  d.insert(d.end(), metaobjects.begin(), metaobjects.end());
  std::vector<uint8_t> pools;
  Put32(pools, 1);
  PutStr(pools, "MP_DEFAULT");
  Sized(d, pools);
  std::vector<uint8_t> list;
  Put32(list, 1);
  Put32(list, 0);
  Sized(d, list);
  d.insert(d.end(), entries.begin(), entries.end());
  Sized(d, refs_body);
  Put32(d, 2);
  d.insert(d.end(), objects_body.begin(), objects_body.end());
  d.insert(d.end(), memory.begin(), memory.end());
  d.insert(d.end(), 64, 0);  // the file's padding
  return d;
}

}  // namespace

int main() {
  // the owner's slot had Pop Thorn (581 + 24) and a few others; the pickup adds Wind-Up (581 + 31)
  const auto before = Build({0, 3, 605, 737});
  const auto after = Build({0, 3, 605, 612, 737});
  const auto bits = ReadVvTrackerBits(after);
  REQUIRE_OR_RETURN(bits.size() == 5);
  CHECK(bits[0] == 0);
  CHECK(bits[3] == 612);
  CHECK(bits[4] == 737);

  const auto found = VvTrackerBitsNew(before, after, 581, 32);
  REQUIRE_OR_RETURN(found.size() == 1);
  CHECK(found[0] == 31);
  // outside the range: nothing; the same save twice: nothing; a new slot: every gem in it
  CHECK(VvTrackerBitsNew(before, after, 0, 32).empty());
  CHECK(VvTrackerBitsNew(after, after, 581, 32).empty());
  const auto fresh = VvTrackerBitsNew({}, after, 581, 32);
  REQUIRE_OR_RETURN(fresh.size() == 2);
  CHECK(fresh[0] == 24 && fresh[1] == 31);

  // no tracker data, cut short anywhere, or junk: nothing
  CHECK(ReadVvTrackerBits(Build({612}, false)).empty());
  const auto good = Build({612});
  CHECK(ReadVvTrackerBits(good).size() == 1);
  for (size_t n = 0; n < good.size() - 64 - 112; n += 5) {
    CHECK(ReadVvTrackerBits(std::span<const uint8_t>(good.data(), n)).empty());
  }
  std::vector<uint8_t> junk(4096, 0xAB);
  CHECK(ReadVvTrackerBits(junk).empty());
  return Finish("vv_tracker_bits");
}
