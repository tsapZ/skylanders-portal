#include "portal/figure_info.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace skylanders::portal {
namespace {

struct CharacterElement {
  std::string_view name;
  Element element;
};

// Every playable character of Spyro's Adventure, Giants and Swap Force (cores, giants, swappers,
// sidekicks). Variants are matched by containing one of these names, so the list holds base
// names only.
constexpr CharacterElement kCharacters[] = {
    // Spyro's Adventure
    {"Spyro", Element::kMagic},           {"Voodood", Element::kMagic},
    {"Double Trouble", Element::kMagic},  {"Wrecking Ball", Element::kMagic},
    {"Bash", Element::kEarth},            {"Terrafin", Element::kEarth},
    {"Prism Break", Element::kEarth},     {"Dino-Rang", Element::kEarth},
    {"Gill Grunt", Element::kWater},      {"Slam Bam", Element::kWater},
    {"Zap", Element::kWater},             {"Wham-Shell", Element::kWater},
    {"Eruptor", Element::kFire},          {"Ignitor", Element::kFire},
    {"Flameslinger", Element::kFire},     {"Sunburn", Element::kFire},
    {"Trigger Happy", Element::kTech},    {"Drobot", Element::kTech},
    {"Boomer", Element::kTech},           {"Drill Sergeant", Element::kTech},
    {"Chop Chop", Element::kUndead},      {"Hex", Element::kUndead},
    {"Cynder", Element::kUndead},         {"Ghost Roaster", Element::kUndead},
    {"Stealth Elf", Element::kLife},      {"Stump Smash", Element::kLife},
    {"Camo", Element::kLife},             {"Zook", Element::kLife},
    {"Whirlwind", Element::kAir},         {"Sonic Boom", Element::kAir},
    {"Warnado", Element::kAir},           {"Lightning Rod", Element::kAir},
    {"Gill Runt", Element::kWater},       {"Terrabite", Element::kEarth},
    {"Trigger Snappy", Element::kTech},   {"Whisper Elf", Element::kLife},
    // Giants
    {"Bouncer", Element::kTech},          {"Crusher", Element::kEarth},
    {"Eye Brawl", Element::kUndead},      {"Hot Head", Element::kFire},
    {"Ninjini", Element::kMagic},         {"Swarm", Element::kAir},
    {"Thumpback", Element::kWater},       {"Tree Rex", Element::kLife},
    {"Chill", Element::kWater},           {"Flashwing", Element::kEarth},
    {"Fright Rider", Element::kUndead},   {"Hot Dog", Element::kFire},
    {"Jet-Vac", Element::kAir},           {"Pop Fizz", Element::kMagic},
    {"Shroomboom", Element::kLife},       {"Sprocket", Element::kTech},
    {"Barkley", Element::kLife},          {"Eye Small", Element::kUndead},
    {"Mini Jini", Element::kMagic},       {"Thumpling", Element::kWater},
    // Swap Force swappers
    {"Blast Zone", Element::kFire},       {"Boom Jet", Element::kAir},
    {"Doom Stone", Element::kEarth},      {"Fire Kraken", Element::kFire},
    {"Free Ranger", Element::kAir},       {"Freeze Blade", Element::kWater},
    {"Grilla Drilla", Element::kLife},    {"Hoot Loop", Element::kMagic},
    {"Magna Charge", Element::kTech},     {"Night Shift", Element::kUndead},
    {"Rattle Shake", Element::kUndead},   {"Rubble Rouser", Element::kEarth},
    {"Spy Rise", Element::kTech},         {"Stink Bomb", Element::kLife},
    {"Trap Shadow", Element::kMagic},     {"Wash Buckler", Element::kWater},
    // Swap Force cores
    {"Bumble Blast", Element::kLife},     {"Countdown", Element::kTech},
    {"Dune Bug", Element::kMagic},        {"Fryno", Element::kFire},
    {"Grim Creeper", Element::kUndead},   {"Pop Thorn", Element::kAir},
    {"Punk Shock", Element::kWater},      {"Rip Tide", Element::kWater},
    {"Roller Brawl", Element::kUndead},   {"Scorp", Element::kEarth},
    {"Scratch", Element::kAir},           {"Slobber Tooth", Element::kEarth},
    {"Smolderdash", Element::kFire},      {"Star Strike", Element::kMagic},
    {"Wind-Up", Element::kTech},          {"Zoo Lou", Element::kLife},
};

// Lower-case letters and digits only, so "Eye-Brawl" and "Eye Brawl" compare equal.
std::string Normalize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

bool IsCharacterCategory(std::string_view category) {
  return category != "Items" && category != "Unreleased";
}

// The character entry whose name the figure's name contains; the longest match wins, so a variant
// name that happens to contain a shorter name as well still maps to its own character.
const CharacterElement* FindCharacter(const SkylanderInfo& sky) {
  if (!IsCharacterCategory(sky.category)) {
    return nullptr;
  }
  const std::string name = Normalize(sky.name);
  size_t best_length = 0;
  const CharacterElement* best = nullptr;
  for (const CharacterElement& character : kCharacters) {
    const std::string base = Normalize(character.name);
    if (base.size() > best_length && name.find(base) != std::string::npos) {
      best_length = base.size();
      best = &character;
    }
  }
  return best;
}

}  // namespace

Element FigureElement(const SkylanderInfo& sky) {
  const CharacterElement* character = FindCharacter(sky);
  return character ? character->element : Element::kNone;
}

std::string_view CharacterName(const SkylanderInfo& sky) {
  const CharacterElement* character = FindCharacter(sky);
  return character ? character->name : std::string_view();
}

std::string VersionBadge(const SkylanderInfo& sky) {
  const size_t series = sky.name.find("(Series ");
  if (series != std::string_view::npos && series + 8 < sky.name.size()) {
    return "Series " + std::string(1, sky.name[series + 8]);
  }
  if (sky.category == "Cores") return "Series 1";
  if (sky.category == "LightCores") return "LightCore";
  if (sky.category == "Giants") return "Giant";
  if (sky.category == "Sidekicks") return "Sidekick";
  if (sky.category == "Swappers") return "Swapper";
  if (sky.category == "Chase Variants") return "Chase";
  if (sky.category == "Variants") return "Variant";
  return std::string(sky.category);
}



std::string_view ElementName(Element element) {
  switch (element) {
    case Element::kMagic:
      return "Magic";
    case Element::kEarth:
      return "Earth";
    case Element::kWater:
      return "Water";
    case Element::kFire:
      return "Fire";
    case Element::kTech:
      return "Tech";
    case Element::kUndead:
      return "Undead";
    case Element::kLife:
      return "Life";
    case Element::kAir:
      return "Air";
    case Element::kLight:
      return "Light";
    case Element::kDark:
      return "Dark";
    case Element::kNone:
      break;
  }
  return {};
}

SwapperHalf GetSwapperHalf(const SkylanderInfo& sky) {
  if (sky.id >= 2000 && sky.id < 2016) {
    return SwapperHalf::kTop;
  }
  if (sky.id >= 1000 && sky.id < 1016) {
    return SwapperHalf::kBottom;
  }
  return SwapperHalf::kNone;
}

const SkylanderInfo* SwapperPartner(const SkylanderInfo& sky) {
  switch (GetSwapperHalf(sky)) {
    case SwapperHalf::kTop:
      return FindSkylander(static_cast<uint16_t>(sky.id - 1000), sky.variant);
    case SwapperHalf::kBottom:
      return FindSkylander(static_cast<uint16_t>(sky.id + 1000), sky.variant);
    case SwapperHalf::kNone:
      break;
  }
  return nullptr;
}

std::string_view SwapperBaseName(const SkylanderInfo& sky) {
  std::string_view name = sky.name;
  for (std::string_view suffix : {std::string_view(" Top"), std::string_view(" Bottom")}) {
    if (GetSwapperHalf(sky) != SwapperHalf::kNone && name.size() > suffix.size() &&
        name.substr(name.size() - suffix.size()) == suffix) {
      return name.substr(0, name.size() - suffix.size());
    }
  }
  return name;
}

std::string DisplayName(std::string_view catalog_name) {
  std::string out(catalog_name);
  for (size_t pos = out.find("_s"); pos != std::string::npos; pos = out.find("_s", pos + 2)) {
    out.replace(pos, 2, "'s");
  }
  return out;
}

}  // namespace skylanders::portal
