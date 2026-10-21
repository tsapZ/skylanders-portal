#pragma once

#include <string>
#include <string_view>

#include "portal/figure_catalog.h"

namespace skylanders::portal {

// A Skylander's element. Items, traps without a villain and unknown figures have none.
enum class Element {
  kNone,
  kMagic,
  kEarth,
  kWater,
  kFire,
  kTech,
  kUndead,
  kLife,
  kAir,
  kLight,
  kDark,
};

// The element of a catalog entry. Variants ("Legendary Chop Chop", "Anchors Away Gill Grunt
// (Series 3)", "Nitro Freeze Blade Top") get the element of the character they are a variant of.
// Covers every game up to Imaginators (its creation crystals by toy id); kNone for items, traps,
// unreleased entries and Kaos.
Element FigureElement(const SkylanderInfo& sky);

// The character a figure belongs to: "Chop Chop" for "Legendary Chop Chop" and "Twin Blade Chop
// Chop (Series 3)", "Freeze Blade" for "Nitro Freeze Blade Top". Same coverage as FigureElement;
// empty for items and figures it doesn't know.
std::string_view CharacterName(const SkylanderInfo& sky);

// What kind of version of its character a figure is, for a badge: "Series 1".."Series 3" for
// cores (from the "(Series N)" suffix, Series 1 otherwise), or "LightCore", "Giant", "Sidekick",
// "Swapper", "Variant", "Chase".
std::string VersionBadge(const SkylanderInfo& sky);

// English element name ("Magic"), empty for kNone.
std::string_view ElementName(Element element);

// Swap Force swappers are two figures: a bottom half (id 1000-1015) and a top half (2000-2015,
// bottom id + 1000), both with the same variant.
enum class SwapperHalf { kNone, kTop, kBottom };
SwapperHalf GetSwapperHalf(const SkylanderInfo& sky);

// The other half of a swapper (top for a bottom and the other way round), or nullptr.
const SkylanderInfo* SwapperPartner(const SkylanderInfo& sky);

// The swapper's name without " Top" / " Bottom" ("Freeze Blade"); the plain name otherwise.
std::string_view SwapperBaseName(const SkylanderInfo& sky);

// The name shown in menus: "_s" in catalog names stands for an apostrophe ("Dragon_s Peak"), and
// typographic apostrophes become plain ones.
std::string DisplayName(std::string_view catalog_name);

}  // namespace skylanders::portal
