// vv_save: the zones of a Vicarious Visions save (Swap Force, SuperChargers, Imaginators; Core of Light
// #71). The saves are built here from the layout in vv_save.h (no game file): a zone's state inline (Swap
// Force) and in a CZoneInfoUserSession the zone points to (SuperChargers, Imaginators), each before and
// after a level's first finish, as in the owner's saves of 2026-10-25 (SuperChargers' intro1: the third
// field of its state went from -1 to 1).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "portal/vv_save.h"
#include "test_util.h"

using skylanders::portal::ReadVvZones;
using skylanders::portal::VvZonesFinished;

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
// A section that starts with its own size.
void Sized(std::vector<uint8_t>& d, const std::vector<uint8_t>& body) {
  Put32(d, uint32_t(body.size() + 4));
  d.insert(d.end(), body.begin(), body.end());
}

const std::vector<std::string> kMetaFields = {"igObjectRefMetaField", "igBoolMetaField",   "igIntMetaField",
                                              "igUnsignedIntMetaField", "igStringMetaField", "igEnumMetaField",
                                              "igHandleMetaField"};
enum Mf : uint16_t { kRef, kBool, kInt, kUInt, kString, kEnum, kHandle };

struct Class {
  std::string name;
  int32_t parent;
  std::vector<std::pair<uint16_t, uint16_t>> fields;  // (meta field, slot)
};

struct Object {
  uint32_t cls;
  std::vector<uint8_t> fields;
};

// classes: 0 igObject, 1 igObjectDirEntry, 2 igMemoryDirEntry, 3 CZoneInfoSave, 4 CZoneInfoUserSession
std::vector<uint8_t> Build(bool inline_state, const std::vector<std::pair<std::string, uint32_t>>& zones,
                           bool handles) {
  std::vector<Class> classes = {{"igObject", -1, {}},
                                {"igObjectDirEntry", 0, {{kString, 0}, {kInt, 4}, {kInt, 5}}},
                                {"igMemoryDirEntry", 0, {{kString, 0}, {kInt, 3}}}};
  if (inline_state) {
    // Swap Force: path, u32, int, enum (finished), bool, a handle, ref
    classes.push_back({"CZoneInfoSave", 0, {{kString, 0}, {kUInt, 1}, {kInt, 2}, {kEnum, 3}, {kBool, 4}, {kHandle, 5}}});
  } else {
    classes.push_back({"CZoneInfoSave", 0, {{kString, 0}, {kRef, 1}}});
    classes.push_back({"CZoneInfoUserSession", 0, {{kUInt, 0}, {kInt, 1}, {kEnum, 2}, {kBool, 3}, {kInt, 4}}});
  }
  // objects and refs: a memory block first (refs count them too), then per zone its save (and state)
  std::vector<Object> objects;
  std::vector<uint16_t> refs = {1};  // entry 1: the memory block
  for (size_t i = 0; i < zones.size(); ++i) {
    const uint32_t finished = zones[i].second;
    std::vector<uint8_t> f;
    Put32(f, uint32_t(i + 1));  // the path: 1-based string index
    if (inline_state) {
      Put32(f, 7);
      Put32(f, 0x33);
      Put32(f, finished);
      Put32(f, 0x01000010);  // a true bool with junk in its padding
      Put32(f, 0x12345678);
      Put32(f, 0x9ABCDEF0);
      objects.push_back({3, f});
      refs.push_back(0);
    } else {
      // the save's ref points at the state, which comes right after it
      Put32(f, uint32_t(refs.size() + 1));
      objects.push_back({3, f});
      refs.push_back(0);
      std::vector<uint8_t> st;
      Put32(st, 0);
      Put32(st, 0);
      Put32(st, finished);
      Put32(st, 0x00000010);  // false, junk in the padding
      Put32(st, finished == 0xFFFFFFFF ? 0 : 1);
      objects.push_back({4, st});
      refs.push_back(0);
    }
  }
  std::vector<uint8_t> strings_body, metafields, metaobjects, entries, refs_body, objects_body;
  // strings: count is one more than the strings (0 is none)
  Put32(strings_body, uint32_t(zones.size() + 1));
  Put32(strings_body, 0);
  for (const auto& [path, _] : zones) {
    Put32(strings_body, uint32_t(path.size() + 1));
    PutStr(strings_body, path);
  }
  for (const std::string& n : kMetaFields) {
    Put32(metafields, uint32_t(n.size() + 1));
    Put32(metafields, 1);
    Put32(metafields, 0);
  }
  for (const std::string& n : kMetaFields) PutStr(metafields, n);
  for (const Class& c : classes) {
    Put32(metaobjects, uint32_t(c.name.size() + 2));  // one junk byte after the NUL, as in the saves
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
  // entries: 0 an object entry, 1 a memory entry
  Put32(entries, 1);
  Put32(entries, 20);
  Put32(entries, 0);
  Put32(entries, 3);
  Put32(entries, 0);
  Put32(entries, 2);
  Put32(entries, 16);
  Put32(entries, 0);
  Put32(entries, 0);
  Put32(refs_body, uint32_t(refs.size()));
  for (const uint16_t r : refs) Put16(refs_body, r);
  for (const Object& o : objects) {
    Put32(objects_body, o.cls);
    Put32(objects_body, uint32_t(o.fields.size() + 8));
    objects_body.insert(objects_body.end(), o.fields.begin(), o.fields.end());
  }

  std::vector<uint8_t> d;
  Put32(d, uint32_t(entries.size()));
  Put32(d, 2);
  Put32(d, uint32_t(metaobjects.size()));
  Put32(d, uint32_t(classes.size()));
  Put32(d, uint32_t(objects_body.size()));
  Put32(d, uint32_t(objects.size()));
  Put32(d, 0);
  Put32(d, 1);
  Put32(d, uint32_t(metafields.size()));
  Put32(d, uint32_t(kMetaFields.size()));
  Put32(d, 0xFADA);
  Put32(d, handles ? 0xB400000C : 0xB000000C);
  // summary: one value ("save_version")
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
  if (handles) {
    std::vector<uint8_t> h;
    Put32(h, 1);
    Put32(h, 1);
    Put32(h, 0xCAFEF00D);
    PutStr(h, "gamevariables");
    Sized(d, h);
  }
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
  Put32(d, uint32_t(objects.size()));
  d.insert(d.end(), objects_body.begin(), objects_body.end());
  d.insert(d.end(), 64, 0);  // the memory blocks and the file's padding
  return d;
}

constexpr uint32_t kNone = 0xFFFFFFFF;

}  // namespace

int main() {
  for (const bool inline_state : {false, true}) {
    const size_t slot = inline_state ? 3 : 2;
    const auto before = Build(inline_state, {{"intro/intro", 1}, {"intro1/intro1", kNone}, {"nat1/nat1", kNone}},
                              !inline_state);
    const auto after = Build(inline_state, {{"intro/intro", 1}, {"intro1/intro1", 1}, {"nat1/nat1", kNone}},
                             !inline_state);
    const auto zones = ReadVvZones(after);
    REQUIRE_OR_RETURN(zones.size() == 3);
    CHECK(zones[1].path == "intro1/intro1");
    CHECK(zones[1].fields.size() == (inline_state ? 6u : 5u));
    CHECK(zones[1].fields[slot] == 1);
    CHECK(zones[2].fields[slot] == kNone);
    // a bool is its first byte, the padding's junk dropped
    CHECK(zones[0].fields[inline_state ? 4 : 3] == (inline_state ? 1u : 0u));

    // intro1 finished between the two saves; intro was done before, nat1 not yet
    const auto finished = VvZonesFinished(before, after, slot);
    REQUIRE_OR_RETURN(finished.size() == 1);
    CHECK(finished[0] == "intro1");
    CHECK(VvZonesFinished(after, after, slot).empty());
    // a new slot: everything finished in it counts
    CHECK(VvZonesFinished({}, after, slot).size() == 2);
    // a slot field past the state's end: nothing
    CHECK(VvZonesFinished(before, after, 40).empty());
  }
  // not a save: nothing, also when cut short anywhere
  const auto good = Build(false, {{"nat2/nat2", 1}}, true);
  CHECK(ReadVvZones(good).size() == 1);
  for (size_t n = 0; n < good.size() - 64; n += 7) {
    CHECK(ReadVvZones(std::span<const uint8_t>(good.data(), n)).empty());
  }
  std::vector<uint8_t> junk(4096, 0xAB);
  CHECK(ReadVvZones(junk).empty());
  return Finish("vv_save");
}
