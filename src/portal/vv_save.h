#pragma once
// The zones of a Vicarious Visions save (Swap Force, SuperChargers, Imaginators: the slot file
// <GAME>_<n>.xsg), for the Soul Gem run's level finishes (Core of Light #71, 2026-11-17). The file is
// Alchemy's IGB object graph (big-endian, magic 0xFADA at +40), self-describing: it lists its field
// types and classes, then the objects field by field. Read from the owner's saves (debug kit
// xsg_read.py). Every section follows the one before without padding:
//   header       12 x u32: entry size/count, meta object size/count, object size/count, memory
//                size/count, meta field size/count, 0xFADA, version and flags
//   summary      the save-select screen's ints: u32, u32 size, size/12 (kind, name offset, value
//                offset), u32 names length, the names, one byte, the values, three bytes
//   strings      u32 size (from its start), u32 count, u32, then (u32 length, bytes); a string field
//                holds a 1-based index into them (0: none)
//   meta fields  count x (u32 name length, u32, u32), then the names (igIntMetaField, ...)
//   meta objects count x (u32 name length, u32, u32, u32 own fields, u32 parent, u32 slots), then per
//                class its name and own fields (u16 meta field, u16 slot, u16 size)
//   handle names only with flag 0x04000000 (SuperChargers, Imaginators): u32 size from its start
//   pool names, an index list: each u32 size from its start
//   entries      entry size bytes of (u32 class, u32 size, ...): the objects' and memory blocks' kinds
//   refs         u32 size from its start, u32 count, count x u16 entry: an object field's reference is
//                an index into these, objects and memory blocks together
//   objects      u32, then (u32 class, u32 size from the class word, the fields in slot order, a
//                class's parents' fields first): a bool is one byte and three more, a handle 8 bytes
// A zone's state is a CZoneInfoSave (field 0: the zone's path, "nat2/nat2"). Swap Force keeps it in
// that object; SuperChargers and Imaginators in the CZoneInfoUserSession its field 1 points to. One of
// its fields goes from -1 to a value when the level is finished the first time (the game says which;
// the owner's saves before and after their first levels show it).

#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace skylanders::portal {

struct VvZone {
  std::string path;              // "nat2/nat2", "barrens/brn1/brn1", "2016/level_508/level_508"
  std::vector<uint32_t> fields;  // the state's fields in slot order (a bool as 0 or 1)
};

// The zones of a save; empty for anything else (cut short, another format).
std::vector<VvZone> ReadVvZones(std::span<const uint8_t> save);

// The collectible trackers' bits of a save (2026-11-18, the Soul Gem run): the first igBitMask its
// CCollectibleTrackerSystemSaveData points to, as the set bit numbers; bit i is word[i / 32] >> (i % 32)
// of the mask's big-endian words. The mask's words are a memory block: the blocks follow the last object,
// in the refs' order, each its igMemoryDirEntry's element count times the element size (its word 4; 8 for
// types 0x1B and 0x1C, whose word 4 says 4), each starting 4-aligned from the section's start (SuperChargers'
// options have byte blocks; with these rules every Swap Force, SuperChargers and Imaginators save adds up). In Swap Force's slot
// it has 738 bits and Soul Gem k of data\collectibletrackers.igx's SoulGems list is bit 581 + k (two
// scripted pickups, Wind-Up 612 and Grilla Drilla 587; the game saves the slot right after a pickup). Empty
// for anything else. Imaginators keeps its own Soul Gems in field 1 of CCollectibleTrackerSystemSlotSaveData (41
// bits: places 153-193 of its SoulGems list; CopyCat's gem set bit 12 = place 165): pass that class and field.
std::vector<uint32_t> ReadVvTrackerBits(std::span<const uint8_t> save,
                                       std::string_view tracker_class = "CCollectibleTrackerSystemSaveData",
                                       size_t field = 0);

// The bits in [first, first + count) set in `after` and not in `before`, as places from `first`; nothing when
// `before` can't be read (a slot seen for the first time, a file cut short).
std::vector<uint32_t> VvTrackerBitsNew(std::span<const uint8_t> before, std::span<const uint8_t> after,
                                      uint32_t first, uint32_t count,
                                      std::string_view tracker_class = "CCollectibleTrackerSystemSaveData",
                                      size_t field = 0);

// The zones whose field `slot` holds a value (not 0xFFFFFFFF) in `after` and didn't in `before` (or
// weren't there): finished for the first time, named by the last part of their path ("nat2",
// "brn1", "level_508"), in the order of the save. Nothing when `before` can't be read.
std::vector<std::string> VvZonesFinished(std::span<const uint8_t> before, std::span<const uint8_t> after,
                                         size_t slot);

}  // namespace skylanders::portal
