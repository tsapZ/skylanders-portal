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

  // Every character entry of the first three games has an element.
  for (const SkylanderInfo& sky : AllSkylanders()) {
    bool early = sky.game == "Spyro's Adventure" || sky.game == "Giants" || sky.game == "Swap Force";
    bool character = sky.category != "Items" && sky.category != "Unreleased";
    if (early && character && FigureElement(sky) == Element::kNone) {
      std::fprintf(stderr, "no element: %s\n", std::string(sky.name).c_str());
      CHECK(!"every early character has an element");
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

  CHECK(DisplayName("Dragon_s Peak") == "Dragon's Peak");
  CHECK(DisplayName("Spyro") == "Spyro");
  CHECK(ElementName(Element::kTech) == "Tech");
  CHECK(ElementName(Element::kNone).empty());
  return Finish("figure_info");
}
