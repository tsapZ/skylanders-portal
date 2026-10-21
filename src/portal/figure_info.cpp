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

// Every playable character of the games (cores, giants, swappers, sidekicks, trap masters, Minis,
// SuperChargers and their vehicles, senseis). Variants are matched by containing one of these
// names, so the list holds base names only.
//
// Trap Team's, SuperChargers' and Imaginators' characters were read from the games' files
// (2026-10-21). Trap Team: every character's scene (rom/character/NNN_<model>.bld, its level.bld)
// names its element ("Elemental_Fire", "Portal_EntryFX_Fire.lvl"); the Minis' scenes are numbered
// apart from their figure ids and were matched by name (505_Mini_Drobot is Drobit). SuperChargers
// and Imaginators: permanent.pak's ToyData/<Name>_ToyData.igz, whose toy object (CFullCharacterToyData,
// CVehicleToyData) has the toy id at +0x08 and the element at +0x0C: 1 Earth, 2 Water, 3 Air,
// 4 Fire, 5 Life, 6 Undead, 7 Magic, 8 Tech, 9 Light, 10 Dark, 11 Kaos. Checked against the
// earlier games' characters already here: all 196 that Trap Team's scenes have, and 118 of the 119
// in SuperChargers' toy data, agree; the other was Crypt Crusher, which matched Crusher (Earth)
// and is Undead in the game's data, so it is listed now. The Nintendo characters and their
// vehicles are not on the Xbox 360 discs; theirs are the figures' (Hammer Slam Bowser and the Clown
// Cruiser Fire, Turbo Charge Donkey Kong and the Barrel Blaster Life). Kaos (Imaginators) has an
// element of his own the menu doesn't list.
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
    // Trap Team trap masters
    {"Blastermind", Element::kMagic},     {"Bushwhack", Element::kLife},
    {"Enigma", Element::kMagic},          {"Gearshift", Element::kTech},
    {"Gusto", Element::kAir},             {"Head Rush", Element::kEarth},
    {"Jawbreaker", Element::kTech},       {"Ka-Boom", Element::kFire},
    {"Knight Light", Element::kLight},    {"Knight Mare", Element::kDark},
    {"Krypt King", Element::kUndead},     {"Lob-Star", Element::kWater},
    {"Short Cut", Element::kUndead},      {"Snap Shot", Element::kWater},
    {"Thunderbolt", Element::kAir},       {"Tuff Luck", Element::kLife},
    {"Wallop", Element::kEarth},          {"Wildfire", Element::kFire},
    // Trap Team cores
    {"Bat Spin", Element::kUndead},       {"Blackout", Element::kDark},
    {"Blades", Element::kAir},            {"Chopper", Element::kTech},
    {"Cobra Cadabra", Element::kMagic},   {"Deja Vu", Element::kMagic},
    {"Echo", Element::kWater},            {"Fist Bump", Element::kEarth},
    {"Fling Kong", Element::kAir},        {"Flip Wreck", Element::kWater},
    {"Food Fight", Element::kLife},       {"Funny Bone", Element::kUndead},
    {"High Five", Element::kLife},        {"Rocky Roll", Element::kEarth},
    {"Spotlight", Element::kLight},       {"Torch", Element::kFire},
    {"Trail Blazer", Element::kFire},     {"Tread Head", Element::kTech},
    // Trap Team Minis
    {"Bop", Element::kEarth},             {"Breeze", Element::kAir},
    {"Drobit", Element::kTech},           {"Hijinx", Element::kUndead},
    {"Pet Vac", Element::kAir},           {"Small Fry", Element::kFire},
    {"Spry", Element::kMagic},
    // SuperChargers
    {"Astroblast", Element::kLight},      {"Dive-Clops", Element::kWater},
    {"Fiesta", Element::kUndead},         {"Hammer Slam Bowser", Element::kFire},
    {"High Volt", Element::kTech},        {"Nightfall", Element::kDark},
    {"Smash Hit", Element::kEarth},       {"Spitfire", Element::kFire},
    {"Splat", Element::kMagic},           {"Stormblade", Element::kAir},
    {"Thrillipede", Element::kLife},      {"Turbo Charge Donkey Kong", Element::kLife},
    // SuperChargers vehicles
    {"Barrel Blaster", Element::kLife},   {"Burn-Cycle", Element::kFire},
    {"Buzz Wing", Element::kLife},        {"Clown Cruiser", Element::kFire},
    {"Crypt Crusher", Element::kUndead},  {"Dive Bomber", Element::kWater},
    {"Gold Rusher", Element::kTech},      {"Hot Streak", Element::kFire},
    {"Jet Stream", Element::kAir},        {"Reef Ripper", Element::kWater},
    {"Sea Shadow", Element::kDark},       {"Shark Tank", Element::kEarth},
    {"Shield Striker", Element::kTech},   {"Sky Slicer", Element::kAir},
    {"Soda Skimmer", Element::kMagic},    {"Splatter Splasher", Element::kMagic},
    {"Stealth Stinger", Element::kLife},  {"Sun Runner", Element::kLight},
    {"Thump Truck", Element::kEarth},     {"Tomb Buggy", Element::kUndead},
    // Imaginators senseis
    {"Air Strike", Element::kAir},        {"Ambush", Element::kLife},
    {"Aurora", Element::kLight},          {"Barbella", Element::kEarth},
    {"Boom Bloom", Element::kLife},       {"Buckshot", Element::kMagic},
    {"Chain Reaction", Element::kTech},   {"Chopscotch", Element::kUndead},
    {"Crash Bandicoot", Element::kLife},  {"Dr. Neo Cortex", Element::kTech},
    {"Ember", Element::kFire},            {"Flarewolf", Element::kFire},
    {"King Pen", Element::kWater},        {"Mysticat", Element::kMagic},
    {"Pit Boss", Element::kUndead},       {"Ro-Bow", Element::kTech},
    {"Starcast", Element::kDark},         {"Tidepool", Element::kWater},
    {"Tri-Tip", Element::kEarth},         {"Wild Storm", Element::kAir},
    // Imaginators villain senseis
    {"Bad Juju", Element::kAir},          {"Blaster-Tron", Element::kLight},
    {"Chompy Mage", Element::kLife},      {"Dr. Krankcase", Element::kTech},
    {"Golden Queen", Element::kEarth},    {"Grave Clobber", Element::kWater},
    {"Hood Sickle", Element::kDark},      {"Pain-Yatta", Element::kMagic},
    {"Tae Kwon Crow", Element::kFire},    {"Wolfgang", Element::kUndead},
};

// Imaginators' creation crystals by toy id: their names ("Cy", "Legendary Life Knight") don't
// always say the element. From the toy data (CYOS_Magic 680 ... CYOS_Light 689; CYOS_Air is not
// in it, 682 is the Air crystals' id in the catalog).
struct CrystalElement {
  uint16_t id;
  Element element;
};
constexpr CrystalElement kCrystals[] = {
    {680, Element::kMagic}, {681, Element::kWater}, {682, Element::kAir},  {683, Element::kUndead},
    {684, Element::kTech},  {685, Element::kFire},  {686, Element::kEarth}, {687, Element::kLife},
    {688, Element::kDark},  {689, Element::kLight},
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

// Traps are left out: their names say an element and a shape ("Fire Torch"), which would match
// characters ("Torch").
bool IsCharacterCategory(std::string_view category) {
  return category != "Items" && category != "Traps" && category != "Unreleased";
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
  if (sky.category == "Creation Crystals") {
    for (const CrystalElement& crystal : kCrystals) {
      if (crystal.id == sky.id) return crystal.element;
    }
    return Element::kNone;
  }
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
  // A few names use a typographic apostrophe (U+2019), which UI fonts often lack.
  for (size_t pos = out.find("\xE2\x80\x99"); pos != std::string::npos;
       pos = out.find("\xE2\x80\x99", pos + 1)) {
    out.replace(pos, 3, "'");
  }
  return out;
}

}  // namespace skylanders::portal
