#include <cstdio>
#include <string>
#include <string_view>

#include "portal/figure_catalog.h"
#include "portal/figure_info.h"
#include "test_util.h"

using namespace skylanders::portal;

static const SkylanderInfo* ByName(std::string_view name) {
  for (const SkylanderInfo& sky : AllSkylanders()) {
    if (sky.name == name) {
      return &sky;
    }
  }
  return nullptr;
}

static Element ElementOf(std::string_view name) {
  const SkylanderInfo* sky = ByName(name);
  CHECK(sky != nullptr);
  return sky ? FigureElement(*sky) : Element::kNone;
}

int main() {
  // Cores and their variants share the element.
  CHECK(ElementOf("Chop Chop") == Element::kUndead);
  CHECK(ElementOf("Legendary Chop Chop") == Element::kUndead);
  CHECK(ElementOf("Twin Blade Chop Chop (Series 3)") == Element::kUndead);
  CHECK(ElementOf("Anchors Away Gill Grunt (Series 3)") == Element::kWater);
  CHECK(ElementOf("Mega Ram Dark Spyro") == Element::kMagic);
  CHECK(ElementOf("Metallic Purple Eye-Brawl") == Element::kUndead);
  CHECK(ElementOf("Legendary LightCore Chill") == Element::kWater);
  CHECK(ElementOf("Green Flame Frito-Lay Fire Bone Hot Dog") == Element::kFire);
  // Swappers, both halves.
  CHECK(ElementOf("Freeze Blade Top") == Element::kWater);
  CHECK(ElementOf("Nitro Freeze Blade Bottom") == Element::kWater);
  CHECK(ElementOf("Blast Zone Top") == Element::kFire);
  // Items have no element, even when their name contains a character's ("Scorpion" / "Scorp").
  CHECK(ElementOf("Scorpion Striker Catapult") == Element::kNone);
  CHECK(ElementOf("Sparx Dragonfly") == Element::kNone);
  // The later games, from their own data: Light and Dark, a Mini, a vehicle, a sensei.
  CHECK(ElementOf("Knight Light") == Element::kLight);
  CHECK(ElementOf("Blackout") == Element::kDark);
  CHECK(ElementOf("Drobit Mini") == Element::kTech);
  CHECK(ElementOf("Nitro Krypt King") == Element::kUndead);
  CHECK(ElementOf("Sea Shadow") == Element::kDark);
  CHECK(ElementOf("Starcast") == Element::kDark);
  // Crypt Crusher contains Crusher (Earth) but is Undead.
  CHECK(ElementOf("Crypt Crusher") == Element::kUndead);
  // Creation crystals by id: "Magic Bazooker" contains Zook (Life).
  CHECK(ElementOf("Magic Bazooker") == Element::kMagic);
  CHECK(ElementOf("Cy (@echoseeker715)") == Element::kMagic);
  // Traps have none, though "Fire Torch" contains Torch (Fire).
  CHECK(ElementOf("Fire Torch (Eternal Flame)") == Element::kNone);
  CHECK(ElementOf("Kaos") == Element::kNone);

  // Every character entry has an element, but Kaos (an element of his own), the Hand of Fate (a
  // magic item among Trap Team's variants) and a creation crystal with an id of no element.
  for (const SkylanderInfo& sky : AllSkylanders()) {
    bool character = sky.category != "Items" && sky.category != "Unreleased" && sky.category != "Traps";
    bool exception = sky.name == "Kaos" || sky.name == "Legendary Hand of Fate" ||
                     sky.name == "Earth Quickshot Bebop (@echoseeker715)";
    if (character && !exception && FigureElement(sky) == Element::kNone) {
      std::fprintf(stderr, "no element: %s\n", std::string(sky.name).c_str());
      CHECK(!"every character has an element");
    }
  }

  // Swapper halves and partners.
  const SkylanderInfo* top = ByName("Freeze Blade Top");
  const SkylanderInfo* bottom = ByName("Freeze Blade Bottom");
  CHECK(top && bottom);
  CHECK(GetSwapperHalf(*top) == SwapperHalf::kTop);
  CHECK(GetSwapperHalf(*bottom) == SwapperHalf::kBottom);
  CHECK(SwapperPartner(*top) == bottom);
  CHECK(SwapperPartner(*bottom) == top);
  CHECK(SwapperBaseName(*top) == "Freeze Blade");
  const SkylanderInfo* nitro_top = ByName("Nitro Freeze Blade Top");
  CHECK(nitro_top && SwapperPartner(*nitro_top) == ByName("Nitro Freeze Blade Bottom"));
  const SkylanderInfo* spyro = ByName("Spyro");
  CHECK(spyro && GetSwapperHalf(*spyro) == SwapperHalf::kNone && SwapperPartner(*spyro) == nullptr);
  CHECK(SwapperBaseName(*spyro) == "Spyro");

  // Characters and version badges.
  auto character = [](std::string_view name) {
    const SkylanderInfo* sky = ByName(name);
    return sky ? std::string(CharacterName(*sky)) : std::string("?");
  };
  auto badge = [](std::string_view name) {
    const SkylanderInfo* sky = ByName(name);
    return sky ? VersionBadge(*sky) : std::string("?");
  };
  CHECK(character("Legendary Chop Chop") == "Chop Chop");
  CHECK(character("Twin Blade Chop Chop (Series 3)") == "Chop Chop");
  CHECK(character("Metallic Blue Chop Chop") == "Chop Chop");
  CHECK(character("Nitro Freeze Blade Top") == "Freeze Blade");
  CHECK(character("Sparx Dragonfly").empty());
  CHECK(badge("Chop Chop") == "Series 1");
  CHECK(badge("Chop Chop (Series 2)") == "Series 2");
  CHECK(badge("Twin Blade Chop Chop (Series 3)") == "Series 3");
  CHECK(badge("Chill") == "Series 1");
  CHECK(badge("Chill LightCore") == "LightCore");
  CHECK(badge("Legendary Chop Chop") == "Variant");
  CHECK(badge("Metallic Blue Chop Chop") == "Chase");
  CHECK(badge("Tree Rex") == "Giant");
  CHECK(badge("Freeze Blade Top") == "Swapper");
  CHECK(badge("Wildfire") == "Trap Master");
  CHECK(badge("Barkley Mini") == "Mini");
  CHECK(badge("Soda Skimmer") == "Vehicle");
  CHECK(badge("Buckshot") == "Sensei");
  CHECK(badge("Hood Sickle") == "Villain Sensei");

  CHECK(DisplayName("Dragon_s Peak") == "Dragon's Peak");
  CHECK(DisplayName("Spyro") == "Spyro");
  CHECK(DisplayName("Eon\xE2\x80\x99s Elite Boomer") == "Eon's Elite Boomer");
  CHECK(ElementName(Element::kTech) == "Tech");
  CHECK(ElementName(Element::kNone).empty());
  return Finish("figure_info");
}
