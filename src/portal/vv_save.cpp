#include "portal/vv_save.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace skylanders::portal {
namespace {

// Reads big-endian values; any read past the end makes `ok` false and returns 0.
struct Reader {
  std::span<const uint8_t> d;
  size_t at = 0;
  bool ok = true;

  bool Has(size_t n) const { return ok && at <= d.size() && d.size() - at >= n; }
  uint32_t U32() {
    if (!Has(4)) return Fail();
    const uint32_t v = uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
    at += 4;
    return v;
  }
  uint16_t U16() {
    if (!Has(2)) return uint16_t(Fail());
    const uint16_t v = uint16_t(d[at] << 8 | d[at + 1]);
    at += 2;
    return v;
  }
  // `n` bytes as a string up to the first NUL.
  std::string Str(size_t n) {
    if (!Has(n)) return Fail(), std::string();
    const auto* p = reinterpret_cast<const char*>(d.data() + at);
    at += n;
    return std::string(p, std::find(p, p + n, '\0'));
  }
  void Skip(size_t n) {
    if (!Has(n)) Fail();
    else at += n;
  }
  // Jumps to `start` + `size` (a section that gives its own size), not backwards.
  void SkipTo(size_t start, size_t size) {
    if (start + size < at || start + size > d.size()) Fail();
    else at = start + size;
  }
  uint32_t Fail() {
    ok = false;
    return 0;
  }
};

struct MetaObject {
  std::string name;
  int32_t parent = -1;
  std::vector<std::pair<uint16_t, uint16_t>> fields;  // (slot, meta field)
};

// The bytes a field of this meta field takes in an object; nullopt for one not seen in these saves.
std::optional<size_t> FieldSize(std::string_view type) {
  static const std::map<std::string_view, size_t> kSizes = {
      {"igIntMetaField", 4},        {"igUnsignedIntMetaField", 4}, {"igEnumMetaField", 4},
      {"igBoolMetaField", 4},       {"igFloatMetaField", 4},       {"igStringMetaField", 4},
      {"igObjectRefMetaField", 4},  {"igMemoryRefMetaField", 4},   {"igSizeTypeMetaField", 4},
      {"igRawRefMetaField", 4},     {"igTimeMetaField", 4},        {"igHandleMetaField", 8},
      {"igUnsignedShortMetaField", 2}, {"igShortMetaField", 2},    {"igUnsignedCharMetaField", 1},
      {"igCharMetaField", 1},       {"igVec2fMetaField", 8},       {"igVec3fMetaField", 12},
      {"igVec4fMetaField", 16},     {"igLongMetaField", 8},        {"igUnsignedLongMetaField", 8},
      {"igBitFieldMetaField", 0},   {"igVectorMetaField", 12},
  };
  const auto it = kSizes.find(type);
  if (it == kSizes.end()) return std::nullopt;
  return it->second;
}

struct Save {
  std::vector<std::string> strings;
  std::vector<std::string> meta_fields;
  std::vector<MetaObject> meta_objects;
  std::vector<std::pair<uint32_t, std::span<const uint8_t>>> objects;  // (class, fields)
  std::vector<int32_t> ref_object;  // per ref: the object's index, -1 for a memory block
  std::vector<uint16_t> ref_entry;  // per ref: its entry
  std::vector<std::span<const uint8_t>> entries;  // each entry's words after (class, size)
  size_t memory_at = 0;    // the memory blocks, right after the last object
  size_t memory_size = 0;  // the header's memory size
};

bool Parse(std::span<const uint8_t> data, Save& s) {
  Reader r{data};
  uint32_t h[12];
  for (uint32_t& w : h) w = r.U32();
  if (!r.ok || h[10] != 0xFADA) return false;
  // summary
  r.U32();
  const uint32_t summary_size = r.U32();
  if (summary_size % 12 || summary_size > 0x1000) return false;
  uint32_t last_value = 0;
  for (uint32_t i = 0; i < summary_size / 12; ++i) {
    r.U32();
    r.U32();
    last_value = std::max(last_value, r.U32() + 4);
  }
  r.Skip(r.U32());
  r.Skip(1 + last_value + 3);
  // strings
  const size_t strings_at = r.at;
  const uint32_t strings_size = r.U32();
  const uint32_t string_count = r.U32();
  r.U32();
  while (r.ok && r.at < strings_at + strings_size && s.strings.size() < string_count) {
    s.strings.push_back(r.Str(r.U32()));
  }
  r.SkipTo(strings_at, strings_size);
  // meta fields
  const size_t fields_at = r.at;
  std::vector<uint32_t> name_lengths;
  for (uint32_t i = 0; i < h[9] && r.ok; ++i) {
    name_lengths.push_back(r.U32());
    r.Skip(8);
  }
  for (const uint32_t n : name_lengths) s.meta_fields.push_back(r.Str(n));
  r.SkipTo(fields_at, h[8]);
  // meta objects
  const size_t objects_meta_at = r.at;
  if (h[2] > data.size() || h[3] > h[2] / 24) return false;  // 6 words a record (a damaged header)
  std::vector<std::array<uint32_t, 6>> records(h[3]);
  for (auto& rec : records) {
    for (uint32_t& w : rec) w = r.U32();
  }
  for (const auto& rec : records) {
    if (!r.ok) return false;
    MetaObject m;
    m.name = r.Str(rec[0]);
    m.parent = rec[4] == 0xFFFFFFFF ? -1 : int32_t(rec[4]);
    for (uint32_t i = 0; i < rec[3] && r.ok; ++i) {
      const uint16_t type = r.U16();
      const uint16_t slot = r.U16();
      r.U16();
      m.fields.emplace_back(slot, type);
    }
    std::sort(m.fields.begin(), m.fields.end());
    s.meta_objects.push_back(std::move(m));
  }
  r.SkipTo(objects_meta_at, h[2]);
  // handle names (SuperChargers and Imaginators), pool names, an index list
  if (h[11] & 0x04000000) {
    const size_t at = r.at;
    r.SkipTo(at, r.U32());
  }
  for (int i = 0; i < 2 && r.ok; ++i) {
    const size_t at = r.at;
    r.SkipTo(at, r.U32());
  }
  // entries: the class of each (an object's: igObjectDirEntry)
  const size_t entries_at = r.at;
  std::vector<uint32_t> entry_class;
  while (r.ok && r.at < entries_at + h[0]) {
    const size_t at = r.at;
    entry_class.push_back(r.U32());
    const uint32_t size = r.U32();
    if (size < 8) return false;
    r.SkipTo(at, size);
    if (r.ok) s.entries.push_back(data.subspan(at + 8, size - 8));
  }
  r.SkipTo(entries_at, h[0]);
  uint32_t object_entry = 0xFFFFFFFF;
  for (size_t i = 0; i < s.meta_objects.size(); ++i) {
    if (s.meta_objects[i].name == "igObjectDirEntry") object_entry = uint32_t(i);
  }
  // refs
  const size_t refs_at = r.at;
  const uint32_t refs_size = r.U32();
  const uint32_t ref_count = r.U32();
  int32_t objects_seen = 0;
  for (uint32_t i = 0; i < ref_count && r.ok; ++i) {
    const uint16_t entry = r.U16();
    const bool object = entry < entry_class.size() && entry_class[entry] == object_entry;
    s.ref_object.push_back(object ? objects_seen++ : -1);
    s.ref_entry.push_back(entry);
  }
  r.SkipTo(refs_at, refs_size);
  // objects
  r.U32();
  const size_t objects_at = r.at;
  while (r.ok && r.at < objects_at + h[4]) {
    const size_t at = r.at;
    const uint32_t cls = r.U32();
    const uint32_t size = r.U32();
    if (size < 8 || cls >= s.meta_objects.size()) return false;
    r.SkipTo(at, size);
    if (r.ok) s.objects.emplace_back(cls, data.subspan(at + 8, size - 8));
  }
  s.memory_at = objects_at + h[4];
  s.memory_size = h[6];
  return r.ok && !s.objects.empty();
}

// An object's fields in slot order as u32 (a bool's first byte; a string's index), nullopt when a
// field's type has no known size or the bytes don't add up.
std::optional<std::vector<uint32_t>> Fields(const Save& s, uint32_t cls, std::span<const uint8_t> raw) {
  std::vector<const MetaObject*> chain;
  for (int32_t c = int32_t(cls); c >= 0 && c < int32_t(s.meta_objects.size()) && chain.size() < 32;
       c = s.meta_objects[c].parent) {
    chain.push_back(&s.meta_objects[c]);
  }
  std::map<uint16_t, uint16_t> slots;  // a class's own entry for a slot wins over its parent's
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (const auto& [slot, type] : (*it)->fields) slots[slot] = type;
  }
  std::vector<uint32_t> out;
  size_t at = 0;
  for (const auto& [slot, type] : slots) {
    if (type >= s.meta_fields.size()) return std::nullopt;
    const std::string_view name = s.meta_fields[type];
    const std::optional<size_t> n = FieldSize(name);
    if (!n || raw.size() - at < *n) return std::nullopt;
    uint32_t v = 0;
    if (name == "igBoolMetaField") {
      v = raw[at] ? 1 : 0;
    } else {
      for (size_t i = 0; i < std::min<size_t>(*n, 4); ++i) v = v << 8 | raw[at + i];
    }
    out.push_back(v);
    at += *n;
  }
  if (at != raw.size()) return std::nullopt;
  return out;
}

// The meta field type name of a class's field `index` in slot order (as Fields counts), "" when there is none.
std::string_view SlotType(const Save& s, uint32_t cls, size_t index) {
  std::map<uint16_t, uint16_t> slots;
  std::vector<const MetaObject*> chain;
  for (int32_t c = int32_t(cls); c >= 0 && c < int32_t(s.meta_objects.size()) && chain.size() < 32;
       c = s.meta_objects[c].parent) {
    chain.push_back(&s.meta_objects[c]);
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (const auto& [slot, type] : (*it)->fields) slots[slot] = type;
  }
  for (const auto& [slot, type] : slots) {
    if (index-- == 0) return type < s.meta_fields.size() ? std::string_view(s.meta_fields[type]) : std::string_view();
  }
  return {};
}

uint32_t Be32(std::span<const uint8_t> d, size_t at) {
  if (at > d.size() || d.size() - at < 4) return 0;
  return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}

int32_t ClassIndex(const Save& s, std::string_view name) {
  for (size_t i = 0; i < s.meta_objects.size(); ++i) {
    if (s.meta_objects[i].name == name) return int32_t(i);
  }
  return -1;
}

std::string LastPart(std::string_view path) {
  const size_t slash = path.rfind('/');
  return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

}  // namespace

std::vector<VvZone> ReadVvZones(std::span<const uint8_t> save) {
  Save s;
  if (!Parse(save, s)) return {};
  int32_t zone_class = -1;
  for (size_t i = 0; i < s.meta_objects.size(); ++i) {
    if (s.meta_objects[i].name == "CZoneInfoSave") zone_class = int32_t(i);
  }
  std::vector<VvZone> out;
  if (zone_class < 0) return out;
  for (const auto& [cls, raw] : s.objects) {
    if (int32_t(cls) != zone_class) continue;
    const auto fields = Fields(s, cls, raw);
    if (!fields || fields->empty() || (*fields)[0] == 0 || (*fields)[0] > s.strings.size()) continue;
    VvZone zone;
    zone.path = s.strings[(*fields)[0] - 1];
    if (fields->size() == 2 && SlotType(s, cls, 1) == "igObjectRefMetaField") {
      // SuperChargers, Imaginators: the state is the object field 1 points to.
      const uint32_t ref = (*fields)[1];
      if (ref >= s.ref_object.size() || s.ref_object[ref] < 0 ||
          size_t(s.ref_object[ref]) >= s.objects.size()) {
        continue;
      }
      const auto& [state_class, state_raw] = s.objects[size_t(s.ref_object[ref])];
      const auto state = Fields(s, state_class, state_raw);
      if (!state) continue;
      zone.fields = *state;
    } else {
      zone.fields = *fields;
    }
    out.push_back(std::move(zone));
  }
  return out;
}

namespace {

// ReadVvTrackerBits, nullopt when the save can't be read (apart from a save without set bits).
std::optional<std::vector<uint32_t>> TrackerBits(std::span<const uint8_t> save, std::string_view tracker_class,
                                                 size_t field) {
  Save s;
  if (!Parse(save, s)) return std::nullopt;
  // The memory blocks' places, per ref (only the memory refs get one).
  std::vector<std::pair<size_t, size_t>> block(s.ref_entry.size(), {0, 0});
  size_t at = s.memory_at;
  for (size_t i = 0; i < s.ref_entry.size(); ++i) {
    if (s.ref_object[i] >= 0) continue;
    const uint16_t e = s.ref_entry[i];
    if (e >= s.entries.size() || s.entries[e].size() < 20) return std::nullopt;
    const uint32_t count = Be32(s.entries[e], 4);
    const uint32_t type = Be32(s.entries[e], 8);
    const uint32_t each = (type == 0x1B || type == 0x36) ? 8 : Be32(s.entries[e], 16);
    if (each == 0 || each > 16 || count > 0x100000) return std::nullopt;
    block[i] = {at, size_t(count) * each};
    at += size_t(count) * each;
  }
  if (at > s.memory_at + s.memory_size || at > save.size()) return std::nullopt;
  const int32_t owner_class = ClassIndex(s, tracker_class);
  const int32_t mask_class = ClassIndex(s, "igBitMask");
  if (owner_class < 0 || mask_class < 0) return std::nullopt;
  for (const auto& [cls, raw] : s.objects) {
    if (int32_t(cls) != owner_class) continue;
    const auto fields = Fields(s, cls, raw);
    if (!fields || fields->size() <= field) return std::nullopt;
    const uint32_t ref = (*fields)[field];
    if (ref >= s.ref_object.size() || s.ref_object[ref] < 0 ||
        size_t(s.ref_object[ref]) >= s.objects.size()) {
      return std::nullopt;
    }
    const auto& [m_cls, m_raw] = s.objects[size_t(s.ref_object[ref])];
    if (int32_t(m_cls) != mask_class) return std::nullopt;
    const auto mask = Fields(s, m_cls, m_raw);  // word count, its copy, the memory ref, the bit count
    if (!mask || mask->size() < 4) return std::nullopt;
    const uint32_t words = (*mask)[0];
    const uint32_t mem = (*mask)[2];
    if (mem >= block.size() || s.ref_object[mem] >= 0 || block[mem].second < size_t(words) * 4) return std::nullopt;
    std::vector<uint32_t> out;
    for (uint32_t w = 0; w < words; ++w) {
      const uint32_t v = Be32(save, block[mem].first + 4 * w);
      for (uint32_t b = 0; b < 32; ++b) {
        if (v >> b & 1) out.push_back(w * 32 + b);
      }
    }
    return out;
  }
  return std::vector<uint32_t>{};  // no tracker object: nothing set
}

}  // namespace

std::vector<uint32_t> ReadVvTrackerBits(std::span<const uint8_t> save, std::string_view tracker_class,
                                       size_t field) {
  return TrackerBits(save, tracker_class, field).value_or(std::vector<uint32_t>{});
}

std::vector<uint32_t> VvTrackerBitsNew(std::span<const uint8_t> before, std::span<const uint8_t> after,
                                      uint32_t first, uint32_t count, std::string_view tracker_class,
                                      size_t field) {
  // A `before` that can't be read (cut short, empty: a slot seen for the first time) gives nothing: taking
  // it as "no bits" would count every gem of `after` as found now. `after` is the next baseline.
  const auto had = TrackerBits(before, tracker_class, field);
  if (!had) return {};
  const std::set<uint32_t> old(had->begin(), had->end());
  std::vector<uint32_t> out;
  for (const uint32_t bit : ReadVvTrackerBits(after, tracker_class, field)) {
    if (bit >= first && bit - first < count && !old.contains(bit)) out.push_back(bit - first);
  }
  return out;
}

std::vector<std::string> VvZonesFinished(std::span<const uint8_t> before, std::span<const uint8_t> after,
                                         size_t slot) {
  // As VvTrackerBitsNew: a `before` that isn't a readable save gives nothing (else every finished zone of
  // `after` would count as a first finish).
  Save check;
  if (!Parse(before, check)) return {};
  std::set<std::string> done_before;
  for (const VvZone& z : ReadVvZones(before)) {
    if (slot < z.fields.size() && z.fields[slot] != 0xFFFFFFFF) done_before.insert(z.path);
  }
  std::vector<std::string> out;
  for (const VvZone& z : ReadVvZones(after)) {
    if (slot < z.fields.size() && z.fields[slot] != 0xFFFFFFFF && !done_before.contains(z.path)) {
      out.push_back(LastPart(z.path));
    }
  }
  return out;
}

}  // namespace skylanders::portal
