#include "portal_rex/portal_menu.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <future>
#include <atomic>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <rex/cvar.h>
#include <rex/input/device.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/windowed_app_context.h>

#include "portal/data_folder.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"
#include "portal/figure_stats.h"
#include "portal/hybrid/real_portal_bridge.h"
#include "portal/software/software_portal.h"
#include "portal_rex/achievements_ui.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/portal_rex.h"
#include "portal_rex/png_file.h"
#include "portal_rex/settings_file.h"

REXCVAR_DEFINE_STRING(export_pictures, "", "Launcher",
                      "Write the figure, hat and achievement pictures to this folder and quit; for the "
                      "Core of Light launcher, right after a disc is set up");
REXCVAR_DEFINE_STRING(portal_menu_games, "", "Portal",
                      "Games whose Skylanders the portal menu offers, separated by ';' (empty: "
                      "all games)");
REXCVAR_DEFINE_BOOL(portal_menu_auto_open, true, "Portal",
                    "Open the portal menu by itself when the game asks a player for a Skylander");
REXCVAR_DEFINE_STRING(portal_menu_hidden_games, "", "Portal",
                      "Games of portal_menu_games the portal menu hides, separated by ';' (set "
                      "with Start in the menu)");
REXCVAR_DEFINE_BOOL(portal_menu_owned_only, false, "Portal",
                    "The portal menu lists only figures that have a figure file");
REXCVAR_DEFINE_BOOL(portal_menu_later_versions, false, "Portal",
                    "The portal menu also shows the tabs of later games that have figures of the "
                    "characters this game knows (those figures, Series 2 Spyro in Spyro's "
                    "Adventure, are among each character's versions anyway; the game plays them as "
                    "its own)");
REXCVAR_DEFINE_STRING(portal_menu_leave_button, "b", "Portal",
                      "The button of the game's portal screen that lets a second player leave "
                      "(\"b\" in Giants and Swap Force, \"rs\" in Spyro's Adventure); the menu's "
                      "\"Leave the game\" presses it");
REXCVAR_DEFINE_BOOL(portal_menu_nintendo, false, "Portal",
                    "List Bowser, Donkey Kong and their vehicles too (their toys work only on "
                    "Nintendo's consoles; the Xbox 360 discs don't have them)");
REXCVAR_DEFINE_STRING(portal_menu_hidden_categories, "", "Portal",
                      "Figure kinds the game can't read, left out of the portal menu, separated "
                      "by ';' (catalog categories, e.g. \"Sidekicks\" in Spyro's Adventure)");
REXCVAR_DEFINE_INT32(portal_menu_open_delay_ms, 2500, "Portal",
                     "After the open button lifts the figure, the menu waits for the game's own "
                     "portal screen to come up, at most this long")
    .range(0, 10000);

namespace skylanders {
namespace {

using namespace menu_draw;
using portal::Element;
using portal::SkylanderInfo;
using portal::SwapperHalf;
using rex::input::DeviceId;
using Clock = std::chrono::steady_clock;

// --- Text -------------------------------------------------------------------------------------
//
// The game's own words where it has them (#52), from its text files (loosefiles.pak
// data\systemdata_en.mua / _de.mua, permanent_xenon.pak strings\xenonplatformstrings_*.igx):
// "PORTAL OF POWER" / "MAGISCHES PORTAL", "Player %d" / "Spieler %d", "Magic Item(s)" /
// "Magische(s) Objekt(e)", "Sidekick" / "Mini-Skylander", "Upgrades" / "Verbesserungen", the
// element names (_elementNames: "Undead Skylanders" / "Gespenster-Skylander"). The game has no
// word for the menu's own ideas ("Recent", "Place").

enum class Text {
  kTitle,
  kPlayer,
  kRecent,
  kPlace,
  kBack,
  kElement,
  kGame,
  kDetails,
  kTop,
  kBottom,
  kNewFigure,
  kUsbActive,
  kNoPortal,
  kNothingRecent,
  kChooseVersion,
  kVersions,
  kNickname,
  kFile,
  kItems,
  kBuildSwapper,
  kHalf,
  kTaken,
  kOnPortal,
  kTakeOff,
  kCategory,
  kTakeOffItem,
  kOptions,
  kLeave,
  kLeaveInfo,
  kShowGames,
  kOwnedOnly,
  kOn,
  kOff,
  kToggle,
  kClose,
  kNothingOwned,
  kItemTakenOff,
  kXpToNext,
  kXpTop,
  kPlayTime,
  kHours,
  kMinutes,
  kHeroPoints,
  kUpgrades,
  kNoPath,
  kPath,
  kHat,
  kHatNumber,
  kLastPlayed,
  kPlayedOn,
  kBringAlong,
  kXp,
  kStartingAttacks,
  kBought,
  kPrice,
  kNoUpgrades,
  kTopLevelShort,
  kNoProgress,
  kSidekickInfo,
  kItemInfo,
  kItemDuration,
  kCheckTitleSkylanders,
  kCheckTitleItems,
  kCheckTitleMagicItems,
  kCheckTitleSidekicks,
  kCheckTitleOther,
  kCheckHint,
  kCheckSidekicks,
  kCheckItems,
  kCheckSkylanders,
  kCheckPlayerSkylander,
  kCheckStraySkylander,
  kCheckItem,
  kCheckSidekick,
  kCheckTooMany,
  kCheckOff,
  kCheckStays,
  kCheckContinue,
  kCheckToggle,
  kCheckRealPortal,
  kOnRealPortal,
  kRealSidekick,
  kRealItem,
  kAdventurePack,
  kAdventurePackInfo,
  kCheckTitleToys,
  kCheckToys,
  kChangeHalves,
  kVariant,
  kCount,
};

constexpr const char* kEnglish[] = {
    "PORTAL OF POWER",
    "Player %u",
    "Recent",
    "Place",
    "Back",
    "Element",
    "Game",
    "Details",
    "Top",
    "Bottom",
    "New figure, created when placed",
    "The real USB portal is active. Put your figures on it.",
    "No portal is active (portal_mode = none).",
    "Nothing placed yet. Choose a game with LT / RT.",
    "Which version?",
    "versions",
    "Nickname",
    "File",
    "Magic Items",
    "Build your Swapper",
    "Top / Bottom",
    "Taken \xC2\xB7 P%u",
    "On the portal",
    "Take off",
    "Category",
    "Take item off",
    "Options",
    "Leave the game",
    "Player %u takes their Skylander off and leaves the game",
    "Show games",
    "Only my figures",
    "On",
    "Off",
    "Toggle",
    "Close",
    "No figures of your own here. Start: options",
    "Magic item taken off the portal",
    "Level %u \xC2\xB7 %u XP \xC2\xB7 %u more to level %u",
    "Level %u \xC2\xB7 %u XP \xC2\xB7 top level",
    "Play time",
    "%u h %02u min",
    "%u min",
    "Hero points",
    "Upgrades",
    "no path chosen yet",
    "path %d",
    "Hat",
    "hat no. %u",
    "Last played",
    "on",
    "Bring along",
    "XP",
    "Starting attacks",
    "Bought",
    "Price: %u gold",
    "No upgrades bought yet",
    "MAX",
    "No progress saved yet: the game writes it onto the figure once it has played with it.",
    "A sidekick goes on the portal next to the Skylanders and tags along. The game takes one "
    "at a time: a new one replaces the one there. It earns no experience and saves nothing on "
    "the figure.",
    "A magic item takes effect as soon as it goes on the portal.",
    "Lasts %d seconds",
    "Too many Skylanders on the portal",
    "Too many magic items or sidekicks on the portal",
    "Too many magic items on the portal",
    "Too many sidekicks on the portal",
    "The game has a problem with the figures on the portal",
    "Choose what to take off, then Continue. This is on the portal:",
    "%u sidekicks: the game takes only one.",
    "%u magic items: the game takes only one.",
    "%u Skylanders: one per player.",
    "Player %u's Skylander",
    "Skylander without a player",
    "Magic item",
    "Sidekick",
    "Too many",
    "Off",
    "Stays",
    "Continue",
    "Off / stays",
    "on the real portal",
    "This figure is on the real portal.",
    "A sidekick is on the real portal: lift it off there first.",
    "A magic item is on the real portal: lift it off there first.",
    "Adventure pack",
    "Opens a level of its own when it comes onto the portal.",
    "Too many toys on the portal",
    "%u toys: the game takes only one.",
    "Change top / bottom",
    "Version",
};
constexpr const char* kGerman[] = {
    "MAGISCHES PORTAL",
    "Spieler %u",
    "Zuletzt",
    "Auflegen",
    "Zur\xC3\xBC"
    "ck",
    "Element",
    "Spiel",
    "Details",
    "Oben",
    "Unten",
    "Neue Figur, wird beim Auflegen angelegt",
    "Das echte USB-Portal ist aktiv. Stell deine Figuren direkt aufs Portal.",
    "Kein Portal aktiv (portal_mode = none).",
    "Noch nichts aufgelegt. W\xC3\xA4hle mit LT / RT ein Spiel.",
    "Welche Variante?",
    "Varianten",
    "Spitzname",
    "Datei",
    "Magische Objekte",
    "Swapper zusammenbauen",
    "Oben / Unten",
    "Belegt \xC2\xB7 SP%u",
    "Auf dem Portal",
    "Runternehmen",
    "Kategorie",
    "Objekt runternehmen",
    "Optionen",
    "Spiel verlassen",
    "Spieler %u nimmt seinen Skylander runter und verl\xC3\xA4sst das Spiel",
    "Spiele anzeigen",
    "Nur eigene Figuren",
    "An",
    "Aus",
    "Umschalten",
    "Schlie\xC3\x9F"
    "en",
    "Hier gibt es keine eigenen Figuren. Start: Optionen",
    "Magisches Objekt vom Portal genommen",
    "Level %u \xC2\xB7 %u EP \xC2\xB7 noch %u bis Level %u",
    "Level %u \xC2\xB7 %u EP \xC2\xB7 h\xC3\xB6" "chstes Level",
    "Spielzeit",
    "%u Std. %02u Min.",
    "%u Min.",
    "Heldenpunkte",
    "Verbesserungen",
    "noch kein Pfad",
    "Pfad %d",
    "Hut",
    "Hut Nr. %u",
    "Zuletzt gespielt",
    "auf",
    "Mitnehmen",
    "EP",
    "Grundangriffe",
    "Gekauft",
    "Preis: %u Gold",
    "Noch keine Verbesserungen gekauft",
    "MAX",
    "Noch kein Spielstand: Das Spiel speichert ihn auf der Figur, sobald es mit ihr gespielt hat.",
    "Ein Mini-Skylander kommt neben die Skylander aufs Portal und l\xC3\xA4uft mit. Das Spiel nimmt "
    "immer nur einen: ein neuer ersetzt den, der schon dort ist. Er sammelt keine Erfahrung "
    "und speichert nichts auf der Figur.",
    "Ein magisches Objekt wirkt, sobald es aufs Portal kommt.",
    "Wirkt %d Sekunden",
    "Zu viele Skylander auf dem Portal",
    "Zu viele magische Objekte oder Mini-Skylander auf dem Portal",
    "Zu viele magische Objekte auf dem Portal",
    "Zu viele Mini-Skylander auf dem Portal",
    "Das Spiel hat ein Problem mit den Figuren auf dem Portal",
    "W\xC3\xA4hle aus, was runter soll, dann Fortfahren. Das ist auf dem Portal:",
    "%u Mini-Skylander: Das Spiel nimmt nur einen.",
    "%u magische Objekte: Das Spiel nimmt nur eins.",
    "%u Skylander: einer pro Spieler.",
    "Skylander von Spieler %u",
    "Skylander ohne Spieler",
    "Magisches Objekt",
    "Mini-Skylander",
    "Zu viele",
    "Runter",
    "Bleibt",
    "Fortfahren",
    "Runter / bleibt",
    "auf dem echten Portal",
    "Diese Figur steht auf dem echten Portal.",
    "Ein Mini-Skylander steht auf dem echten Portal: Nimm ihn dort zuerst runter.",
    "Ein magisches Objekt steht auf dem echten Portal: Nimm es dort zuerst runter.",
    "Abenteuer-Paket",
    "\xC3\x96"
    "ffnet ein eigenes Level, wenn es aufs Portal kommt.",
    "Zu viele Spielzeuge auf dem Portal",
    "%u Spielzeuge: Das Spiel nimmt nur eins.",
    "Oben / Unten tauschen",
    "Variante",
};
static_assert(std::size(kEnglish) == size_t(Text::kCount));
static_assert(std::size(kGerman) == size_t(Text::kCount));

bool g_german = false;

const char* T(Text text) { return (g_german ? kGerman : kEnglish)[size_t(text)]; }

// Element names as the German game shows them (systemdata_de.mua _elementNames: "Technologie-",
// "Gespenster-Skylander"). Light and Dark are not in Swap Force.
std::string_view ElementLabel(Element element) {
  if (g_german) {
    switch (element) {
      case Element::kMagic:
        return "Magie";
      case Element::kEarth:
        return "Erde";
      case Element::kWater:
        return "Wasser";
      case Element::kFire:
        return "Feuer";
      case Element::kTech:
        return "Technologie";
      case Element::kUndead:
        return "Gespenster";
      case Element::kLife:
        return "Leben";
      case Element::kAir:
        return "Luft";
      case Element::kLight:
        return "Licht";
      case Element::kDark:
        return "Dunkelheit";
      case Element::kNone:
        return "-";
    }
  }
  std::string_view name = portal::ElementName(element);
  return name.empty() ? "-" : name;
}

// The German words are the games' own (Imaginators' German text: "Fahrzeug", "Kreationskristall",
// "Falle", "Mini-Skylander", read 2026-10-21); names the German games keep in English (Eon's
// Elite, LightCore, Trap Master, SuperChargers) stay as they are.
std::string BadgeLabel(const std::string& badge) {
  if (g_german) {
    if (badge.rfind("Series ", 0) == 0) return "Serie " + badge.substr(7);
    if (badge == "Variant") return "Variante";
    if (badge == "Chase") return "Chase-Variante";
    if (badge == "Giant") return "Gigant";
    if (badge == "Sidekick" || badge == "Mini") return "Mini-Skylander";
    if (badge == "Trap") return "Falle";
    if (badge == "Vehicle") return "Fahrzeug";
    if (badge == "Vehicle Variant") return "Fahrzeug-Variante";
    if (badge == "Sensei Variant") return "Sensei-Variante";
    if (badge == "Villain Sensei") return "Schurken-Sensei";
    if (badge == "Villain Variant") return "Schurken-Variante";
    if (badge == "Creation Crystal") return "Kreationskristall";
    if (badge == "Unreleased") return "Unver\xC3\xB6" "ffentlicht";
  }
  return badge;
}

// The version badge as a card shows it under the name: "S2" for Series 2, else the badge.
// Swappers have none (their card says SWAP), and neither have items.
std::string ShortBadge(const SkylanderInfo& sky) {
  if (sky.category == "Items") return {};
  const std::string badge = portal::VersionBadge(sky);
  if (badge.rfind("Series ", 0) == 0) return "S" + badge.substr(7);
  if (badge == "Swapper") return {};
  return BadgeLabel(badge);
}

// The order elements are listed in, as on the game's own element screens.
constexpr Element kElementOrder[] = {Element::kMagic,  Element::kEarth, Element::kWater,
                                     Element::kFire,   Element::kTech,  Element::kUndead,
                                     Element::kLife,   Element::kAir,   Element::kLight,
                                     Element::kDark,   Element::kNone};

int ElementRank(Element element) {
  for (int i = 0; i < int(std::size(kElementOrder)); ++i) {
    if (kElementOrder[i] == element) return i;
  }
  return int(std::size(kElementOrder));
}

// --- Colours ----------------------------------------------------------------------------------

ImVec4 ElementRgb(Element element) {
  switch (element) {
    case Element::kMagic:
      return {0.62f, 0.32f, 0.86f, 1};
    case Element::kEarth:
      return {0.72f, 0.50f, 0.25f, 1};
    case Element::kWater:
      return {0.18f, 0.55f, 0.92f, 1};
    case Element::kFire:
      return {0.95f, 0.33f, 0.16f, 1};
    case Element::kTech:
      return {0.98f, 0.66f, 0.12f, 1};
    case Element::kUndead:
      return {0.55f, 0.52f, 0.70f, 1};
    case Element::kLife:
      return {0.32f, 0.74f, 0.30f, 1};
    case Element::kAir:
      return {0.45f, 0.82f, 0.98f, 1};
    case Element::kLight:
      return {0.98f, 0.90f, 0.50f, 1};
    case Element::kDark:
      return {0.33f, 0.25f, 0.52f, 1};
    case Element::kNone:
      break;
  }
  // Items (and anything without an element): gold.
  return {0.80f, 0.60f, 0.24f, 1};
}

ImU32 ElementColor(Element element, float brightness = 1.0f, int alpha = 255) {
  ImVec4 c = ElementRgb(element);
  return IM_COL32(int(std::min(1.0f, c.x * brightness) * 255),
                  int(std::min(1.0f, c.y * brightness) * 255),
                  int(std::min(1.0f, c.z * brightness) * 255), alpha);
}

std::string_view GameShortName(std::string_view game) {
  if (game == "Spyro's Adventure") return "SSA";
  if (game == "Swap Force") return "SWAP Force";
  return game;
}

// --- Drawing helpers ----------------------------------------------------------------------------

// Self-drawn element symbols (the official ones can't be used): star with sparkles, mountains,
// drop with a wave, flame, gear, skull, leaf, tornado, sun, crescent. `fill` is the symbol,
// `detail` the lines and holes cut into it, `glow` the lighter inner parts.
void DrawElementShape(ImDrawList* dl, ImVec2 c, float r, Element element, ImU32 fill,
                      ImU32 detail, ImU32 glow) {
  constexpr float kPi = 3.14159265f;
  auto P = [&](float x, float y) { return ImVec2{c.x + x * r, c.y + y * r}; };
  auto sparkle = [&](float x, float y, float s, ImU32 col) {
    const ImVec2 pts[] = {P(x, y - s),          P(x + s * 0.24f, y - s * 0.24f), P(x + s, y),
                          P(x + s * 0.24f, y + s * 0.24f), P(x, y + s),
                          P(x - s * 0.24f, y + s * 0.24f), P(x - s, y),
                          P(x - s * 0.24f, y - s * 0.24f)};
    dl->AddConcavePolyFilled(pts, 8, col);
  };
  std::vector<ImVec2> pts;
  switch (element) {
    case Element::kMagic: {
      for (int i = 0; i < 10; ++i) {
        const float a = -kPi / 2 + i * kPi / 5.0f;
        const float rr = (i % 2) ? 0.40f : 0.86f;
        pts.push_back(P(-0.08f + std::cos(a) * rr, 0.08f + std::sin(a) * rr));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), fill);
      sparkle(0.66f, -0.66f, 0.30f, fill);
      sparkle(-0.74f, 0.70f, 0.20f, fill);
      dl->AddCircleFilled(P(-0.08f, 0.10f), r * 0.17f, glow);
      break;
    }
    case Element::kEarth: {
      pts = {P(-1.0f, 0.78f),  P(-0.42f, -0.42f), P(-0.16f, 0.02f),
             P(0.28f, -0.88f), P(1.0f, 0.78f)};
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), fill);
      // Snow caps and a ridge line.
      const ImVec2 cap[] = {P(0.28f, -0.88f), P(0.47f, -0.50f), P(0.32f, -0.58f),
                            P(0.20f, -0.46f), P(0.10f, -0.52f)};
      dl->AddConcavePolyFilled(cap, 5, glow);
      const ImVec2 cap2[] = {P(-0.42f, -0.42f), P(-0.28f, -0.16f), P(-0.40f, -0.22f),
                             P(-0.54f, -0.18f)};
      dl->AddConcavePolyFilled(cap2, 4, glow);
      const ImVec2 ridge[] = {P(0.28f, -0.60f), P(0.10f, 0.05f), P(0.30f, 0.55f)};
      dl->AddPolyline(ridge, 3, detail, 0, r * 0.08f);
      break;
    }
    case Element::kWater: {
      pts.push_back(P(0.0f, -1.0f));
      for (int i = 0; i <= 20; ++i) {
        const float a = -0.52f + i * (kPi + 1.04f) / 20.0f;
        pts.push_back(P(std::cos(a) * 0.68f, 0.28f + std::sin(a) * 0.68f));
      }
      dl->AddConvexPolyFilled(pts.data(), int(pts.size()), fill);
      std::vector<ImVec2> wave;
      for (int i = 0; i <= 12; ++i) {
        const float t = float(i) / 12.0f;
        wave.push_back(P(-0.52f + 1.04f * t, 0.42f + std::sin(t * 2.0f * kPi) * 0.10f));
      }
      dl->AddPolyline(wave.data(), int(wave.size()), detail, 0, r * 0.09f);
      dl->AddCircleFilled(P(-0.24f, 0.02f), r * 0.13f, glow);
      break;
    }
    case Element::kFire: {
      pts = {P(0.05f, -1.0f),  P(0.30f, -0.52f), P(0.58f, -0.74f), P(0.56f, -0.24f),
             P(0.80f, 0.12f),  P(0.70f, 0.62f),  P(0.34f, 0.94f),  P(-0.10f, 0.98f),
             P(-0.56f, 0.82f), P(-0.80f, 0.40f), P(-0.72f, -0.10f), P(-0.50f, -0.40f),
             P(-0.42f, -0.04f), P(-0.22f, -0.48f)};
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), fill);
      const ImVec2 inner[] = {P(0.02f, -0.16f), P(0.22f, 0.20f), P(0.38f, 0.06f),
                              P(0.40f, 0.48f),  P(0.12f, 0.80f), P(-0.22f, 0.76f),
                              P(-0.40f, 0.46f), P(-0.20f, 0.12f)};
      dl->AddConcavePolyFilled(inner, 8, glow);
      break;
    }
    case Element::kTech: {
      const int teeth = 10;
      const float step = 2.0f * kPi / teeth;
      for (int i = 0; i < teeth; ++i) {
        const float a = i * step;
        pts.push_back(P(std::cos(a - step * 0.20f), std::sin(a - step * 0.20f)));
        pts.push_back(P(std::cos(a + step * 0.20f), std::sin(a + step * 0.20f)));
        pts.push_back(P(std::cos(a + step * 0.30f) * 0.76f, std::sin(a + step * 0.30f) * 0.76f));
        pts.push_back(P(std::cos(a + step * 0.70f) * 0.76f, std::sin(a + step * 0.70f) * 0.76f));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), fill);
      dl->AddCircle(c, r * 0.52f, glow, 0, r * 0.08f);
      dl->AddCircleFilled(c, r * 0.28f, detail);
      break;
    }
    case Element::kUndead: {
      dl->AddCircleFilled(P(0.0f, -0.16f), r * 0.78f, fill);
      dl->AddRectFilled(P(-0.42f, 0.30f), P(0.42f, 0.90f), fill, r * 0.16f);
      // Eye sockets, nose, teeth.
      const ImVec2 eye_l[] = {P(-0.52f, -0.24f), P(-0.10f, -0.14f), P(-0.20f, 0.18f),
                              P(-0.46f, 0.12f)};
      const ImVec2 eye_r[] = {P(0.52f, -0.24f), P(0.46f, 0.12f), P(0.20f, 0.18f),
                              P(0.10f, -0.14f)};
      dl->AddConvexPolyFilled(eye_l, 4, detail);
      dl->AddConvexPolyFilled(eye_r, 4, detail);
      const ImVec2 nose[] = {P(0.0f, 0.22f), P(0.10f, 0.42f), P(-0.10f, 0.42f)};
      dl->AddConvexPolyFilled(nose, 3, detail);
      for (float x : {-0.21f, 0.0f, 0.21f}) {
        dl->AddLine(P(x, 0.62f), P(x, 0.90f), detail, r * 0.07f);
      }
      dl->AddCircleFilled(P(-0.34f, -0.56f), r * 0.10f, glow);
      break;
    }
    case Element::kLife: {
      // A leaf tipped at the top right, with a stem and veins.
      for (int i = 0; i <= 16; ++i) {
        const float t = float(i) / 16.0f;
        pts.push_back(P(-0.70f + 1.62f * t - 0.50f * std::sin(t * kPi),
                        0.70f - 1.62f * t - 0.50f * std::sin(t * kPi)));
      }
      for (int i = 15; i >= 1; --i) {
        const float t = float(i) / 16.0f;
        pts.push_back(P(-0.70f + 1.62f * t + 0.50f * std::sin(t * kPi),
                        0.70f - 1.62f * t + 0.50f * std::sin(t * kPi)));
      }
      dl->AddConvexPolyFilled(pts.data(), int(pts.size()), fill);
      dl->AddLine(P(-0.98f, 0.98f), P(-0.62f, 0.62f), fill, r * 0.14f);
      dl->AddLine(P(-0.62f, 0.62f), P(0.62f, -0.62f), detail, r * 0.09f);
      for (float t : {-0.30f, 0.05f, 0.40f}) {
        dl->AddLine(P(t, -t), P(t - 0.30f, -t - 0.26f), detail, r * 0.06f);
        dl->AddLine(P(t, -t), P(t + 0.26f, -t + 0.30f), detail, r * 0.06f);
      }
      break;
    }
    case Element::kAir: {
      // A tornado: swirls getting narrower towards the ground.
      const float rows[][3] = {{-0.72f, 0.95f, 0.00f}, {-0.36f, 0.74f, 0.08f},
                               {0.00f, 0.52f, 0.14f},  {0.34f, 0.32f, 0.08f},
                               {0.64f, 0.16f, -0.04f}};
      for (const auto& row : rows) {
        const float y = row[0], half = row[1], x = row[2];
        dl->AddLine(P(x - half, y), P(x + half, y), fill, r * 0.22f);
        dl->AddCircleFilled(P(x - half, y), r * 0.11f, fill);
        dl->AddCircleFilled(P(x + half, y), r * 0.11f, fill);
      }
      dl->AddLine(P(-0.50f, -0.72f), P(0.30f, -0.72f), glow, r * 0.07f);
      dl->AddCircleFilled(P(0.02f, 0.88f), r * 0.10f, fill);
      break;
    }
    case Element::kLight: {
      for (int i = 0; i < 12; ++i) {
        const float a = i * kPi / 6.0f;
        const float len = (i % 2) ? 0.78f : 1.0f;
        const ImVec2 ray[] = {P(std::cos(a) * len, std::sin(a) * len),
                              P(std::cos(a + 0.20f) * 0.56f, std::sin(a + 0.20f) * 0.56f),
                              P(std::cos(a - 0.20f) * 0.56f, std::sin(a - 0.20f) * 0.56f)};
        dl->AddConvexPolyFilled(ray, 3, fill);
      }
      dl->AddCircleFilled(c, r * 0.50f, fill);
      dl->AddCircleFilled(c, r * 0.30f, glow);
      break;
    }
    case Element::kDark: {
      for (int i = 0; i <= 24; ++i) {
        const float a = 0.55f + i * (2.0f * kPi - 1.1f) / 24.0f;
        pts.push_back(P(std::cos(a) * 0.95f, std::sin(a) * 0.95f));
      }
      for (int i = 24; i >= 0; --i) {
        const float a = 1.05f + i * (2.0f * kPi - 2.1f) / 24.0f;
        pts.push_back(P(0.40f + std::cos(a) * 0.68f, -0.20f + std::sin(a) * 0.68f));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), fill);
      sparkle(0.62f, 0.42f, 0.22f, fill);
      break;
    }
    case Element::kNone: {
      // A magic item: a cut gem.
      const ImVec2 gem[] = {P(-0.62f, -0.50f), P(0.62f, -0.50f), P(0.95f, -0.12f),
                            P(0.0f, 0.92f),    P(-0.95f, -0.12f)};
      dl->AddConvexPolyFilled(gem, 5, fill);
      dl->AddLine(P(-0.95f, -0.12f), P(0.95f, -0.12f), detail, r * 0.07f);
      dl->AddLine(P(-0.30f, -0.50f), P(-0.40f, -0.12f), detail, r * 0.06f);
      dl->AddLine(P(0.30f, -0.50f), P(0.40f, -0.12f), detail, r * 0.06f);
      dl->AddLine(P(-0.40f, -0.12f), P(0.0f, 0.92f), detail, r * 0.06f);
      dl->AddLine(P(0.40f, -0.12f), P(0.0f, 0.92f), detail, r * 0.06f);
      const ImVec2 shine[] = {P(-0.55f, -0.42f), P(-0.30f, -0.42f), P(-0.36f, -0.20f),
                              P(-0.70f, -0.20f)};
      dl->AddConvexPolyFilled(shine, 4, glow);
      break;
    }
  }
}

// An element symbol in one colour. `outlined` adds a dark rim and drop shadow, for symbols drawn
// over busy or similarly coloured backgrounds.
void DrawElementIcon(ImDrawList* dl, ImVec2 c, float r, Element element, ImU32 col,
                     bool outlined = false) {
  const int alpha = int((col >> IM_COL32_A_SHIFT) & 0xFF);
  const ImU32 detail = IM_COL32(10, 8, 20, alpha * 2 / 3);
  const ImU32 glow = IM_COL32(255, 255, 255, alpha * 2 / 5);
  if (outlined) {
    const ImU32 rim = IM_COL32(12, 10, 26, alpha * 4 / 5);
    DrawElementShape(dl, {c.x + r * 0.06f, c.y + r * 0.10f}, r, element, rim, rim, rim);
    const float o = std::max(1.0f, r * 0.07f);
    for (const ImVec2 d : {ImVec2{-o, 0}, ImVec2{o, 0}, ImVec2{0, -o}, ImVec2{0, o}}) {
      DrawElementShape(dl, {c.x + d.x, c.y + d.y}, r, element, rim, rim, rim);
    }
  }
  DrawElementShape(dl, c, r, element, col, detail, glow);
}

// The game's own picture for a key (SetArtImageProvider), or a null id while there is none.
ImTextureID ArtTexture(const std::string& key);

// "element:magic": the key of the game's symbol for an element; empty for none.
std::string ElementKey(Element element) {
  switch (element) {
    case Element::kMagic: return "element:magic";
    case Element::kEarth: return "element:earth";
    case Element::kWater: return "element:water";
    case Element::kFire: return "element:fire";
    case Element::kTech: return "element:tech";
    case Element::kUndead: return "element:undead";
    case Element::kLife: return "element:life";
    case Element::kAir: return "element:air";
    case Element::kLight: return "element:light";
    case Element::kDark: return "element:dark";
    default: return {};
  }
}

// The element as a medallion: the game's own element symbol when there is one, else a gold
// ring, a disc shaded in the element colour and a drawn symbol in white on top.
void DrawElementBadge(ImDrawList* dl, ImVec2 c, float r, Element element, int alpha = 255) {
  if (ImTextureID art = ArtTexture(ElementKey(element))) {
    dl->AddCircleFilled({c.x, c.y + r * 0.08f}, r * 1.02f, IM_COL32(0, 0, 0, alpha / 3));
    dl->AddImage(art, {c.x - r * 1.08f, c.y - r * 1.08f}, {c.x + r * 1.08f, c.y + r * 1.08f},
                 {0, 0}, {1, 1}, IM_COL32(255, 255, 255, alpha));
    return;
  }
  dl->AddCircleFilled({c.x, c.y + r * 0.08f}, r * 1.04f, IM_COL32(0, 0, 0, alpha / 3));
  for (int i = 0; i < 8; ++i) {
    const float k = float(i) / 7.0f;
    dl->AddCircleFilled({c.x - r * 0.10f * k, c.y - r * 0.12f * k}, r * (1.0f - 0.55f * k),
                        ElementColor(element, 0.42f + 0.75f * k, alpha));
  }
  dl->AddCircle(c, r * 0.97f, IM_COL32(244, 196, 84, alpha), 0, r * 0.12f);
  dl->AddCircle(c, r * 0.86f, IM_COL32(90, 56, 12, alpha * 3 / 4), 0, r * 0.04f);
  dl->PathArcTo(c, r * 0.97f, 3.6f, 4.6f, 12);
  dl->PathStroke(IM_COL32(255, 244, 205, alpha * 3 / 4), 0, r * 0.05f);
  DrawElementIcon(dl, c, r * 0.56f, element, IM_COL32(255, 255, 255, alpha), true);
}

// A Portal of Power for the preview figure to stand on: a dark round base, a silver rim and a
// glowing top in `glow` (the figure's element colour), as a real portal lights up for the figure on
// it. Animated like the games' portal: the light above pulses, a swirl turns on the top and sparks
// rise from it. `c` is the middle of the top, `w` the width. Returns the lowest point drawn.
float DrawPortalOfPower(ImDrawList* dl, ImVec2 c, float w, ImU32 glow) {
  const float rx = w * 0.5f, ry = w * 0.15f, h = w * 0.12f;
  const float t = float(ImGui::GetTime());
  const int gr = int(glow & 0xFF), gg = int((glow >> 8) & 0xFF), gb = int((glow >> 16) & 0xFF);
  // The glow colour, lightened towards white by `k` (0 the colour, 1 white).
  auto tint = [&](float k, int alpha) {
    return IM_COL32(int(gr + (255 - gr) * k), int(gg + (255 - gg) * k), int(gb + (255 - gb) * k),
                    std::clamp(alpha, 0, 255));
  };
  const float pulse = 0.8f + 0.2f * std::sin(t * 2.4f);
  // Light rising from the top, behind the figure.
  const ImVec2 beam0{c.x - rx * 0.62f, c.y - w * 0.95f}, beam1{c.x + rx * 0.62f, c.y};
  dl->AddRectFilledMultiColor(beam0, beam1, tint(0.45f, 0), tint(0.45f, 0), tint(0.45f, int(80 * pulse)),
                              tint(0.45f, int(80 * pulse)));
  // A soft glow on the "floor".
  for (int i = 6; i >= 1; --i) {
    const float k = float(i) / 6.0f;
    dl->AddEllipseFilled({c.x, c.y + h}, {rx * (1.0f + 0.35f * k), ry * (1.0f + 0.35f * k)},
                         tint(0.3f, int((14 * (1.0f - k) + 4) * pulse)));
  }
  // Rings of light running out over the "floor", one every 2.2 s, as the portal sends out its
  // power (the owner's wish for a portal more like the game's, SwapForceRecomp #57).
  for (int k = 0; k < 2; ++k) {
    float phase = t / 2.2f + float(k) * 0.5f;
    phase -= std::floor(phase);
    const float grow = 1.0f + 0.55f * phase;
    dl->AddEllipse({c.x, c.y + h}, {rx * grow, ry * grow}, tint(0.5f, int(110 * (1.0f - phase) * pulse)), 0, 48,
                   std::max(1.0f, w * 0.01f * (1.0f - 0.5f * phase)));
  }
  // The base: its side, then the top.
  const ImU32 side = IM_COL32(22, 24, 40, 255);
  dl->AddEllipseFilled({c.x, c.y + h}, {rx, ry}, side);
  dl->AddRectFilled({c.x - rx, c.y}, {c.x + rx, c.y + h}, side);
  // The band of light round its side under the rim, as the real portal glows all round: the side's
  // front half, a wide soft stroke and a thin bright one.
  dl->PathEllipticalArcTo({c.x, c.y + h * 0.45f}, {rx * 0.995f, ry}, 0, 0.0f, 3.14159265f, 40);
  dl->PathStroke(tint(0.25f, int(70 * pulse)), 0, h * 0.5f);
  dl->PathEllipticalArcTo({c.x, c.y + h * 0.45f}, {rx * 0.995f, ry}, 0, 0.0f, 3.14159265f, 40);
  dl->PathStroke(tint(0.6f, int(170 * pulse)), 0, std::max(1.0f, h * 0.12f));
  dl->PathEllipticalArcTo({c.x, c.y + h}, {rx, ry}, 0, 0, 3.14159265f, 32);
  dl->PathStroke(IM_COL32(90, 100, 140, 220), 0, std::max(1.0f, w * 0.008f));
  dl->AddEllipseFilled(c, {rx, ry}, IM_COL32(52, 58, 86, 255));
  // The rim's ridges.
  for (int i = 0; i < 32; ++i) {
    const float a = float(i) / 32.0f * 6.2831853f;
    const float ca = std::cos(a), sa = std::sin(a);
    dl->AddLine({c.x + ca * rx * 0.9f, c.y + sa * ry * 0.9f}, {c.x + ca * rx * 0.985f, c.y + sa * ry * 0.985f},
                IM_COL32(120, 132, 168, 200), std::max(1.0f, w * 0.006f));
  }
  dl->AddEllipse(c, {rx, ry}, IM_COL32(190, 200, 225, 255), 0, 48, std::max(1.5f, w * 0.014f));
  dl->AddEllipse(c, {rx * 0.88f, ry * 0.88f}, IM_COL32(96, 106, 140, 255), 0, 48,
                 std::max(1.0f, w * 0.008f));
  // The glowing middle, from the colour at the edge to white in the centre.
  for (int i = 0; i < 8; ++i) {
    const float k = float(i) / 7.0f;
    dl->AddEllipseFilled(c, {rx * (0.80f - 0.55f * k), ry * (0.80f - 0.55f * k)},
                         tint(0.15f + 0.85f * k, int((150 + 90 * k) * (0.9f + 0.1f * pulse))));
  }
  // A swirl turning on the top: two bright arcs opposite each other.
  for (int k = 0; k < 2; ++k) {
    const float a0 = t * 1.7f + float(k) * 3.14159265f;
    dl->PathEllipticalArcTo(c, {rx * 0.6f, ry * 0.6f}, 0, a0, a0 + 1.3f, 16);
    dl->PathStroke(tint(0.8f, 150), 0, std::max(1.0f, w * 0.012f));
  }
  // Sparks rising from the top and fading out, each on its own path and time.
  constexpr int kSparks = 14;
  for (int i = 0; i < kSparks; ++i) {
    float phase = t * 0.42f + float(i) * 0.618034f;
    phase -= std::floor(phase);
    const float angle = float(i) * 2.399963f;  // spread around the top
    // Around the rim, so they show beside the figure's picture.
    const float x = c.x + std::cos(angle) * rx * 0.88f * (1.0f - 0.3f * phase) +
                    std::sin(t * 1.3f + float(i)) * rx * 0.05f;
    const float y = c.y + std::sin(angle) * ry * 0.5f - phase * w * 0.85f;
    const float size = std::max(1.0f, w * 0.014f * (1.0f - 0.5f * phase));
    const int alpha = int(220 * (1.0f - phase) * std::min(1.0f, phase * 6.0f));
    dl->AddCircleFilled({x, y}, size * 2.2f, tint(0.4f, alpha / 5));
    dl->AddCircleFilled({x, y}, size, tint(0.85f, alpha));
  }
  return c.y + h + ry;
}
// --- Figure library ---------------------------------------------------------------------------

uint32_t Key(uint16_t id, uint16_t variant) { return (uint32_t(id) << 16) | variant; }
uint32_t Key(const SkylanderInfo& sky) { return Key(sky.id, sky.variant); }

std::filesystem::path Utf8Path(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string PathUtf8(const std::filesystem::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

std::filesystem::path FiguresDir() { return Utf8Path(REXCVAR_GET(portal_figures_dir)); }

struct OwnedFigure {
  std::filesystem::path path;
  const SkylanderInfo* sky = nullptr;
  std::optional<portal::FigureStats> stats;
  std::filesystem::file_time_type mtime{};
};

// One version of a character: a figure, or a swapper's top half with its own bottom.
struct Version {
  const SkylanderInfo* sky = nullptr;
  const SkylanderInfo* bottom = nullptr;
};

// A character with all its versions, or a magic item / adventure pack (`item`, one version).
struct Character {
  std::string name;
  Element element = Element::kNone;
  std::string debut_game;
  bool swapper = false;
  bool item = false;
  bool sidekick = false;  // a companion (IsCompanion): goes on next to the player's Skylander
  std::vector<Version> versions;  // catalog (release) order

  bool HasVersionFrom(std::string_view game) const {
    for (const Version& v : versions) {
      if (v.sky->game == game) return true;
    }
    return false;
  }
};

// A recently placed combination of figure files (two for a swapper).
struct RecentEntry {
  std::vector<std::filesystem::path> paths;
  std::vector<const SkylanderInfo*> figures;
};

// A game tab has one row per element; the items tab one row per game.
struct Tab {
  std::string title;
  std::string game;  // game tabs: every character with a version from this game
  bool recent = false;
  bool items = false;
  std::vector<Element> elements;         // game tabs: the element of each row
  std::vector<std::string> row_titles;   // items tab: the game of each row
  std::vector<std::vector<size_t>> rows;  // character indices per row
};

std::vector<std::string> AllowedGames() {
  std::vector<std::string> games;
  std::stringstream list(REXCVAR_GET(portal_menu_games));
  std::string game;
  while (std::getline(list, game, ';')) {
    if (!game.empty()) games.push_back(game);
  }
  if (games.empty()) {
    for (const SkylanderInfo& sky : portal::AllSkylanders()) {
      if (std::find(games.begin(), games.end(), sky.game) == games.end()) {
        games.emplace_back(sky.game);
      }
    }
  }
  return games;
}

bool Contains(const std::vector<std::string>& list, std::string_view value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

std::vector<std::string> SplitList(const std::string& value) {
  std::vector<std::string> items;
  std::stringstream list(value);
  std::string item;
  while (std::getline(list, item, ';')) {
    if (!item.empty()) items.push_back(item);
  }
  return items;
}

// The games of AllowedGames() the menu shows: without portal_menu_hidden_games, and all of them if
// that would leave none.
std::vector<std::string> ShownGames(const std::vector<std::string>& games) {
  const std::vector<std::string> hidden = SplitList(REXCVAR_GET(portal_menu_hidden_games));
  std::vector<std::string> shown;
  for (const std::string& game : games) {
    if (!Contains(hidden, game)) shown.push_back(game);
  }
  return shown.empty() ? games : shown;
}

// What goes on the portal next to a player's Skylander instead of being one, by the game the menu
// runs in (its newest game in portal_menu_games): up to Swap Force the sidekicks, which only tag
// along; in Trap Team the traps (one trap slot, as on the Traptanium portal); from SuperChargers on
// the vehicles (each driver's own). From Trap Team on the old sidekicks are Minis the game plays,
// so they go on as a player's Skylander (the colleague's reports, TrapTeamRecomp #11,
// SuperChargersRecomp #6, Core of Light #32: a trap or a vehicle lifted the player's Skylander).
enum class CompanionKind { kSidekick, kTrap, kVehicle };
CompanionKind GameCompanionKind() {
  const std::vector<std::string> games = SplitList(REXCVAR_GET(portal_menu_games));
  if (Contains(games, "SuperChargers") || Contains(games, "Imaginators")) return CompanionKind::kVehicle;
  if (Contains(games, "Trap Team")) return CompanionKind::kTrap;
  return CompanionKind::kSidekick;
}

bool IsCompanion(const SkylanderInfo* sky) {
  if (!sky) return false;
  switch (GameCompanionKind()) {
    case CompanionKind::kSidekick: return sky->category == "Sidekicks";
    case CompanionKind::kTrap: return sky->category == "Traps";
    case CompanionKind::kVehicle: return sky->category == "Vehicles" || sky->category == "Vehicle Variants";
  }
  return false;
}

// The Nintendo characters and their vehicles: their toys work only on Nintendo's consoles, the
// Xbox 360 discs don't have them (the game only says the Skylander isn't supported; figure_info.cpp,
// ImaginatorsRecomp #7). Hidden unless portal_menu_nintendo.
bool IsNintendoOnly(const SkylanderInfo& sky) {
  return sky.id == 3423 || sky.id == 3424 || sky.id == 3233 || sky.id == 3240;
}

// The game's button for "leave the game" on its portal screen (portal_menu_leave_button).
uint16_t LeaveButton() {
  const uint16_t button = menu_input::ButtonByName(REXCVAR_GET(portal_menu_leave_button));
  return button ? button : uint16_t(rex::input::X_INPUT_GAMEPAD_B);
}

// A figure kind the game can't read (portal_menu_hidden_categories).
bool IsHiddenCategory(std::string_view category) {
  return Contains(SplitList(REXCVAR_GET(portal_menu_hidden_categories)), category);
}

bool IsPlayableCategory(std::string_view category) {
  return category != "Items" && category != "Unreleased";
}

// Where the menu saves its options (portal_menu_hidden_games, portal_menu_owned_only); empty: not
// saved. UI thread only.
std::filesystem::path g_config_path;

void SetOption(std::string_view name, const std::string& value) {
  StoreSetting(g_config_path, name, value);
}

// Each player's recent list, kept across sessions, one line per placement with the figure files
// relative to portal_figures_dir ("Swap Force/Blast Zone Top.dump|Swap Force/Fire Kraken
// Bottom.dump" for a swapper). A file appears in one entry only: placing a half in a new
// combination removes its old combination, as with a real figure.
std::filesystem::path RecentFile(uint32_t user) {
  return FiguresDir() / ("portal_menu_recent_p" + std::to_string(user + 1) + ".txt");
}

// The Recent tab keeps this many entries per player, newest first.
constexpr size_t kMaxRecent = 12;

std::vector<std::vector<std::filesystem::path>> LoadRecentPaths(uint32_t user) {
  std::vector<std::vector<std::filesystem::path>> recent;
  std::ifstream in(RecentFile(user));
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::filesystem::path> paths;
    std::stringstream parts(line);
    std::string part;
    while (std::getline(parts, part, '|')) {
      if (!part.empty()) paths.push_back(FiguresDir() / Utf8Path(part));
    }
    if (!paths.empty()) recent.push_back(std::move(paths));
  }
  if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);  // older files kept 16
  return recent;
}

void AddRecent(uint32_t user, const std::vector<std::filesystem::path>& paths) {
  auto recent = LoadRecentPaths(user);
  auto shares_file = [&](const std::vector<std::filesystem::path>& entry) {
    for (const auto& p : entry) {
      if (std::find(paths.begin(), paths.end(), p) != paths.end()) return true;
    }
    return false;
  };
  recent.erase(std::remove_if(recent.begin(), recent.end(), shares_file), recent.end());
  recent.insert(recent.begin(), paths);
  if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
  std::ofstream out(RecentFile(user), std::ios::trunc);
  std::error_code ec;
  for (const auto& entry : recent) {
    for (size_t i = 0; i < entry.size(); ++i) {
      std::filesystem::path rel = std::filesystem::relative(entry[i], FiguresDir(), ec);
      out << (i ? "|" : "") << PathUtf8(ec ? entry[i] : rel);
    }
    out << "\n";
  }
}

class Library {
 public:
  void Build(uint32_t user) {
    owned_.clear();
    by_path_.clear();
    characters_.clear();
    tabs_.clear();
    recent_.clear();
    ScanOwned();
    const std::vector<std::string> games = AllowedGames();
    const std::vector<std::string> shown = ShownGames(games);
    const bool owned_only = REXCVAR_GET(portal_menu_owned_only);
    // A version is listed unless its game is hidden or, with "only my figures", neither of its
    // figures has a file.
    // Imaginators' toys only with a file: they carry 32 bytes in sector 0 (blocks 2 and 4) that the
    // game checks, and a blank figure, or a real dump with those two blocks zeroed, is "damaged"
    // (portal error reason 0; Air Strike, a Water crystal and a Bronze Mystery Chest, 2026-10-23).
    // The earlier games' figures have zeros there. The menu can't make such a toy, so only the
    // files there are of them (read from the real portal, docs/hybrid-portal.md) are offered.
    auto listed = [&](const SkylanderInfo* sky, const SkylanderInfo* bottom) {
      if (Contains(games, sky->game) && !Contains(shown, sky->game)) return false;
      if (sky->game == "Imaginators" && !Owned(sky)) return false;
      return !owned_only || Owned(sky) || (bottom && Owned(bottom));
    };

    // Characters with all their versions (catalog order is release order). Figures from later
    // games count too when they carry the id of a character this game knows: the game goes by
    // the id, so "Eon's Elite Chop Chop" (id 30) plays as Chop Chop in Swap Force, while a
    // SuperChargers driver (own id) or a character new in a later game is unknown to it. They are
    // always among a character's versions (Y: Spyro in Spyro's Adventure with Series 2 and 3,
    // LightCore, Eon's Elite; the owner's wish, 2026-10-24), while the tabs stay the game's own
    // unless portal_menu_later_versions adds the later games' (the menu looks like the game's own
    // by default, 2026-10-21).
    const bool later_versions = REXCVAR_GET(portal_menu_later_versions);
    std::unordered_map<std::string, size_t> index;
    std::unordered_map<std::string, std::string> debut;  // character -> first game, unfiltered
    known_ids_.clear();
    std::vector<size_t> items;
    for (const SkylanderInfo& sky : portal::AllSkylanders()) {
      const bool game_allowed = std::find(games.begin(), games.end(), sky.game) != games.end();
      if (sky.category == "Items" && game_allowed) {
        if (!listed(&sky, nullptr)) continue;
        Character item;
        item.name = portal::DisplayName(sky.name);
        item.debut_game = sky.game;
        item.item = true;
        item.versions.push_back({&sky, nullptr});
        items.push_back(characters_.size());
        characters_.push_back(std::move(item));
        continue;
      }
      if (IsNintendoOnly(sky) && !REXCVAR_GET(portal_menu_nintendo)) continue;
      // Kinds the game can't read (sidekicks in Spyro's Adventure) aren't listed, and their ids
      // stay unknown, so a later game's figure with such an id isn't either.
      if (!IsPlayableCategory(sky.category) || IsHiddenCategory(sky.category)) {
        continue;
      }
      if (portal::GetSwapperHalf(sky) == SwapperHalf::kBottom) {
        // Bottoms belong to their swapper's character; only listed through it.
        if (game_allowed) {
          std::string name(portal::CharacterName(sky));
          if (name.empty()) name = portal::DisplayName(portal::SwapperBaseName(sky));
          known_ids_.emplace(sky.id, name);
        }
        continue;
      }
      std::string name;
      if (game_allowed) {
        name = portal::CharacterName(sky);
        if (name.empty()) name = portal::DisplayName(portal::SwapperBaseName(sky));
        known_ids_.emplace(sky.id, name);
      } else if (auto known = known_ids_.find(sky.id); known != known_ids_.end()) {
        name = known->second;  // a later game's version of a character this game knows
      } else {
        continue;
      }
      debut.emplace(name, sky.game);
      Version version{&sky, nullptr};
      if (portal::GetSwapperHalf(sky) == SwapperHalf::kTop) {
        version.bottom = portal::SwapperPartner(sky);
      }
      if (!listed(version.sky, version.bottom)) continue;
      auto [it, inserted] = index.emplace(name, characters_.size());
      if (inserted) {
        Character character;
        character.name = name;
        character.element = portal::FigureElement(sky);
        character.debut_game = debut[name];
        character.swapper = portal::GetSwapperHalf(sky) == SwapperHalf::kTop;
        character.sidekick = IsCompanion(&sky);
        characters_.push_back(std::move(character));
      }
      characters_[it->second].versions.push_back(version);
    }

    // Recent first, then one tab per game with every character that has a version from that game
    // (Drobot is in Spyro's Adventure with Series 1 and in Giants with Series 2 and LightCore),
    // grouped by element.
    Tab recent;
    recent.title = T(Text::kRecent);
    recent.recent = true;
    tabs_.push_back(recent);
    for (const auto& paths : LoadRecentPaths(user)) {
      RecentEntry entry;
      for (const auto& path : paths) {
        auto it = by_path_.find(PathUtf8(path));
        if (it == by_path_.end() || !it->second->sky) continue;
        entry.paths.push_back(path);
        entry.figures.push_back(it->second->sky);
      }
      if (!entry.figures.empty() && known_ids_.contains(entry.figures[0]->id)) {
        recent_.push_back(std::move(entry));
      }
    }
    // The game's own tabs, then (with later_versions) the later games that have a version of one
    // of its characters, in release order.
    std::vector<std::string> tab_games = shown;
    if (later_versions) {
      for (const SkylanderInfo& sky : portal::AllSkylanders()) {
        if (Contains(games, sky.game) || Contains(tab_games, sky.game)) continue;
        for (const Character& character : characters_) {
          if (!character.item && character.HasVersionFrom(sky.game)) {
            tab_games.emplace_back(sky.game);
            break;
          }
        }
      }
    }
    for (const std::string& game : tab_games) {
      Tab tab;
      tab.title = game;
      for (size_t i = 0; i < characters_.size(); ++i) {
        if (characters_[i].item || !characters_[i].HasVersionFrom(game)) continue;
        auto e = std::find(tab.elements.begin(), tab.elements.end(), characters_[i].element);
        if (e == tab.elements.end()) {
          tab.elements.push_back(characters_[i].element);
          tab.rows.emplace_back();
          e = tab.elements.end() - 1;
        }
        tab.rows[size_t(e - tab.elements.begin())].push_back(i);
      }
      // Elements in the game's order; within one, characters with an owned version from this
      // game first, then the ones new in this game, then the returning ones.
      std::vector<size_t> order(tab.elements.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = i;
      std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return ElementRank(tab.elements[a]) < ElementRank(tab.elements[b]);
      });
      Tab sorted;
      sorted.title = tab.title;
      sorted.game = game;
      for (size_t i : order) {
        sorted.elements.push_back(tab.elements[i]);
        std::vector<size_t> chars = tab.rows[i];
        auto rank = [&](size_t c) {
          const Character& character = characters_[c];
          return (BestOwned(character, game) ? 0 : 2) + (character.debut_game == game ? 0 : 1);
        };
        std::stable_sort(chars.begin(), chars.end(),
                         [&](size_t a, size_t b) { return rank(a) < rank(b); });
        sorted.rows.push_back(std::move(chars));
      }
      if (!sorted.elements.empty()) tabs_.push_back(std::move(sorted));
    }

    // Magic items and adventure packs last, one row per game.
    Tab item_tab;
    item_tab.title = T(Text::kItems);
    item_tab.items = true;
    for (const std::string& game : shown) {
      std::vector<size_t> row;
      for (size_t i : items) {
        if (characters_[i].debut_game == game) row.push_back(i);
      }
      if (row.empty()) continue;
      item_tab.row_titles.emplace_back(GameShortName(game));
      item_tab.rows.push_back(std::move(row));
    }
    if (!item_tab.rows.empty()) tabs_.push_back(std::move(item_tab));
  }

  // The character a figure plays as in this game ("Chop Chop" for Eon's Elite Chop Chop, the
  // swapper's name for either half); empty for items and figures the game doesn't know.
  std::string CharacterOf(const SkylanderInfo& sky) const {
    auto it = known_ids_.find(sky.id);
    return it == known_ids_.end() ? std::string() : it->second;
  }

  const std::vector<Tab>& tabs() const { return tabs_; }
  const std::vector<Character>& characters() const { return characters_; }

  // The index of the character (or item) a figure is a version of.
  std::optional<size_t> CharacterIndex(const SkylanderInfo* sky) const {
    if (!sky) return std::nullopt;
    for (size_t i = 0; i < characters_.size(); ++i) {
      for (const Version& v : characters_[i].versions) {
        if (v.sky == sky || v.bottom == sky) return i;
      }
    }
    return std::nullopt;
  }
  const std::vector<RecentEntry>& recent() const { return recent_; }

  const OwnedFigure* Owned(const SkylanderInfo* sky) const {
    if (!sky) return nullptr;
    auto it = owned_.find(Key(*sky));
    return it == owned_.end() ? nullptr : &it->second;
  }

  const OwnedFigure* OwnedAt(const std::filesystem::path& path) const {
    auto it = by_path_.find(PathUtf8(path));
    return it == by_path_.end() ? nullptr : it->second;
  }

  // The version played last, if any version (from `game`, if given) is owned.
  const OwnedFigure* BestOwned(const Character& character, std::string_view game = {}) const {
    const OwnedFigure* best = nullptr;
    for (const Version& v : character.versions) {
      if (!game.empty() && v.sky->game != game) continue;
      for (const SkylanderInfo* sky : {v.sky, v.bottom}) {
        const OwnedFigure* owned = Owned(sky);
        if (owned && (!best || owned->mtime > best->mtime)) best = owned;
      }
    }
    return best;
  }

  // Finds the tab, element and position of the character a figure belongs to, in the tab of the
  // figure's own game if it is listed there.
  bool Locate(const SkylanderInfo* sky, size_t& tab, size_t& element, size_t& position) const {
    std::vector<size_t> order;
    for (size_t t = 0; t < tabs_.size(); ++t) {
      if (tabs_[t].game == sky->game) order.insert(order.begin(), t);
      else order.push_back(t);
    }
    for (size_t t : order) {
      for (size_t e = 0; e < tabs_[t].rows.size(); ++e) {
        const auto& chars = tabs_[t].rows[e];
        for (size_t p = 0; p < chars.size(); ++p) {
          for (const Version& v : characters_[chars[p]].versions) {
            if (v.sky == sky || v.bottom == sky) {
              tab = t;
              element = e;
              position = p;
              return true;
            }
          }
        }
      }
    }
    return false;
  }

 private:
  void ScanOwned() {
    std::error_code ec;
    std::vector<OwnedFigure> all;
    for (const portal::FigureCatalogEntry& entry : portal::ScanFigureCatalog(FiguresDir())) {
      std::optional<portal::FigureData> data = portal::LoadFigureFile(entry.path);
      if (!data) continue;
      OwnedFigure figure;
      figure.path = entry.path;
      figure.sky = portal::FindSkylander(portal::ReadFigureId(*data), portal::ReadFigureVariant(*data));
      figure.stats = portal::ParseFigureStats(*data);
      figure.mtime = std::filesystem::last_write_time(entry.path, ec);
      all.push_back(std::move(figure));
    }
    storage_ = std::move(all);
    for (const OwnedFigure& figure : storage_) {
      by_path_[PathUtf8(figure.path)] = &figure;
      if (!figure.sky) continue;
      auto it = owned_.find(Key(*figure.sky));
      // With several files for one figure, the one played last stands for it.
      if (it == owned_.end() || figure.mtime > it->second.mtime) owned_[Key(*figure.sky)] = figure;
    }
  }

  std::vector<OwnedFigure> storage_;
  std::unordered_map<uint32_t, OwnedFigure> owned_;
  std::unordered_map<std::string, const OwnedFigure*> by_path_;
  std::vector<Character> characters_;
  std::vector<Tab> tabs_;
  std::vector<RecentEntry> recent_;
  std::unordered_map<uint16_t, std::string> known_ids_;  // figure id -> character name
};

// --- Figure pictures and names ----------------------------------------------------------------

std::mutex g_provider_mutex;
FigureImageProvider g_image_provider;
FigureNameProvider g_name_provider;
ArtImageProvider g_art_provider;
FigureUpgradesProvider g_upgrades_provider;
HatInfoProvider g_hat_info_provider;
ItemDurationProvider g_item_duration_provider;
std::string g_pack_name;  // SetAdventurePackName; empty: the menu's own
FigureTraitProvider g_trait_provider;
FigureStatsTraitProvider g_stats_trait_provider;
rex::ui::ImmediateDrawer* g_immediate_drawer = nullptr;
// Uploaded pictures by figure. UI thread only.
std::unordered_map<uint32_t, std::unique_ptr<rex::ui::ImmediateTexture>> g_textures;
// Uploaded art by key. UI thread only.
std::unordered_map<std::string, std::unique_ptr<rex::ui::ImmediateTexture>> g_art_textures;

// The figure's picture as a texture, or null while there is none (yet).
ImTextureID FigureTexture(const SkylanderInfo* sky) {
  if (!sky || !g_immediate_drawer) return ImTextureID{};
  const uint32_t key = Key(*sky);
  if (auto it = g_textures.find(key); it != g_textures.end()) {
    return reinterpret_cast<ImTextureID>(it->second.get());
  }
  FigureImageProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_image_provider;
  }
  std::shared_ptr<const FigureImage> image = provider ? provider(sky->id, sky->variant) : nullptr;
  // None of the game's own: another game's, if one on this PC has left it.
  if (!image) image = OtherGamePicture(sky->id, sky->variant);
  if (!image || image->width <= 0 || image->height <= 0 ||
      image->rgba.size() < size_t(image->width) * image->height * 4) {
    return ImTextureID{};
  }
  auto texture = g_immediate_drawer->CreateTexture(uint32_t(image->width),
                                                   uint32_t(image->height),
                                                   rex::ui::ImmediateTextureFilter::kLinear, false,
                                                   image->rgba.data());
  if (!texture) return ImTextureID{};
  ImTextureID id = reinterpret_cast<ImTextureID>(texture.get());
  g_textures.emplace(key, std::move(texture));
  return id;
}

// The name the game shows for the figure in its language, else the catalog's.
std::string FigureName(const SkylanderInfo& sky) {
  FigureNameProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_name_provider;
  }
  if (provider) {
    if (auto name = provider(sky.id, sky.variant)) return *name;
  }
  return portal::DisplayName(sky.name);
}

ImTextureID ArtTexture(const std::string& key) {
  if (key.empty() || !g_immediate_drawer) return ImTextureID{};
  if (auto it = g_art_textures.find(key); it != g_art_textures.end()) {
    return reinterpret_cast<ImTextureID>(it->second.get());
  }
  ArtImageProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_art_provider;
  }
  std::shared_ptr<const FigureImage> image = provider ? provider(key) : nullptr;
  if (!image || image->width <= 0 || image->height <= 0 ||
      image->rgba.size() < size_t(image->width) * image->height * 4) {
    return ImTextureID{};
  }
  auto texture = g_immediate_drawer->CreateTexture(uint32_t(image->width),
                                                   uint32_t(image->height),
                                                   rex::ui::ImmediateTextureFilter::kLinear, false,
                                                   image->rgba.data());
  if (!texture) return ImTextureID{};
  ImTextureID id = reinterpret_cast<ImTextureID>(texture.get());
  g_art_textures.emplace(key, std::move(texture));
  return id;
}

std::shared_ptr<const HatInfo> HatInfoFor(uint16_t hat) {
  HatInfoProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_hat_info_provider;
  }
  return provider ? provider(hat) : nullptr;
}

std::optional<int> ItemDurationFor(const SkylanderInfo& sky) {
  ItemDurationProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_item_duration_provider;
  }
  return provider ? provider(sky.id) : std::nullopt;
}

std::shared_ptr<const FigureUpgrades> UpgradesFor(const SkylanderInfo& sky) {
  FigureUpgradesProvider provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_upgrades_provider;
  }
  return provider ? provider(sky.id, sky.variant) : nullptr;
}

// The hat's name in the game's language, else its number.
std::string HatName(uint16_t hat) {
  if (auto info = HatInfoFor(hat); info && !info->name.empty()) return info->name;
  char text[64];
  std::snprintf(text, sizeof(text), T(Text::kHatNumber), unsigned(hat));
  return text;
}

// Whether a figure has bought an upgrade. Both paths share their bits, which count for the chosen
// path; the other one's are its alternate bits (the game's eTAFB_AlternateUpgradePath, from 11).
bool UpgradeOwned(const portal::FigureStats& s, const FigureUpgradePath& path, int path_index,
                  const FigureUpgrade& u) {
  if (u.bit < 0) return true;
  if (path.kind != FigureUpgradePath::Kind::kPath) return (s.upgrades >> u.bit) & 1;
  const int chosen = portal::UpgradePath(s);
  if (chosen == 0) return false;
  const int bit = chosen == path_index + 1 ? u.bit : u.alternate_bit;
  return bit >= 0 && ((s.upgrades >> bit) & 1);
}

// The details view's facts below the level bar: play time, hero points, when and where last
// played, the nickname.
std::vector<std::string> StatsLines(const portal::FigureStats& s) {
  const std::string dot = " \xC2\xB7 ";
  std::vector<std::string> lines;
  char text[256];
  char time[64];
  const unsigned minutes = s.play_seconds / 60;
  if (minutes >= 60) {
    std::snprintf(time, sizeof(time), T(Text::kHours), minutes / 60, minutes % 60);
  } else {
    std::snprintf(time, sizeof(time), T(Text::kMinutes), minutes);
  }
  std::string line = std::string(T(Text::kPlayTime)) + " " + time;
  if (s.hero_points) line += dot + T(Text::kHeroPoints) + " " + std::to_string(s.hero_points);
  lines.push_back(line);
  if (s.last_played) {
    const portal::FigureDate& d = *s.last_played;
    if (g_german) {
      std::snprintf(text, sizeof(text), "%02u.%02u.%04u", unsigned(d.day), unsigned(d.month),
                    unsigned(d.year));
    } else {
      std::snprintf(text, sizeof(text), "%04u-%02u-%02u", unsigned(d.year), unsigned(d.month),
                    unsigned(d.day));
    }
    line = std::string(T(Text::kLastPlayed)) + ": " + text;
    std::string platforms;
    static const char* const kPlatforms[] = {"Wii", "Xbox 360", "PS3"};
    for (int i = 0; i < 3; ++i) {
      if (s.platforms & (1 << i)) platforms += (platforms.empty() ? "" : ", ") + std::string(kPlatforms[i]);
    }
    if (!platforms.empty()) line += " " + std::string(T(Text::kPlayedOn)) + " " + platforms;
    lines.push_back(line);
  }
  if (!s.nickname.empty()) lines.push_back(std::string(T(Text::kNickname)) + ": " + s.nickname);
  return lines;
}

// The gold symbol: the game's own ("ui:gold" from the art provider, e.g. its HUD coin), or a
// drawn coin while there is none.
void DrawGoldCoin(ImDrawList* dl, ImVec2 c, float r) {
  if (ImTextureID t = ArtTexture("ui:gold")) {
    const float h = r * 1.2f;  // the game's pictures leave a little room around the coin
    dl->AddImage(t, {c.x - h, c.y - h}, {c.x + h, c.y + h});
    return;
  }
  dl->AddCircleFilled(c, r, Rgba(250, 205, 70));
  dl->AddCircle(c, r * 0.72f, Rgba(196, 140, 30), 0, std::max(1.0f, r * 0.18f));
  dl->AddCircle(c, r, Rgba(130, 84, 10), 0, std::max(1.0f, r * 0.14f));
}

// "+2 Armor", "+0.5 ..." for a hat stat.
std::string StatText(const HatStat& stat) {
  char text[160];
  if (stat.value == float(int(stat.value))) {
    std::snprintf(text, sizeof(text), "%+d %s", int(stat.value), stat.name.c_str());
  } else {
    std::snprintf(text, sizeof(text), "%+.1f %s", stat.value, stat.name.c_str());
  }
  return text;
}

// The picture in a rounded square, or the element symbol when there is no picture.
void DrawFigurePicture(ImDrawList* dl, ImVec2 a, ImVec2 b, const SkylanderInfo* sky,
                       float rounding) {
  if (ImTextureID texture = FigureTexture(sky)) {
    dl->AddImageRounded(texture, a, b, {0, 0}, {1, 1}, IM_COL32_WHITE, rounding);
    return;
  }
  const Element element = sky ? portal::FigureElement(*sky) : Element::kNone;
  dl->AddRectFilled(a, b, ElementColor(element, 0.38f, 255), rounding);
  DrawElementIcon(dl, {(a.x + b.x) / 2, (a.y + b.y) / 2}, (b.x - a.x) * 0.32f, element,
                  ElementColor(element, 1.2f, 150));
}

// --- Players on the emulated portal -----------------------------------------------------------

// Which portal slots hold each player's figure (two for a swapper). The game itself only sees
// slots and assigns players by the order figures arrive; when one player's figure is lifted and
// another placed, that player gets it. UI thread only.
std::array<std::vector<int>, rex::input::kMaxGuestUsers> g_player_slots;
// The slot of the magic item / adventure pack put on through the menu, or -1. There is one, shared
// by all players: any player can replace it or take it off, and it stays on the portal when a
// player's figure is lifted.
int g_item_slot = -1;
// The slot of the sidekick put on through the menu, or -1, and the player who brought it. In
// Giants and Swap Force a sidekick only tags along behind a Skylander (it stores no data and isn't
// played), so like an item it goes on the portal next to the Skylanders and stays there when one
// is lifted. The game takes one at a time: with a second one it shows "too many Mini Skylanders on
// the portal" until one goes off (two players, one sidekick each, 2026-10-19). So there is one,
// shared like the item: any player can bring another (it replaces the first) or take it off.
int g_sidekick_slot = -1;
uint32_t g_sidekick_user = 0;
// A trap goes in that shared slot too (one at a time, a new one in place of the old one), a vehicle
// in its driver's own slot (SuperChargers: each player drives one).
std::array<int, rex::input::kMaxGuestUsers> g_vehicle_slots = [] {
  std::array<int, rex::input::kMaxGuestUsers> slots;
  slots.fill(-1);
  return slots;
}();

int& CompanionSlot(uint32_t user) {
  return GameCompanionKind() == CompanionKind::kVehicle ? g_vehicle_slots[user % g_vehicle_slots.size()]
                                                       : g_sidekick_slot;
}

bool IsItemSlot(int slot) { return slot >= 0 && slot == g_item_slot; }

bool IsSidekickSlot(int slot) {
  return slot >= 0 && (slot == g_sidekick_slot ||
                       std::find(g_vehicle_slots.begin(), g_vehicle_slots.end(), slot) != g_vehicle_slots.end());
}

// A slot that belongs to no player's Skylander: the item or a sidekick.
bool IsExtraSlot(int slot) { return IsItemSlot(slot) || IsSidekickSlot(slot); }

bool OwnedByOther(int slot, uint32_t user) {
  for (uint32_t u = 0; u < g_player_slots.size(); ++u) {
    if (u != user && std::find(g_player_slots[u].begin(), g_player_slots[u].end(), slot) !=
                         g_player_slots[u].end()) {
      return true;
    }
  }
  return false;
}

const SkylanderInfo* FigureIn(int slot) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return nullptr;
  std::optional<portal::FigureData> data = software->Figure(slot);
  if (!data) return nullptr;
  return portal::FindSkylander(portal::ReadFigureId(*data), portal::ReadFigureVariant(*data));
}

bool IsItem(const SkylanderInfo* sky) { return sky && sky->category == "Items"; }
// Adventure packs are the items 300-399 (Spyro's Adventure's location pieces on).
bool IsAdventurePack(const SkylanderInfo* sky) {
  return IsItem(sky) && sky->id >= 300 && sky->id < 400;
}

// The figure's trait, if the game names one: from its file's stats when there is one
// (SetFigureStatsTraitProvider), else from the toy (SetFigureTraitProvider).
std::optional<FigureTrait> TraitOf(const SkylanderInfo* sky, const portal::FigureStats* stats = nullptr) {
  if (!sky) return std::nullopt;
  FigureTraitProvider provider;
  FigureStatsTraitProvider stats_provider;
  {
    std::lock_guard lock(g_provider_mutex);
    provider = g_trait_provider;
    stats_provider = g_stats_trait_provider;
  }
  if (stats && stats_provider) {
    if (auto trait = stats_provider(sky->id, sky->variant, *stats)) return trait;
  }
  return provider ? provider(sky->id, sky->variant) : std::nullopt;
}

// A trait's symbol in a dark round frame; nothing while its picture isn't there.
void DrawTraitSymbol(ImDrawList* dl, ImVec2 c, float r, const FigureTrait& trait) {
  ImTextureID t = ArtTexture(trait.image);
  if (!t) return;
  dl->AddCircleFilled(c, r, Rgba(10, 12, 30, 215));
  dl->AddCircle(c, r, Rgba(244, 196, 84, 200), 0, S(1.5f));
  const float s = r * 0.82f;
  dl->AddImage(t, {c.x - s, c.y - s}, {c.x + s, c.y + s});
}

// The game's word for an adventure pack, else the menu's.
std::string AdventurePackName() {
  {
    std::lock_guard lock(g_provider_mutex);
    if (!g_pack_name.empty()) return g_pack_name;
  }
  return T(Text::kAdventurePack);
}

// portal_mode "hybrid": whether this figure file is a figure standing on the real portal (the same
// UID). Two copies of one figure would overwrite each other's progress, so the menu doesn't put
// it on.
bool OnRealPortal(const std::filesystem::path& path) {
  const std::vector<uint32_t> uids = RealPortalUids();
  if (uids.empty()) return false;
  const std::optional<portal::FigureData> data = portal::LoadFigureFile(path);
  return data && std::find(uids.begin(), uids.end(), portal::RealPortalBridge::UidOf(*data)) !=
                     uids.end();
}

// Whether a figure like that (a sidekick, an item) stands on the real portal.
bool RealPortalHas(bool (*is)(const SkylanderInfo*)) {
  for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
    if (IsRealPortalCopy(slot) && is(FigureIn(slot))) return true;
  }
  return false;
}

void ShowToast(std::string text);

// The player whose figure is in a slot. Figures placed some other way (start-up figure, the
// developer tools) belong to player 1.
uint32_t OwnerOf(int slot) {
  for (uint32_t u = 0; u < g_player_slots.size(); ++u) {
    if (std::find(g_player_slots[u].begin(), g_player_slots[u].end(), slot) !=
        g_player_slots[u].end()) {
      return u;
    }
  }
  return 0;
}

// The Skylanders the other players have on the portal, with whose they are.
std::vector<std::pair<uint32_t, const SkylanderInfo*>> OtherPlayersFigures(uint32_t user) {
  std::vector<std::pair<uint32_t, const SkylanderInfo*>> figures;
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return figures;
  for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
    if (!software->HasFigure(slot) || IsExtraSlot(slot)) continue;
    const SkylanderInfo* sky = FigureIn(slot);
    const uint32_t owner = OwnerOf(slot);
    if (sky && !IsItem(sky) && !IsCompanion(sky) && owner != user) figures.emplace_back(owner, sky);
  }
  return figures;
}

struct LiftedFigure {
  int slot = 0;
  std::optional<std::filesystem::path> path;
  std::optional<portal::FigureData> data;
  const SkylanderInfo* sky = nullptr;
};

std::vector<LiftedFigure> LiftPlayer(uint32_t user) {
  std::vector<LiftedFigure> lifted;
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return lifted;
  auto& slots = g_player_slots[user];
  slots.erase(std::remove_if(slots.begin(), slots.end(),
                             [&](int slot) { return !software->HasFigure(slot); }),
              slots.end());
  // Figures placed some other way (start-up figure, the developer tools) belong to player 1.
  if (slots.empty() && user == 0) {
    for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
      if (software->HasFigure(slot) && !OwnedByOther(slot, user) && !IsExtraSlot(slot) &&
          !IsItem(FigureIn(slot)) && !IsCompanion(FigureIn(slot))) {
        slots.push_back(slot);
      }
    }
  }
  for (int slot : slots) {
    LiftedFigure figure;
    figure.slot = slot;
    figure.path = software->Source(slot);
    figure.data = software->Figure(slot);
    if (figure.data) {
      figure.sky = portal::FindSkylander(portal::ReadFigureId(*figure.data),
                                         portal::ReadFigureVariant(*figure.data));
    }
    RemoveFigureFromSlot(slot);
    lifted.push_back(std::move(figure));
  }
  slots.clear();
  return lifted;
}

int FreeSlot(uint32_t user, const std::vector<int>& preferred, const std::vector<int>& taken) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  auto usable = [&](int slot) {
    return !software->HasFigure(slot) && !OwnedByOther(slot, user) &&
           std::find(taken.begin(), taken.end(), slot) == taken.end();
  };
  for (int slot : preferred) {
    if (usable(slot)) return slot;
  }
  for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
    if (usable(slot)) return slot;
  }
  return -1;
}

void PlaceForPlayer(uint32_t user, const std::vector<std::filesystem::path>& paths,
                    const std::vector<int>& preferred) {
  if (!GetSoftwarePortal()) return;
  auto& slots = g_player_slots[user];
  for (const auto& path : paths) {
    int slot = FreeSlot(user, preferred, slots);
    if (slot < 0) {
      REXLOG_WARN("Portal menu: no free portal slot for player {}", user + 1);
      return;
    }
    if (PlaceFigureFromFile(slot, path)) slots.push_back(slot);
  }
}

void RestoreLifted(uint32_t user, const std::vector<LiftedFigure>& lifted) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return;
  auto& slots = g_player_slots[user];
  for (const LiftedFigure& figure : lifted) {
    bool placed = false;
    if (figure.path) {
      placed = PlaceFigureFromFile(figure.slot, *figure.path);
    } else if (figure.data) {
      placed = software->PlaceFigure(figure.slot, *figure.data);
    }
    if (placed) slots.push_back(figure.slot);
  }
}

// The item put on through the menu, if it's still there.
const SkylanderInfo* PortalItem() {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  int& slot = g_item_slot;
  if (!software || slot < 0 || !software->HasFigure(slot)) {
    slot = -1;
    return nullptr;
  }
  return FigureIn(slot);
}

// The companion (sidekick, trap, or this player's vehicle) put on through the menu, if it's still
// there.
const SkylanderInfo* PortalSidekick(uint32_t user) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  int& slot = CompanionSlot(user);
  if (!software || slot < 0 || !software->HasFigure(slot)) {
    slot = -1;
    return nullptr;
  }
  return FigureIn(slot);
}

// Puts an item (or the sidekick) on the portal in place of the previous one; no path just
// takes the previous one off. A different slot each time, so the game sees the old one leave.
void SetExtraFigure(int& slot, uint32_t user, const std::optional<std::filesystem::path>& path) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return;
  const int old_slot = slot;
  if (slot >= 0 && software->HasFigure(slot)) RemoveFigureFromSlot(slot);
  slot = -1;
  if (!path) return;
  const int free = FreeSlot(user, {}, {old_slot});
  if (free >= 0 && PlaceFigureFromFile(free, *path)) slot = free;
  REXLOG_INFO("Portal menu: {} {} slot {}", PathUtf8(path->filename()),
              slot >= 0 ? "placed in" : "could not be placed in", free);
}

void SetPortalItem(uint32_t user, const std::optional<std::filesystem::path>& path) {
  SetExtraFigure(g_item_slot, user, path);
}

void SetPortalSidekick(uint32_t user, const std::optional<std::filesystem::path>& path) {
  SetExtraFigure(CompanionSlot(user), user, path);
  g_sidekick_user = user;
}

// An item change waits until the player's Skylander is back in the game: putting the figure back
// and taking the item off in the same moment made the game ask for a Skylander again. So the
// figure goes back first, and the item follows once the game is no longer busy with figures
// (SetGameBusyProvider), at least 0.7 s later and at most 10 s. A sidekick change goes the same
// way, together with an item change if both are pending.
struct PendingItem {
  uint32_t user = 0;
  bool item = false;  // an item change is pending
  std::optional<std::filesystem::path> path;  // nullopt: take it off
  bool sidekick = false;  // a sidekick change by sidekick_user is pending
  uint32_t sidekick_user = 0;
  std::optional<std::filesystem::path> sidekick_path;
  uint64_t serial = 0;
};
std::mutex g_item_mutex;
std::condition_variable g_item_cv;
std::optional<PendingItem> g_pending_item;
uint64_t g_item_serial = 0;
bool g_item_stop = false;
std::thread g_item_thread;
std::function<bool()> g_game_busy;  // guarded by g_provider_mutex
rex::ui::WindowedAppContext* g_item_app_context = nullptr;

void ItemThread() {
  std::unique_lock lock(g_item_mutex);
  while (true) {
    g_item_cv.wait(lock, [] { return g_item_stop || g_pending_item.has_value(); });
    if (g_item_stop) return;
    const PendingItem item = *g_pending_item;
    const auto start = Clock::now();
    while (true) {
      g_item_cv.wait_for(lock, std::chrono::milliseconds(50));
      if (g_item_stop) return;
      if (!g_pending_item || g_pending_item->serial != item.serial) break;  // replaced
      const auto waited = Clock::now() - start;
      if (waited < std::chrono::milliseconds(700)) continue;
      std::function<bool()> busy;
      {
        std::lock_guard provider_lock(g_provider_mutex);
        busy = g_game_busy;
      }
      lock.unlock();
      const bool still_busy = busy && busy();
      lock.lock();
      if (g_item_stop) return;
      if (!g_pending_item || g_pending_item->serial != item.serial) break;
      if (still_busy && waited < std::chrono::seconds(10)) continue;
      g_pending_item.reset();
      if (g_item_app_context) {
        g_item_app_context->CallInUIThread([item] {
          if (item.item) SetPortalItem(item.user, item.path);
          if (item.sidekick) SetPortalSidekick(item.sidekick_user, item.sidekick_path);
        });
      }
      break;
    }
  }
}

// Adds to a pending change (an item and a sidekick change can wait together).
PendingItem& PendingChange() {
  if (!g_pending_item) g_pending_item = PendingItem{};
  g_pending_item->serial = ++g_item_serial;
  if (!g_item_thread.joinable()) g_item_thread = std::thread(ItemThread);
  g_item_cv.notify_all();
  return *g_pending_item;
}

void ScheduleItemChange(uint32_t user, std::optional<std::filesystem::path> path) {
  std::lock_guard lock(g_item_mutex);
  PendingItem& change = PendingChange();
  change.user = user;
  change.item = true;
  change.path = std::move(path);
}

void ScheduleSidekickChange(uint32_t user, std::optional<std::filesystem::path> path) {
  std::lock_guard lock(g_item_mutex);
  PendingItem& change = PendingChange();
  change.sidekick = true;
  change.sidekick_user = user;
  change.sidekick_path = std::move(path);
}

void StopItemThread() {
  {
    std::lock_guard lock(g_item_mutex);
    g_item_stop = true;
    g_pending_item.reset();
  }
  g_item_cv.notify_all();
  if (g_item_thread.joinable()) g_item_thread.join();
}
// --- Menu input ---------------------------------------------------------------------------------

struct Actions {
  bool up = false, down = false, left = false, right = false;
  bool accept = false, back = false, details = false, lb = false, rb = false, lt = false,
       rt = false;
  bool take_off = false;  // Y: take the item off the portal
  bool options = false;   // Start: options (shown games, only my figures, leave the portal)

  bool Any() const {
    return up || down || left || right || accept || back || details || lb || rb || lt || rt || take_off || options;
  }
};

// Edge and key-repeat detection for one controller.
class PadReader {
 public:
  Actions Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    Actions a;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    a.accept = pressed(rex::input::X_INPUT_GAMEPAD_A);
    a.back = pressed(rex::input::X_INPUT_GAMEPAD_B);
    a.details = pressed(rex::input::X_INPUT_GAMEPAD_X);
    a.take_off = pressed(rex::input::X_INPUT_GAMEPAD_Y);
    a.options = pressed(rex::input::X_INPUT_GAMEPAD_START);
    a.lb = pressed(rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    a.rb = pressed(rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    const bool lt = pad.left_trigger > 128, rt = pad.right_trigger > 128;
    a.lt = lt && !prev_lt_;
    a.rt = rt && !prev_rt_;
    prev_lt_ = lt;
    prev_rt_ = rt;
    // The open button again closes the menu.
    if (menu_input::IsOpenChord(buttons) && !menu_input::IsOpenChord(prev_)) a.back = true;
    const float lx = float(int16_t(pad.thumb_lx)) / 32767.0f;
    const float ly = float(int16_t(pad.thumb_ly)) / 32767.0f;
    const bool dirs[4] = {
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) || ly > 0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) || ly < -0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_LEFT) || lx < -0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.55f,
    };
    bool* outs[4] = {&a.up, &a.down, &a.left, &a.right};
    const auto now = Clock::now();
    for (int i = 0; i < 4; ++i) {
      if (dirs[i] && !held_[i]) {
        *outs[i] = true;
        next_repeat_[i] = now + std::chrono::milliseconds(350);
      } else if (dirs[i] && now >= next_repeat_[i]) {
        *outs[i] = true;
        next_repeat_[i] = now + std::chrono::milliseconds(90);
      }
      held_[i] = dirs[i];
    }
    prev_ = buttons;
    return a;
  }

  // Nothing counts until every button of the opening press is released.
  bool Released(const rex::input::X_INPUT_GAMEPAD& pad) const {
    return uint16_t(pad.buttons) == 0 && pad.left_trigger < 30 && pad.right_trigger < 30;
  }

 private:
  uint16_t prev_ = 0;
  bool prev_lt_ = false, prev_rt_ = false;
  bool held_[4] = {};
  Clock::time_point next_repeat_[4]{};
};

Actions ReadKeyboard() {
  Actions a;
  auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  a.up = key(ImGuiKey_UpArrow) || key(ImGuiKey_W);
  a.down = key(ImGuiKey_DownArrow) || key(ImGuiKey_S);
  a.left = key(ImGuiKey_LeftArrow) || key(ImGuiKey_A);
  a.right = key(ImGuiKey_RightArrow) || key(ImGuiKey_D);
  // The same keys the keyboard controller emulation uses: Space/Enter = A, Backspace = B,
  // L = X, P = Y, 1/3 = LB/RB, Q/E = LT/RT.
  a.accept = once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
  a.back = once(ImGuiKey_Escape) || once(ImGuiKey_Backspace);
  a.details = once(ImGuiKey_L) || once(ImGuiKey_Tab);
  a.take_off = once(ImGuiKey_P);
  a.options = once(ImGuiKey_O);
  a.lb = once(ImGuiKey_1);
  a.rb = once(ImGuiKey_3);
  a.lt = once(ImGuiKey_Q);
  a.rt = once(ImGuiKey_E);
  return a;
}

void Merge(Actions& into, const Actions& from) {
  into.up |= from.up;
  into.down |= from.down;
  into.left |= from.left;
  into.right |= from.right;
  into.accept |= from.accept;
  into.back |= from.back;
  into.details |= from.details;
  into.lb |= from.lb;
  into.rb |= from.rb;
  into.lt |= from.lt;
  into.rt |= from.rt;
  into.take_off |= from.take_off;
  into.options |= from.options;
}

// --- The dialog ---------------------------------------------------------------------------------

// One line of the version list: what it puts on the portal.
struct Choice {
  std::string label;
  std::string badge;
  std::vector<const SkylanderInfo*> figures;
};

// "Blast Zone" + "Fire Kraken": Swap Force names a combination after the top's first word and
// the bottom's last ("Blast Kraken"). A matching pair keeps the swapper's own name.
std::string ComboName(const std::vector<const SkylanderInfo*>& figures) {
  if (figures.empty()) return {};
  if (figures.size() == 2) {
    if (portal::SwapperPartner(*figures[0]) == figures[1]) {
      return portal::DisplayName(portal::SwapperBaseName(*figures[0]));
    }
    const std::string top(portal::CharacterName(*figures[0]));
    const std::string bottom(portal::CharacterName(*figures[1]));
    if (!top.empty() && !bottom.empty()) {
      return top.substr(0, top.find(' ')) + " " + bottom.substr(bottom.rfind(' ') + 1);
    }
    return portal::DisplayName(portal::SwapperBaseName(*figures[0])) + " / " +
           portal::DisplayName(portal::SwapperBaseName(*figures[1]));
  }
  return FigureName(*figures[0]);
}

// A small rounded label; returns its width.
float Pill(ImDrawList* dl, ImVec2 pos, std::string_view text, ImU32 bg, ImU32 fg,
           float size = 14) {
  const ImVec2 ts = TextSize(BoldFont(), S(size), text);
  const ImVec2 end{pos.x + ts.x + S(14), pos.y + ts.y + S(4)};
  dl->AddRectFilled(pos, end, bg, S(8));
  Label(dl, BoldFont(), S(size), {pos.x + S(7), CapCentredY((pos.y + end.y) / 2, S(size))}, fg,
        text);
  return end.x - pos.x;
}

float PillWidth(std::string_view text, float size = 14) {
  return TextSize(BoldFont(), S(size), text).x + S(14);
}

class PortalMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  PortalMenuDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done, DeviceId device,
                   uint32_t user, bool from_keyboard, bool auto_opened)
      : ImGuiDialog(drawer),
        on_done_(std::move(on_done)),
        device_(device),
        user_(user),
        keyboard_(from_keyboard),
        auto_opened_(auto_opened) {
    g_german = menu_draw::GermanText();
    menu_input::SetMenuOpen(true);
    // The settings menu (F4, Back + RB) closes it like B: the lifted figure goes back.
    menu_input::SetMenuCloser([this] { Cancel(); });
    // The keyboard steers the menu of the player it plays as.
    const std::optional<DeviceId> keyboard = menu_input::KeyboardDevice();
    keyboard_controls_ = keyboard_ || (keyboard && menu_input::UserOf(*keyboard) == user_);
    // The hints start with what opened the menu.
    menu_draw::UseKeyboardHints(keyboard_, true);
    software_ = GetSoftwarePortal() != nullptr;
    if (software_) {
      library_.Build(user_);
      lifted_ = LiftPlayer(user_);
      lift_time_ = Clock::now();
      for (const auto& [owner, sky] : OtherPlayersFigures(user_)) {
        const std::string name = library_.CharacterOf(*sky);
        if (!name.empty()) taken_.emplace(name, owner);
      }
      current_item_ = PortalItem();
      current_sidekick_ = PortalSidekick(user_);
      // Recent starts with what is on the portal besides the Skylanders, so A takes it off.
      for (const SkylanderInfo* extra : {current_item_, current_sidekick_}) {
        if (auto index = library_.CharacterIndex(extra)) extras_.push_back(*index);
      }
      OpenFirstTab();
    }
    // Opened with a button, the figure was just lifted: the menu stays hidden while the game
    // reacts (the camera zooms out to the portal) and comes up with the game's own portal screen.
    // Without a lifted figure there is nothing to wait for.
    revealed_ = auto_opened_ || !software_ || lifted_.empty();
    if (revealed_) reveal_time_ = Clock::now();
    REXLOG_INFO("Portal menu: opened for player {}{}", user_ + 1,
                auto_opened_ ? " (the game asks for a Skylander)" : "");
  }

  ~PortalMenuDialog() override {
    if (state_ != State::kDone) menu_input::SetMenuOpen(false);
  }

  // Closes like the Back button: the lifted figure goes back on the portal.
  void Cancel() {
    if (state_ != State::kWaiting && state_ != State::kDone) StartCancel();
  }

  uint32_t user() const { return user_; }
  bool auto_opened() const { return auto_opened_; }

  // hybrid: the player put a figure on the real portal while the menu was open, so that one is
  // theirs now; the menu closes without putting the lifted figure back.
  void RealFigurePlaced() {
    if (state_ == State::kWaiting || state_ == State::kDone) return;
    pending_paths_.clear();
    restore_ = false;
    item_change_ = sidekick_change_ = false;
    state_ = State::kWaiting;
    REXLOG_INFO("Portal menu: player {} put a figure on the real portal", user_ + 1);
  }

  // The game started zooming out to the portal: come up shortly before its portal screen.
  void RevealSoon() {
    if (!revealed_) reveal_at_ = Clock::now() + std::chrono::milliseconds(600);
  }

  // The game shows its portal screen for this player: come up now if still waiting for it.
  void Reveal() {
    if (revealed_ || state_ == State::kWaiting || state_ == State::kDone) return;
    revealed_ = true;
    reveal_time_ = Clock::now();
    REXLOG_INFO("Portal menu: shown {} ms after the open button",
                std::chrono::duration_cast<std::chrono::milliseconds>(reveal_time_ - lift_time_)
                    .count());
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    SetScale(io.DisplaySize.y / 1080.0f);
    TrackMouse(io);
    if (state_ == State::kWaiting) {
      if (Clock::now() - lift_time_ >= std::chrono::milliseconds(350)) {
        if (restore_) {
          RestoreLifted(user_, lifted_);
        } else {
          std::vector<int> preferred;
          for (const LiftedFigure& f : lifted_) preferred.push_back(f.slot);
          PlaceForPlayer(user_, pending_paths_, preferred);
        }
        if (item_change_) ScheduleItemChange(user_, item_path_);
        if (sidekick_change_) ScheduleSidekickChange(user_, sidekick_path_);
        // The game's own way out: "leave the game" on its portal screen for this player.
        if (leave_) menu_input::TapButton(user_, LeaveButton());
        Finish();
      }
      return;
    }
    if (state_ == State::kDone) return;

    Actions actions = ReadActions();
    if (!revealed_) {
      // B or the open button again: never mind, the figure goes back.
      if (actions.back) {
        StartCancel();
        return;
      }
      if (!ShouldReveal()) return;
      Reveal();
      actions = {};
    }
    if (!software_) state_ = State::kMessage;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##portal_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    // The game stays visible, dimmed, around and slightly through the panel.
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, 120));
    const ImVec2 p0{W * 0.06f, H * 0.07f}, p1{W * 0.94f, H * 0.93f};
    DrawPanel(dl, p0, p1);
    DrawHeader(dl, p0, p1);
    if (state_ == State::kMessage) {
      DrawMessage(dl, p0, p1, actions);
    } else {
      HandleInput(actions);
      DrawBody(dl, p0, p1);
    }
    DrawFooter(dl, p0, p1);
    // Fades in over 0.2 s.
    FadeVertices(dl, first_vertex,
                 std::chrono::duration<float>(Clock::now() - reveal_time_).count() / 0.2f);
    ImGui::End();
  }

 private:
  enum class State {
    kBrowsing,
    kChoosing,
    kBuilding,
    kDetails,
    kOptions,
    kMessage,
    kWaiting,
    kDone
  };

  // Hidden after the open button, the menu comes up with the game's own portal screen (Reveal from
  // AutoOpenPortalMenu); if the game doesn't show one (a second player just drops out), once it
  // is clearly not busy with the figure; at the latest after portal_menu_open_delay_ms.
  bool ShouldReveal() const {
    if (reveal_at_ && Clock::now() >= *reveal_at_) return true;
    const auto waited = Clock::now() - lift_time_;
    if (waited >= std::chrono::milliseconds(REXCVAR_GET(portal_menu_open_delay_ms))) return true;
    if (waited < std::chrono::milliseconds(700)) return false;
    std::function<bool()> busy;
    {
      std::lock_guard lock(g_provider_mutex);
      busy = g_game_busy;
    }
    return busy && !busy();
  }

  // Recent opens first, on its first Skylander (the item and sidekick cards before it are there
  // to take off, not to be picked by a quick A); with nothing in it, the lifted figure's character.
  void OpenFirstTab() {
    tab_ = element_ = selected_ = scroll_row_ = 0;
    if (!library_.recent().empty()) selected_ = extras_.size();
    if (library_.recent().empty() && extras_.empty() && library_.tabs().size() > 1) {
      tab_ = 1;
      for (const LiftedFigure& figure : lifted_) {
        if (figure.sky && library_.Locate(figure.sky, tab_, element_, selected_)) break;
      }
    }
  }

  // --- input ---

  void TrackMouse(ImGuiIO& io) {
    if (!mouse_start_) {
      mouse_start_ = io.MousePos;
    } else if (!mouse_moved_ && ImGui::IsMousePosValid(&io.MousePos) &&
               ImGui::IsMousePosValid(&*mouse_start_)) {
      const float dx = io.MousePos.x - mouse_start_->x, dy = io.MousePos.y - mouse_start_->y;
      mouse_moved_ = dx * dx + dy * dy > S(12) * S(12);
    }
  }

  bool MouseSelects(ImVec2 a, ImVec2 b) const {
    const ImGuiIO& io = ImGui::GetIO();
    return mouse_moved_ && ImGui::IsMouseHoveringRect(a, b) &&
           (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
  }

  Actions ReadActions() {
    Actions actions;
    // Every controller of this player steers the menu: the one that opened it and any other.
    std::vector<DeviceId> pads = menu_input::PadsOfUser(user_);
    if (!keyboard_ && std::find(pads.begin(), pads.end(), device_) == pads.end()) {
      pads.push_back(device_);
    }
    bool pads_released = true;
    for (DeviceId id : pads) {
      const std::optional<rex::input::X_INPUT_GAMEPAD> pad = menu_input::ReadPad(id);
      if (!pad) continue;
      PadReader& reader = pad_readers_[uint64_t(id)];
      pads_released = pads_released && reader.Released(*pad);
      const Actions read = reader.Read(*pad);
      if (!waiting_for_release_) Merge(actions, read);
      if (!waiting_for_release_ && read.Any()) menu_draw::UseKeyboardHints(false);
    }
    if (waiting_for_release_) {
      const bool keys_released =
          !ImGui::IsKeyDown(ImGuiKey_F6) && !ImGui::IsKeyDown(ImGuiKey_Enter);
      if (pads_released && keys_released) waiting_for_release_ = false;
      return {};
    }
    if (keyboard_controls_) {
      const Actions keys = ReadKeyboard();
      if (keys.Any()) menu_draw::UseKeyboardHints(true);
      Merge(actions, keys);
    }
    return actions;
  }

  const Tab& CurrentTab() const { return library_.tabs()[tab_]; }
  // The stats of the owner's file of the figure (the one played last), for traits kept on it.
  const portal::FigureStats* OwnedStats(const SkylanderInfo* sky) const {
    const OwnedFigure* owned = library_.Owned(sky);
    return owned && owned->stats ? &*owned->stats : nullptr;
  }

  size_t ItemCount() const {
    const Tab& tab = CurrentTab();
    if (tab.recent) return extras_.size() + library_.recent().size();
    return tab.rows.empty() ? 0 : tab.rows[element_].size();
  }

  const Character* SelectedCharacter() const {
    const Tab& tab = CurrentTab();
    if (tab.recent && selected_ < extras_.size()) return &library_.characters()[extras_[selected_]];
    if (tab.recent || tab.rows.empty() || tab.rows[element_].empty()) return nullptr;
    const auto& chars = tab.rows[element_];
    return &library_.characters()[chars[std::min(selected_, chars.size() - 1)]];
  }

  const RecentEntry* SelectedRecent() const {
    if (!CurrentTab().recent || library_.recent().empty() || selected_ < extras_.size()) {
      return nullptr;
    }
    return &library_.recent()[std::min(selected_ - extras_.size(), library_.recent().size() - 1)];
  }

  // The player who has this character on the portal, if it's another one.
  std::optional<uint32_t> TakenBy(const std::string& character) const {
    auto it = taken_.find(character);
    if (it == taken_.end()) return std::nullopt;
    return it->second;
  }
  std::optional<uint32_t> TakenBy(const SkylanderInfo* sky) const {
    return sky ? TakenBy(library_.CharacterOf(*sky)) : std::nullopt;
  }
  std::optional<uint32_t> TakenBy(const Character& character) const {
    return character.item ? std::nullopt : TakenBy(character.name);
  }
  std::optional<uint32_t> TakenBy(const RecentEntry& entry) const {
    for (const SkylanderInfo* sky : entry.figures) {
      if (auto owner = TakenBy(sky)) return owner;
    }
    return std::nullopt;
  }

  // The current tab's versions first (Series 2 and LightCore in the Giants tab), then the rest.
  std::vector<Choice> ChoicesFor(const Character& character) const {
    std::vector<Choice> choices;
    const std::string& game = CurrentTab().game;
    for (int pass = 0; pass < 2; ++pass) {
      for (const Version& v : character.versions) {
        if (!game.empty() && (v.sky->game == game) != (pass == 0)) continue;
        if (game.empty() && pass == 1) continue;
        choices.push_back({FigureName(*v.sky), portal::VersionBadge(*v.sky), {v.sky}});
      }
    }
    return choices;
  }

  // The version a character's card stands for in the current tab: its version from the tab's game
  // played last, else the tab's first version from that game (Series 2 Drobot in Giants). Outside
  // a game tab, the version played last, else the first.
  const Version& ShownVersion(const Character& character) const {
    const std::string& game = CurrentTab().game;
    const Version* first = nullptr;
    const Version* best = nullptr;
    const OwnedFigure* best_owned = nullptr;
    for (const Version& v : character.versions) {
      if (!game.empty() && v.sky->game != game) continue;
      if (!first) first = &v;
      for (const SkylanderInfo* sky : {v.sky, v.bottom}) {
        const OwnedFigure* owned = library_.Owned(sky);
        if (owned && (!best_owned || owned->mtime > best_owned->mtime)) {
          best_owned = owned;
          best = &v;
        }
      }
    }
    if (best) return *best;
    return first ? *first : character.versions.front();
  }

  // What the preview shows: the swapper being built, the highlighted choice or recent entry, or
  // the character's version played last (else its first).
  std::vector<const SkylanderInfo*> PreviewFigures() const {
    if (state_ == State::kBuilding || (state_ == State::kDetails && previous_state_ == State::kBuilding)) {
      const SkylanderInfo* top = BuiltHalf(0);
      const SkylanderInfo* bottom = BuiltHalf(1);
      if (!top || !bottom) return {};
      return {top, bottom};
    }
    if ((state_ == State::kChoosing ||
         (state_ == State::kDetails && previous_state_ == State::kChoosing)) &&
        !choices_.empty()) {
      return choices_[std::min(choice_, choices_.size() - 1)].figures;
    }
    if (const RecentEntry* entry = SelectedRecent()) return entry->figures;
    if (const Character* character = SelectedCharacter()) {
      const Version& v = ShownVersion(*character);
      return v.bottom ? std::vector<const SkylanderInfo*>{v.sky, v.bottom}
                      : std::vector<const SkylanderInfo*>{v.sky};
    }
    return {};
  }

  void SwitchTab(int delta) {
    const size_t count = library_.tabs().size();
    if (!count) return;
    tab_ = (tab_ + count + delta) % count;
    element_ = 0;
    selected_ = 0;
    scroll_row_ = 0;
    if (CurrentTab().items && current_item_) {
      size_t tab = 0;
      if (library_.Locate(current_item_, tab, element_, selected_) && tab != tab_) {
        element_ = selected_ = 0;
      }
    }
  }

  void SwitchRow(int delta) {
    const Tab& tab = CurrentTab();
    if (tab.recent || tab.rows.empty()) return;
    element_ = (element_ + tab.rows.size() + delta) % tab.rows.size();
    selected_ = 0;
    scroll_row_ = 0;
  }

  void HandleInput(const Actions& a) {
    if (state_ == State::kDetails) {
      if (a.back || a.details) state_ = previous_state_;
      if (a.left) MoveDetailFocus(-1, 0);
      if (a.right) MoveDetailFocus(+1, 0);
      if (a.up) MoveDetailFocus(0, -1);
      if (a.down) MoveDetailFocus(0, +1);
      if (a.lb && detail_half_ > 0) --detail_half_, detail_focus_ = 0;
      if (a.rb && detail_half_ + 1 < PreviewFigures().size()) ++detail_half_, detail_focus_ = 0;
      return;
    }
    if (state_ == State::kOptions) {
      HandleOptions(a);
      return;
    }
    if (a.details) {
      previous_state_ = state_;
      state_ = State::kDetails;
      detail_half_ = 0;
      detail_focus_ = 0;
      return;
    }
    if (state_ == State::kBuilding) {
      HandleBuilder(a);
      return;
    }
    if (state_ == State::kChoosing) {
      if (choices_.empty()) {
        state_ = State::kBrowsing;
        return;
      }
      if (a.up) choice_ = (choice_ + choices_.size() - 1) % choices_.size();
      if (a.down) choice_ = (choice_ + 1) % choices_.size();
      if (a.back) state_ = State::kBrowsing;
      if (a.accept) Confirm(choices_[choice_].figures);
      return;
    }
    if (a.back) {
      StartCancel();
      return;
    }
    if (a.take_off && current_item_) {
      ToggleItem(*current_item_);
      return;
    }
    if (a.options) {
      OpenOptions();
      return;
    }
    // A swapper in Recent: LB changes its top, RB its bottom, the other half stays (the owner's
    // idea, 2026-10-23: right after opening the menu, without looking for the character again).
    if (const RecentEntry* entry = SelectedRecentSwapper(); entry && (a.lb || a.rb)) {
      OpenBuilder(entry->figures[0], entry->figures[1], a.lb ? 0 : 1, entry);
      return;
    }
    if (a.lt) SwitchTab(-1);
    if (a.rt) SwitchTab(+1);
    if (a.lb) SwitchRow(-1);
    if (a.rb) SwitchRow(+1);
    const size_t count = ItemCount();
    if (!count) return;
    const size_t cols = std::max<size_t>(1, columns_);
    if (a.left && selected_ > 0) --selected_;
    if (a.right && selected_ + 1 < count) ++selected_;
    if (a.up && selected_ >= cols) selected_ -= cols;
    if (a.down) {
      if (selected_ + cols < count) {
        selected_ += cols;
      } else if (selected_ / cols < (count - 1) / cols) {
        selected_ = count - 1;
      }
    }
    if (a.accept) Activate();
  }

  void Activate() {
    if (const RecentEntry* entry = SelectedRecent()) {
      if (!TakenBy(*entry)) ConfirmFiles(entry->paths, entry->figures);
      return;
    }
    const Character* character = SelectedCharacter();
    if (!character) return;
    if (character->item) {
      ToggleItem(*character->versions.front().sky);
      return;
    }
    if (character->sidekick) {
      ToggleSidekick(*ShownVersion(*character).sky);
      return;
    }
    if (TakenBy(*character)) return;
    if (character->swapper) {
      // Starts with the version the card shows, whole.
      const Version& v = ShownVersion(*character);
      OpenBuilder(v.sky, v.bottom, 0, nullptr);
      return;
    }
    choices_ = ChoicesFor(*character);
    if (choices_.size() == 1) {
      Confirm(choices_[0].figures);
      return;
    }
    choice_ = 0;
    choice_scroll_ = 0;
    state_ = State::kChoosing;
  }

  // --- swapper builder: pick a top and a bottom, then place both ---
  //
  // Each half is a grid of the swapper characters with their pictures, one column per element in
  // the game's order (Swap Force has two swappers of each), the top grid above the bottom one, so
  // all of them are in view at once (the owner, 2026-10-23: the two long name lists were hard to
  // choose from). The stick moves through both grids, LB / RB jump to the top / bottom one, Y
  // switches a tile between the character's versions (Legendary, Dark, ...).

  struct BuildTile {
    Element element = Element::kNone;
    std::vector<const SkylanderInfo*> versions;  // this half of each version, catalog order
    size_t version = 0;                          // the one the tile shows
    const SkylanderInfo* sky() const { return versions[version]; }
  };
  struct BuildHalf {
    std::vector<std::vector<BuildTile>> columns;  // one per element, never empty
    size_t col = 0, row = 0;                      // the chosen tile
  };

  void MakeBuildGrids() {
    for (int half = 0; half < 2; ++half) {
      BuildHalf& grid = build_[half];
      grid = {};
      for (const Character& c : library_.characters()) {
        if (!c.swapper) continue;
        BuildTile tile;
        tile.element = c.element;
        const OwnedFigure* latest = nullptr;
        for (const Version& v : c.versions) {
          if (!v.bottom) continue;
          const SkylanderInfo* sky = half == 0 ? v.sky : v.bottom;
          // A tile starts on the version of this half played last.
          const OwnedFigure* owned = library_.Owned(sky);
          if (owned && (!latest || owned->mtime > latest->mtime)) {
            latest = owned;
            tile.version = tile.versions.size();
          }
          tile.versions.push_back(sky);
        }
        if (tile.versions.empty()) continue;
        auto column = std::find_if(grid.columns.begin(), grid.columns.end(), [&](const auto& col) {
          return col.front().element == c.element;
        });
        if (column == grid.columns.end()) {
          grid.columns.emplace_back();
          column = grid.columns.end() - 1;
        }
        column->push_back(std::move(tile));
      }
      std::stable_sort(grid.columns.begin(), grid.columns.end(), [](const auto& a, const auto& b) {
        return ElementRank(a.front().element) < ElementRank(b.front().element);
      });
    }
  }

  const BuildTile* BuildTileAt(int half) const {
    const BuildHalf& grid = build_[half];
    if (grid.col >= grid.columns.size() || grid.row >= grid.columns[grid.col].size()) {
      return nullptr;
    }
    return &grid.columns[grid.col][grid.row];
  }
  const SkylanderInfo* BuiltHalf(int half) const {
    const BuildTile* tile = BuildTileAt(half);
    return tile ? tile->sky() : nullptr;
  }

  // Puts a half's choice on the tile of `sky`, showing that version.
  void SelectBuildHalf(int half, const SkylanderInfo* sky) {
    BuildHalf& grid = build_[half];
    for (size_t c = 0; c < grid.columns.size(); ++c) {
      for (size_t r = 0; r < grid.columns[c].size(); ++r) {
        BuildTile& tile = grid.columns[c][r];
        auto it = std::find(tile.versions.begin(), tile.versions.end(), sky);
        if (it == tile.versions.end()) continue;
        tile.version = size_t(it - tile.versions.begin());
        grid.col = c;
        grid.row = r;
        return;
      }
    }
  }

  // Opens on `top` + `bottom`, the top grid focused (`focus` 0) or the bottom one (1); `from` is
  // the recent entry being changed, whose file of the half that stays is kept.
  void OpenBuilder(const SkylanderInfo* top, const SkylanderInfo* bottom, int focus,
                   const RecentEntry* from) {
    if (build_[0].columns.empty()) MakeBuildGrids();
    if (build_[0].columns.empty() || build_[1].columns.empty()) return;
    SelectBuildHalf(0, top);
    SelectBuildHalf(1, bottom);
    build_focus_ = focus;
    build_keep_.reset();
    if (from) build_keep_ = *from;
    state_ = State::kBuilding;
  }

  // The highlighted recent entry if it is a swapper, a top with a bottom.
  const RecentEntry* SelectedRecentSwapper() const {
    const RecentEntry* entry = SelectedRecent();
    if (!entry || entry->figures.size() != 2) return nullptr;
    const bool swapper = portal::GetSwapperHalf(*entry->figures[0]) == SwapperHalf::kTop &&
                         portal::GetSwapperHalf(*entry->figures[1]) == SwapperHalf::kBottom;
    return swapper ? entry : nullptr;
  }

  void HandleBuilder(const Actions& a) {
    if (a.back) {
      state_ = State::kBrowsing;
      return;
    }
    if (a.lb) build_focus_ = 0;
    if (a.rb) build_focus_ = 1;
    BuildHalf& grid = build_[build_focus_];
    auto clamp_row = [&] { grid.row = std::min(grid.row, grid.columns[grid.col].size() - 1); };
    if (a.left && grid.col > 0) --grid.col, clamp_row();
    if (a.right && grid.col + 1 < grid.columns.size()) ++grid.col, clamp_row();
    // Up out of the bottom grid goes to the top one, down out of the top grid to the bottom one;
    // each keeps its own choice.
    if (a.up) {
      if (grid.row > 0) {
        --grid.row;
      } else {
        build_focus_ = 0;
      }
    }
    if (a.down) {
      if (grid.row + 1 < grid.columns[grid.col].size()) {
        ++grid.row;
      } else {
        build_focus_ = 1;
      }
    }
    if (a.take_off) {
      BuildTile& tile = grid.columns[grid.col][grid.row];
      tile.version = (tile.version + 1) % tile.versions.size();
    }
    if (a.accept) {
      const SkylanderInfo* top = BuiltHalf(0);
      const SkylanderInfo* bottom = BuiltHalf(1);
      if (top && bottom && !TakenBy(top) && !TakenBy(bottom)) {
        Confirm({top, bottom}, build_keep_ ? &*build_keep_ : nullptr);
      }
    }
  }

  // --- options ---

  struct OptionRow {
    enum class Kind { kLeave, kGame, kOwnedOnly } kind;
    std::string game;
  };

  std::vector<OptionRow> OptionRows() const {
    std::vector<OptionRow> rows;
    // Leaving only means something for a second player: player 1 alone would just be asked for a
    // Skylander again.
    if (user_ > 0) rows.push_back({OptionRow::Kind::kLeave, {}});
    for (const std::string& game : AllowedGames()) rows.push_back({OptionRow::Kind::kGame, game});
    rows.push_back({OptionRow::Kind::kOwnedOnly, {}});
    return rows;
  }

  bool GameShown(const std::string& game) const {
    return Contains(ShownGames(AllowedGames()), game);
  }

  void OpenOptions() {
    option_ = 0;
    state_ = State::kOptions;
  }

  void HandleOptions(const Actions& a) {
    const std::vector<OptionRow> rows = OptionRows();
    if (a.back || a.options) {
      state_ = State::kBrowsing;
      return;
    }
    if (rows.empty()) return;
    if (a.up) option_ = (option_ + rows.size() - 1) % rows.size();
    if (a.down) option_ = (option_ + 1) % rows.size();
    if (!a.accept) return;
    const OptionRow& row = rows[std::min(option_, rows.size() - 1)];
    switch (row.kind) {
      case OptionRow::Kind::kLeave:
        Leave();
        return;
      case OptionRow::Kind::kGame: {
        const std::vector<std::string> games = AllowedGames();
        std::vector<std::string> hidden = SplitList(REXCVAR_GET(portal_menu_hidden_games));
        if (GameShown(row.game)) {
          // The last shown game stays.
          if (ShownGames(games).size() <= 1) return;
          hidden.push_back(row.game);
        } else {
          hidden.erase(std::remove(hidden.begin(), hidden.end(), row.game), hidden.end());
        }
        std::string value;
        for (const std::string& game : hidden) value += (value.empty() ? "" : ";") + game;
        SetOption("portal_menu_hidden_games", value);
        break;
      }
      case OptionRow::Kind::kOwnedOnly:
        SetOption("portal_menu_owned_only", REXCVAR_GET(portal_menu_owned_only) ? "false" : "true");
        break;
    }
    RebuildLibrary();
  }

  // After an option changed: the lists again, on the same tab if it is still there.
  void RebuildLibrary() {
    const std::string title = CurrentTab().title;
    library_.Build(user_);
    build_[0].columns.clear();
    build_[1].columns.clear();
    tab_ = element_ = selected_ = scroll_row_ = 0;
    for (size_t t = 0; t < library_.tabs().size(); ++t) {
      if (library_.tabs()[t].title == title) tab_ = t;
    }
  }

  // A second player takes their Skylander off and leaves the game: with the figure gone the game
  // shows its portal screen for that player, and the menu presses that screen's B ("leave the
  // game") as it closes.
  void Leave() {
    pending_paths_.clear();
    restore_ = false;
    item_change_ = false;
    // A player who leaves takes the sidekick or trap along if they brought it, and their vehicle.
    sidekick_change_ = current_sidekick_ != nullptr &&
                       (GameCompanionKind() == CompanionKind::kVehicle || g_sidekick_user == user_);
    sidekick_path_.reset();
    leave_ = true;
    state_ = State::kWaiting;
    REXLOG_INFO("Portal menu: player {} leaves the game", user_ + 1);
  }

  void DrawOptions(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    const std::vector<OptionRow> rows = OptionRows();
    const float row_h = S(62);
    ImVec2 a, b;
    DrawOverlayPanel(dl, area0, area1, {S(660), S(96) + rows.size() * (row_h + S(8))}, a, b);
    Label(dl, BoldFont(), S(28), {a.x + S(28), a.y + S(22)}, kGold, T(Text::kOptions));
    for (size_t i = 0; i < rows.size(); ++i) {
      const OptionRow& row = rows[i];
      const ImVec2 r0{a.x + S(24), a.y + S(78) + i * (row_h + S(8))};
      const ImVec2 r1{b.x - S(24), r0.y + row_h};
      const bool active = i == option_;
      if (state_ == State::kOptions) {
        if (MouseSelects(r0, r1)) option_ = i;
        if (ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseClicked(0)) {
          option_ = i;
          Actions click;
          click.accept = true;
          HandleOptions(click);
          return;
        }
      }
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      std::string label;
      std::optional<bool> on;
      switch (row.kind) {
        case OptionRow::Kind::kLeave: {
          char text[96];
          std::snprintf(text, sizeof(text), T(Text::kLeaveInfo), user_ + 1);
          Label(dl, BoldFont(), S(23), {r0.x + S(20), r0.y + S(6)}, PlayerColor(user_),
                T(Text::kLeave));
          Label(dl, BoldFont(), S(16), {r0.x + S(20), r0.y + S(36)}, kDim, text);
          continue;
        }
        case OptionRow::Kind::kGame:
          label = std::string(T(Text::kShowGames)) + ": " + row.game;
          on = GameShown(row.game);
          break;
        case OptionRow::Kind::kOwnedOnly:
          label = T(Text::kOwnedOnly);
          on = REXCVAR_GET(portal_menu_owned_only);
          break;
      }
      Label(dl, BoldFont(), S(23), {r0.x + S(20), r0.y + S(16)}, kWhite, label);
      // A switch on the right: green and to the right when on.
      const float sw = S(74), sh = S(34);
      const ImVec2 s0{r1.x - sw - S(18), r0.y + (row_h - sh) / 2}, s1{s0.x + sw, s0.y + sh};
      dl->AddRectFilled(s0, s1, *on ? Rgba(60, 170, 80) : Rgba(70, 76, 110), sh / 2);
      const float knob = sh / 2 - S(4);
      dl->AddCircleFilled({*on ? s1.x - sh / 2 : s0.x + sh / 2, (s0.y + s1.y) / 2}, knob, kWhite);
      const char* state = *on ? T(Text::kOn) : T(Text::kOff);
      const ImVec2 ts = TextSize(BoldFont(), S(18), state);
      Label(dl, BoldFont(), S(18), {s0.x - ts.x - S(12), r0.y + S(20)}, kDim, state);
    }
  }

  // --- placing ---

  void ToggleItem(const SkylanderInfo& item) {
    if (current_item_ == &item) {
      item_path_.reset();
      REXLOG_INFO("Portal menu: player {} takes off {}", user_ + 1, item.name);
    } else {
      if (RealPortalHas(IsItem)) {
        ShowToast(T(Text::kRealItem));
        return;
      }
      std::optional<std::filesystem::path> path;
      if (const OwnedFigure* owned = library_.Owned(&item)) {
        path = owned->path;
      } else {
        path = CreateFigureFile(item);
      }
      if (!path) {
        REXLOG_WARN("Portal menu: could not create a figure file for {}", item.name);
        return;
      }
      if (OnRealPortal(*path)) {
        ShowToast(T(Text::kOnRealPortal));
        return;
      }
      item_path_ = path;
      REXLOG_INFO("Portal menu: player {} puts on {}", user_ + 1, item.name);
    }
    item_change_ = true;
    StartCancel();
  }

  // A sidekick goes on next to the Skylanders in place of the one there, or off again if it is
  // that one; the player's Skylander stays.
  void ToggleSidekick(const SkylanderInfo& sidekick) {
    if (current_sidekick_ && current_sidekick_->id == sidekick.id) {
      sidekick_path_.reset();
      REXLOG_INFO("Portal menu: player {} takes off companion {}", user_ + 1, sidekick.name);
    } else {
      if (RealPortalHas(IsCompanion)) {
        ShowToast(T(Text::kRealSidekick));
        return;
      }
      std::optional<std::filesystem::path> path;
      if (const OwnedFigure* owned = library_.Owned(&sidekick)) {
        path = owned->path;
      } else {
        path = CreateFigureFile(sidekick);
      }
      if (!path) {
        REXLOG_WARN("Portal menu: could not create a figure file for {}", sidekick.name);
        return;
      }
      if (OnRealPortal(*path)) {
        ShowToast(T(Text::kOnRealPortal));
        return;
      }
      sidekick_path_ = path;
      REXLOG_INFO("Portal menu: player {} brings companion {}", user_ + 1, sidekick.name);
    }
    sidekick_change_ = true;
    StartCancel();
  }

  // Each figure's file: the one `keep` (a recent entry) has for it, else the one played last, else
  // a new one.
  void Confirm(const std::vector<const SkylanderInfo*>& figures,
               const RecentEntry* keep = nullptr) {
    std::vector<std::filesystem::path> paths;
    std::vector<const SkylanderInfo*> placed;
    for (const SkylanderInfo* sky : figures) {
      if (!sky) continue;
      std::optional<std::filesystem::path> path;
      for (size_t i = 0; keep && i < keep->figures.size() && i < keep->paths.size(); ++i) {
        if (keep->figures[i] == sky) path = keep->paths[i];
      }
      if (!path) {
        if (const OwnedFigure* owned = library_.Owned(sky)) {
          path = owned->path;
        } else {
          path = CreateFigureFile(*sky);
        }
      }
      if (!path) {
        REXLOG_WARN("Portal menu: could not create a figure file for {}", sky->name);
        continue;
      }
      paths.push_back(*path);
      placed.push_back(sky);
    }
    ConfirmFiles(paths, placed);
  }

  void ConfirmFiles(const std::vector<std::filesystem::path>& paths,
                    const std::vector<const SkylanderInfo*>& figures) {
    if (paths.empty()) return;
    for (const auto& path : paths) {
      if (OnRealPortal(path)) {
        ShowToast(T(Text::kOnRealPortal));
        return;
      }
    }
    AddRecent(user_, paths);
    pending_paths_ = paths;
    restore_ = false;
    state_ = State::kWaiting;
    REXLOG_INFO("Portal menu: player {} places {}", user_ + 1,
                figures.empty() ? std::string("?") : ComboName(figures));
  }

  void StartCancel() {
    restore_ = true;
    state_ = State::kWaiting;
  }

  void Finish() {
    state_ = State::kDone;
    menu_input::SetMenuOpen(false);
    if (on_done_) on_done_();
  }

  // --- drawing ---

  void DrawHeader(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    OutlinedLabel(dl, TitleFont(), S(46), {p0.x + S(38), p0.y + S(20)}, kGold, T(Text::kTitle),
                  S(2.5f));
    char player[32];
    std::snprintf(player, sizeof(player), T(Text::kPlayer), user_ + 1);
    const ImVec2 ts = TextSize(BoldFont(), S(24), player);
    const ImVec2 c1{p1.x - S(38), p0.y + S(26)};
    const ImVec2 c0{c1.x - ts.x - S(40), c1.y};
    dl->AddRectFilled(c0, {c1.x, c1.y + S(42)}, PlayerColor(user_), S(21));
    dl->AddRect(c0, {c1.x, c1.y + S(42)}, Rgba(255, 255, 255, 120), S(21), 0, S(2));
    Label(dl, BoldFont(), S(24), {c0.x + S(20), CapCentredY(c0.y + S(21), S(24))}, kWhite, player);
  }

  void DrawMessage(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Actions& a) {
    const char* text = GetUsbPortal() || REXCVAR_GET(portal_mode) == "usb" ? T(Text::kUsbActive)
                                                                           : T(Text::kNoPortal);
    Label(dl, BoldFont(), S(30), {p0.x + S(60), (p0.y + p1.y) / 2 - S(30)}, kWhite, text,
          p1.x - p0.x - S(120));
    // Closes through kWaiting like B elsewhere, so this frame still draws as the message.
    if (a.back || a.accept) StartCancel();
  }

  void DrawFooter(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    const ImU32 grey = Rgba(78, 86, 120);
    // Without the emulated portal the menu only shows a message and has no library to look at
    // (it crashed here when B closed that message with the real USB portal active).
    if (state_ == State::kMessage || state_ == State::kDetails || !software_) {
      Hint(dl, {x, y}, "B", Rgba(232, 73, 43), T(Text::kBack), true, "Esc");
      return;
    }
    if (state_ == State::kOptions) {
      x += Hint(dl, {x, y}, "A", Rgba(76, 175, 80), T(Text::kToggle), true, "Enter");
      Hint(dl, {x, y}, "B", Rgba(232, 73, 43), T(Text::kClose), true, "Esc");
      return;
    }
    const Character* character = SelectedCharacter();
    const bool item = state_ == State::kBrowsing && character && character->item;
    const bool sidekick = state_ == State::kBrowsing && character && character->sidekick;
    const char* accept = item && current_item_ == character->versions.front().sky
                             ? T(Text::kTakeOff)
                         : sidekick ? (current_sidekick_ &&
                                               current_sidekick_->id == ShownVersion(*character).sky->id
                                           ? T(Text::kTakeOff)
                                           : T(Text::kBringAlong))
                                    : T(Text::kPlace);
    x += Hint(dl, {x, y}, "A", Rgba(76, 175, 80), accept, true, "Enter");
    x += Hint(dl, {x, y}, "B", Rgba(232, 73, 43), T(Text::kBack), true, "Esc");
    x += Hint(dl, {x, y}, "X", Rgba(46, 134, 222), T(Text::kDetails), true, "L");
    if (state_ == State::kBuilding) {
      x += Hint(dl, {x, y}, "LB / RB", grey, T(Text::kHalf), false, "1 / 3");
      if (const BuildTile* tile = BuildTileAt(build_focus_); tile && tile->versions.size() > 1) {
        Hint(dl, {x, y}, "Y", Rgba(230, 180, 40), T(Text::kVariant), true, "P");
      }
    } else if (state_ == State::kBrowsing) {
      x += Hint(dl, {x, y}, "LT / RT", grey, T(Text::kCategory), false, "Q / E");
      if (!CurrentTab().recent) {
        x += Hint(dl, {x, y}, "LB / RB", grey, CurrentTab().items ? T(Text::kGame) : T(Text::kElement), false,
                  "1 / 3");
      } else if (SelectedRecentSwapper()) {
        x += Hint(dl, {x, y}, "LB / RB", grey, T(Text::kChangeHalves), false, "1 / 3");
      }
      if (current_item_) {
        x += Hint(dl, {x, y}, "Y", Rgba(230, 180, 40), T(Text::kTakeOffItem), true, "P");
      }
      // Options last, at the right edge when there is room.
      const std::string options = user_ > 0
                                      ? std::string(T(Text::kOptions)) + " / " + T(Text::kLeave)
                                      : std::string(T(Text::kOptions));
      const float w = HintWidth("Start", options, false, "O");
      Hint(dl, {std::max(x, p1.x - S(38) - w + S(28)), y}, "Start", grey, options, false, "O");
    }
  }

  void DrawBody(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const float preview_w = (p1.x - p0.x) * 0.27f;
    const ImVec2 prev0{p0.x + S(38), p0.y + S(100)};
    const ImVec2 prev1{prev0.x + preview_w, p1.y - S(86)};
    const ImVec2 grid0{prev1.x + S(36), p0.y + S(100)};
    const ImVec2 grid1{p1.x - S(38), p1.y - S(86)};

    // Category tabs: Recent, the games, Items.
    float tx = grid0.x;
    const float tab_h = S(46);
    for (size_t t = 0; t < library_.tabs().size(); ++t) {
      const std::string& title = library_.tabs()[t].title;
      const ImVec2 ts = TextSize(BoldFont(), S(22), title);
      const ImVec2 a{tx, grid0.y}, b{tx + ts.x + S(40), grid0.y + tab_h};
      const bool active = t == tab_;
      dl->AddRectFilled(a, b, active ? kGold : Rgba(255, 255, 255, 26), S(23));
      if (active) dl->AddRect(a, b, Rgba(255, 240, 190), S(23), 0, S(2));
      Label(dl, BoldFont(), S(22), {a.x + S(20), CapCentredY(a.y + tab_h / 2, S(22))},
            active ? kInk : kDim, title);
      if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsMouseClicked(0) && state_ == State::kBrowsing) {
        tab_ = t;
        element_ = 0;
        selected_ = 0;
        scroll_row_ = 0;
      }
      tx = b.x + S(12);
    }

    ImVec2 area0{grid0.x, grid0.y + tab_h + S(20)};
    const Tab& tab = CurrentTab();
    if (!tab.recent) {
      DrawRowSelector(dl, area0, grid1.x, tab);
      area0.y += S(78);
    }
    DrawPreview(dl, prev0, prev1, PreviewFigures());

    const size_t count = ItemCount();
    if (!count) {
      const bool filtered = REXCVAR_GET(portal_menu_owned_only) && !tab.recent;
      Label(dl, BoldFont(), S(26), {area0.x, area0.y + S(10)}, kDim,
            filtered ? T(Text::kNothingOwned) : T(Text::kNothingRecent));
      columns_ = 1;
      if (state_ == State::kOptions) DrawOptions(dl, area0, grid1);
      return;
    }
    const float gap = S(16);
    const float card_w = S(172), card_h = S(236);
    columns_ = std::max<size_t>(1, size_t((grid1.x - area0.x + gap) / (card_w + gap)));
    const size_t rows_visible =
        std::max<size_t>(1, size_t((grid1.y - area0.y + gap) / (card_h + gap)));
    const size_t sel_row = selected_ / columns_;
    if (sel_row < scroll_row_) scroll_row_ = sel_row;
    if (sel_row >= scroll_row_ + rows_visible) scroll_row_ = sel_row - rows_visible + 1;

    dl->PushClipRect({area0.x - S(8), area0.y - S(8)}, {grid1.x + S(8), grid1.y}, true);
    for (size_t i = scroll_row_ * columns_; i < count && i < (scroll_row_ + rows_visible) * columns_;
         ++i) {
      const size_t row = i / columns_ - scroll_row_, col = i % columns_;
      const ImVec2 a{area0.x + col * (card_w + gap), area0.y + row * (card_h + gap)};
      const ImVec2 b{a.x + card_w, a.y + card_h};
      if (state_ == State::kBrowsing) {
        if (MouseSelects(a, b)) selected_ = i;
        if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsMouseClicked(0)) {
          selected_ = i;
          Activate();
        }
      }
      if (tab.recent && i < extras_.size()) {
        DrawCharacterCard(dl, a, b, library_.characters()[extras_[i]], i == selected_);
      } else if (tab.recent) {
        DrawRecentCard(dl, a, b, library_.recent()[i - extras_.size()], i == selected_);
      } else {
        DrawCharacterCard(dl, a, b, library_.characters()[tab.rows[element_][i]],
                          i == selected_);
      }
    }
    dl->PopClipRect();

    const size_t total_rows = (count + columns_ - 1) / columns_;
    if (total_rows > rows_visible) {
      const float track_h = grid1.y - area0.y;
      const float thumb_h = track_h * float(rows_visible) / float(total_rows);
      const float thumb_y = area0.y + track_h * float(scroll_row_) / float(total_rows);
      dl->AddRectFilled({grid1.x + S(12), thumb_y}, {grid1.x + S(18), thumb_y + thumb_h},
                        Rgba(255, 255, 255, 90), S(3));
    }

    const State overlay = state_ == State::kDetails ? previous_state_ : state_;
    if (overlay == State::kChoosing) DrawChoices(dl, area0, grid1);
    if (overlay == State::kBuilding) DrawBuilder(dl, area0, grid1);
    if (state_ == State::kDetails) DrawDetails(dl, area0, grid1);
    if (state_ == State::kOptions) DrawOptions(dl, area0, grid1);
  }

  // The rows of a tab: elements (medallion + name) or, for items, games. When the names don't
  // all fit, only the active row shows its name.
  void DrawRowSelector(ImDrawList* dl, ImVec2 a, float right, const Tab& tab) {
    const float h = S(58);
    auto label_of = [&](size_t e) -> std::string {
      return tab.items ? tab.row_titles[e] : std::string(ElementLabel(tab.elements[e]));
    };
    auto width_of = [&](size_t e, bool with_label) {
      const float icon = tab.items ? S(20) : S(62);
      return with_label ? icon + TextSize(BoldFont(), S(21), label_of(e)).x + S(22) : S(62);
    };
    float total = 0;
    for (size_t e = 0; e < tab.rows.size(); ++e) total += width_of(e, true) + S(10);
    const bool all_labels = tab.items || total <= right - a.x;
    float x = a.x;
    for (size_t e = 0; e < tab.rows.size(); ++e) {
      const Element element = tab.items ? Element::kNone : tab.elements[e];
      const bool active = e == element_;
      const bool with_label = all_labels || active;
      const float w = width_of(e, with_label);
      if (x + w > right) break;
      const ImVec2 b{x + w, a.y + h};
      dl->AddRectFilled({x, a.y}, b, active ? ElementColor(element, 1.0f, 255)
                                            : ElementColor(element, 0.40f, 210),
                        h / 2);
      if (active) dl->AddRect({x, a.y}, b, kGold, h / 2, 0, S(3));
      if (!tab.items) {
        DrawElementBadge(dl, {x + h / 2, a.y + h / 2}, S(23), element, active ? 255 : 215);
      }
      if (with_label) {
        Label(dl, BoldFont(), S(21),
              {x + (tab.items ? S(20) : S(60)), CapCentredY(a.y + h / 2, S(21))},
              active ? kInk : kWhite, label_of(e));
      }
      if (ImGui::IsMouseHoveringRect({x, a.y}, b) && ImGui::IsMouseClicked(0) &&
          state_ == State::kBrowsing) {
        element_ = e;
        selected_ = 0;
        scroll_row_ = 0;
      }
      x = b.x + S(10);
    }
  }

  void DrawCardFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, Element element, bool selected) {
    if (selected) {
      dl->AddRectFilled({a.x - S(6), a.y - S(6)}, {b.x + S(6), b.y + S(6)}, Rgba(244, 196, 84, 80),
                        S(18));
    }
    GradientRect(dl, a, b, ElementColor(element, 0.62f, 255), ElementColor(element, 0.30f, 255),
                 S(14));
    dl->AddRect(a, b, selected ? kGold : Rgba(255, 255, 255, 46), S(14), 0,
                selected ? S(4) : S(1.5f));
  }

  // A collection tile: the picture on top, the name below and, if given, the version badge (S2,
  // LightCore) under the name. Returns the picture's rectangle.
  std::pair<ImVec2, ImVec2> DrawTile(ImDrawList* dl, ImVec2 a, ImVec2 b, Element element,
                                     bool selected, const SkylanderInfo* picture,
                                     const std::string& name, const std::string& badge = {}) {
    DrawCardFrame(dl, a, b, element, selected);
    const float pad = S(9);
    const ImVec2 p0{a.x + pad, a.y + pad};
    const ImVec2 p1{b.x - pad, a.y + pad + (b.x - a.x - 2 * pad)};
    DrawFigurePicture(dl, p0, p1, picture, S(12));
    const float size = S(19), wrap = b.x - a.x - S(14);
    const ImVec2 ts = TextSize(BoldFont(), size, name, wrap);
    const float x = ts.x < wrap ? (a.x + b.x - ts.x) / 2 : a.x + S(7);
    dl->PushClipRect(a, b, true);
    Label(dl, BoldFont(), size, {x, p1.y + S(5)}, kWhite, name, wrap);
    if (!badge.empty()) {
      const ImVec2 bs = TextSize(BoldFont(), S(15), badge);
      Label(dl, BoldFont(), S(15), {(a.x + b.x - bs.x) / 2, p1.y + S(8) + ts.y}, kGold, badge);
    }
    dl->PopClipRect();
    return {p0, p1};
  }

  // Darkens a card another player's figure is using and says whose it is.
  void DrawTaken(ImDrawList* dl, ImVec2 a, ImVec2 b, uint32_t owner) {
    dl->AddRectFilled(a, b, Rgba(8, 10, 26, 165), S(12));
    char text[48];
    std::snprintf(text, sizeof(text), T(Text::kTaken), owner + 1);
    const ImVec2 ts = TextSize(BoldFont(), S(19), text);
    const ImVec2 c{(a.x + b.x) / 2, (a.y + b.y) / 2};
    const ImVec2 q0{c.x - ts.x / 2 - S(14), c.y - S(15)}, q1{c.x + ts.x / 2 + S(14), c.y + S(15)};
    dl->AddRectFilled(q0, q1, PlayerColor(owner), S(15));
    dl->AddRect(q0, q1, Rgba(255, 255, 255, 140), S(15), 0, S(1.5f));
    Label(dl, BoldFont(), S(19), {c.x - ts.x / 2, c.y - ts.y / 2}, kWhite, text);
  }

  void DrawCharacterCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const Character& character,
                         bool selected) {
    const Version& version = ShownVersion(character);
    const SkylanderInfo* shown = version.sky;
    const std::string name = character.item ? FigureName(*shown) : character.name;
    const auto [p0, p1] =
        DrawTile(dl, a, b, character.element, selected, shown, name, ShortBadge(*shown));
    if (character.item) {
      if (current_item_ == character.versions.front().sky) {
        const float w = PillWidth(T(Text::kOnPortal), 15);
        Pill(dl, {(p0.x + p1.x - w) / 2, p1.y - S(30)}, T(Text::kOnPortal), Rgba(60, 170, 80),
             kWhite, 15);
      }
      return;
    }
    if (character.swapper) Pill(dl, {p0.x + S(6), p0.y + S(6)}, "SWAP", kGold, kInk);
    if (character.sidekick && current_sidekick_ && current_sidekick_->id == shown->id) {
      const float w = PillWidth(T(Text::kOnPortal), 15);
      Pill(dl, {(p0.x + p1.x - w) / 2, p1.y - S(30)}, T(Text::kOnPortal), Rgba(60, 170, 80),
           kWhite, 15);
    }
    DrawElementBadge(dl, {p0.x + S(19), p1.y - S(19)}, S(15), character.element);
    const SkylanderInfo* trait_sky = version.bottom ? version.bottom : version.sky;
    if (auto trait = TraitOf(trait_sky, OwnedStats(trait_sky))) {
      DrawTraitSymbol(dl, {p1.x - S(21), p1.y - S(21)}, S(16), *trait);
    }
    if (character.versions.size() > 1) {
      Pill(dl, {p0.x + S(38), p1.y - S(27)},
           std::to_string(character.versions.size()) + " " + T(Text::kVersions),
           Rgba(10, 12, 30, 200), kWhite);
    }
    const OwnedFigure* owned = library_.Owned(version.sky);
    if (!owned && version.bottom) owned = library_.Owned(version.bottom);
    if (owned && owned->stats) {
      const std::string lv = "Lv " + std::to_string(owned->stats->level);
      Pill(dl, {p1.x - S(6) - PillWidth(lv), p0.y + S(6)}, lv, kGold, kInk);
    }
    if (auto owner = TakenBy(character)) DrawTaken(dl, a, b, *owner);
  }

  void DrawRecentCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const RecentEntry& entry,
                      bool selected) {
    const Element element = portal::FigureElement(*entry.figures[0]);
    const auto [p0, p1] =
        DrawTile(dl, a, b, element, selected, entry.figures[0], ComboName(entry.figures));
    // A swapper combination: the bottom's character small in the corner.
    if (entry.figures.size() == 2 && portal::SwapperPartner(*entry.figures[0]) != entry.figures[1]) {
      const float s = (p1.x - p0.x) * 0.42f;
      dl->AddRectFilled({p1.x - s - S(7), p1.y - s - S(7)}, {p1.x - S(3), p1.y - S(3)},
                        Rgba(10, 12, 30, 220), S(10));
      DrawFigurePicture(dl, {p1.x - s - S(5), p1.y - s - S(5)}, {p1.x - S(5), p1.y - S(5)},
                        entry.figures[1], S(8));
    }
    Pill(dl, {p0.x + S(6), p0.y + S(6)}, GameShortName(entry.figures[0]->game),
         Rgba(10, 12, 30, 200), kWhite);
    DrawElementBadge(dl, {p0.x + S(19), p1.y - S(19)}, S(15), element);
    if (entry.figures.size() == 1 || portal::SwapperPartner(*entry.figures[0]) == entry.figures[1]) {
      // (a mixed swapper combination shows its bottom's picture in that corner instead)
      if (auto trait = TraitOf(entry.figures.back(), OwnedStats(entry.figures.back()))) {
        DrawTraitSymbol(dl, {p1.x - S(21), p1.y - S(21)}, S(16), *trait);
      }
    }
    if (const OwnedFigure* owned = library_.OwnedAt(entry.paths[0]); owned && owned->stats) {
      const std::string lv = "Lv " + std::to_string(owned->stats->level);
      Pill(dl, {p1.x - S(6) - PillWidth(lv), p0.y + S(6)}, lv, kGold, kInk);
    }
    if (auto owner = TakenBy(entry)) DrawTaken(dl, a, b, *owner);
  }

  // Level and gold, and the bar to the next level like the game's own one. Returns the y below it.
  float DrawLevelBar(ImDrawList* dl, ImVec2 a, float width, const portal::FigureStats& s,
                     float scale = 1.0f) {
    char text[96];
    std::snprintf(text, sizeof(text), "Level %u", unsigned(s.level));
    OutlinedLabel(dl, TitleFont(), S(30) * scale, a, kGold, text, S(1.5f));
    // Gold: the game's coin and the amount at the right.
    const std::string gold = std::to_string(s.gold);
    const ImVec2 gs = TextSize(BoldFont(), S(22) * scale, gold);
    const float coin = S(11) * scale;
    const ImVec2 cc{a.x + width - gs.x - coin - S(10), a.y + S(18) * scale};
    DrawGoldCoin(dl, cc, coin);
    Label(dl, BoldFont(), S(22) * scale, {cc.x + coin + S(6), a.y + S(6) * scale}, kWhite, gold);
    float y = a.y + S(42) * scale;
    const float h = S(24) * scale;
    const ImVec2 b0{a.x, y}, b1{a.x + width, y + h};
    dl->AddRectFilled(b0, b1, Rgba(8, 10, 28, 210), h / 2);
    const float fraction =
        s.next_xp > s.level_xp ? std::clamp(float(s.xp - s.level_xp) / float(s.next_xp - s.level_xp),
                                            0.0f, 1.0f)
                               : 1.0f;
    if (fraction > 0.0f) {
      const float inset = S(3);
      const float fill = std::max(h - 2 * inset, (width - 2 * inset) * fraction);
      GradientRect(dl, {b0.x + inset, b0.y + inset}, {b0.x + inset + fill, b1.y - inset},
                   Rgba(150, 228, 255), Rgba(40, 132, 228), (h - 2 * inset) / 2);
    }
    dl->AddRect(b0, b1, Rgba(255, 255, 255, 90), h / 2, 0, S(1.5f));
    if (s.next_xp) {
      std::snprintf(text, sizeof(text), "%u / %u %s", unsigned(s.xp), unsigned(s.next_xp),
                    T(Text::kXp));
    } else {
      std::snprintf(text, sizeof(text), "%u %s \xC2\xB7 %s", unsigned(s.xp), T(Text::kXp),
                    T(Text::kTopLevelShort));
    }
    const ImVec2 ts = TextSize(BoldFont(), S(15) * scale, text);
    OutlinedLabel(dl, BoldFont(), S(15) * scale, {(b0.x + b1.x - ts.x) / 2, y + (h - ts.y) / 2},
                  kWhite, text, S(1));
    return y + h + S(12) * scale;
  }

  // An upgrade as the game's icon: bright with a check when bought, dimmed otherwise.
  void DrawUpgradeIcon(ImDrawList* dl, ImVec2 a, float size, const FigureUpgrade& u, bool owned,
                       bool focused) {
    const ImVec2 b{a.x + size, a.y + size};
    if (focused) {
      dl->AddRectFilled({a.x - S(5), a.y - S(5)}, {b.x + S(5), b.y + S(5)},
                        Rgba(244, 196, 84, 90), S(14));
    }
    dl->AddRectFilled(a, b, owned ? Rgba(36, 56, 128, 235) : Rgba(16, 18, 36, 230), S(10));
    const float pad = size * 0.06f;
    if (ImTextureID t = ArtTexture(u.image)) {
      dl->AddImageRounded(t, {a.x + pad, a.y + pad}, {b.x - pad, b.y - pad}, {0, 0}, {1, 1},
                          owned ? IM_COL32_WHITE : Rgba(150, 150, 170, 120), S(8));
    } else if (u.image.empty()) {
      // A game without upgrade pictures (Spyro's Adventure lists them as text): an upgrade arrow,
      // gold once bought.
      const ImVec2 c{(a.x + b.x) / 2, (a.y + b.y) / 2};
      const float s = size * 0.22f;
      const ImU32 col = owned ? kGold : Rgba(150, 156, 190, 150);
      dl->AddTriangleFilled({c.x, c.y - s * 1.3f}, {c.x - s * 1.1f, c.y}, {c.x + s * 1.1f, c.y}, col);
      dl->AddRectFilled({c.x - s * 0.45f, c.y - S(1)}, {c.x + s * 0.45f, c.y + s * 1.2f}, col);
    } else {
      const ImVec2 qs = TextSize(TitleFont(), size * 0.45f, "?");
      Label(dl, TitleFont(), size * 0.45f, {(a.x + b.x - qs.x) / 2, (a.y + b.y - qs.y) / 2},
            Rgba(255, 255, 255, 90), "?");
    }
    dl->AddRect(a, b, focused ? kGold : owned ? Rgba(130, 205, 255, 200) : Rgba(255, 255, 255, 40),
                S(10), 0, focused ? S(3) : S(1.5f));
    if (owned && u.bit >= 0) {
      const float r = size * 0.15f;
      const ImVec2 c{b.x - r * 0.6f, b.y - r * 0.6f};
      dl->AddCircleFilled(c, r, Rgba(60, 170, 80));
      dl->AddCircle(c, r, IM_COL32_WHITE, 0, S(1.5f));
      const ImVec2 tick[] = {{c.x - r * 0.5f, c.y}, {c.x - r * 0.1f, c.y + r * 0.42f},
                             {c.x + r * 0.55f, c.y - r * 0.4f}};
      dl->AddPolyline(tick, 3, IM_COL32_WHITE, 0, std::max(1.5f, r * 0.28f));
    }
  }

  // The hat: its picture, the game's name for it and what it adds.
  float DrawHat(ImDrawList* dl, ImVec2 a, float width, uint16_t hat, bool compact) {
    const float pic = compact ? S(52) : S(70);
    auto info = HatInfoFor(hat);
    dl->AddRectFilled(a, {a.x + pic, a.y + pic}, Rgba(255, 255, 255, 26), S(10));
    if (ImTextureID t = info ? ArtTexture(info->image) : ImTextureID{}) {
      dl->AddImageRounded(t, {a.x + S(3), a.y + S(3)}, {a.x + pic - S(3), a.y + pic - S(3)}, {0, 0},
                          {1, 1}, IM_COL32_WHITE, S(8));
    } else {
      // No picture on this disc (Imaginators shows its hats as 3D models): a plain top hat.
      const ImU32 c = Rgba(255, 255, 255, 110);
      const float cx = a.x + pic * 0.5f;
      dl->AddRectFilled({cx - pic * 0.18f, a.y + pic * 0.24f}, {cx + pic * 0.18f, a.y + pic * 0.64f}, c,
                        pic * 0.04f);
      dl->AddRectFilled({cx - pic * 0.32f, a.y + pic * 0.64f}, {cx + pic * 0.32f, a.y + pic * 0.74f}, c,
                        pic * 0.05f);
    }
    const float x = a.x + pic + S(12);
    Label(dl, BoldFont(), compact ? S(19) : S(22), {x, a.y + S(2)}, kWhite, HatName(hat),
          width - pic - S(12));
    float y = a.y + (compact ? S(26) : S(32));
    if (info) {
      for (const HatStat& stat : info->stats) {
        float sx = x;
        if (ImTextureID t = ArtTexture(stat.image)) {
          dl->AddImage(t, {sx, y}, {sx + S(20), y + S(20)});
          sx += S(24);
        }
        Label(dl, BoldFont(), compact ? S(15) : S(17), {sx, y + S(1)}, Rgba(150, 230, 150),
              StatText(stat), width - (sx - a.x));
        y += compact ? S(20) : S(24);
      }
    }
    return std::max(y, a.y + pic) + S(10);
  }

  void DrawPreview(ImDrawList* dl, ImVec2 a, ImVec2 b,
                   const std::vector<const SkylanderInfo*>& figures) {
    if (figures.empty()) {
      dl->AddRectFilled(a, b, Rgba(255, 255, 255, 12), S(20));
      return;
    }
    const SkylanderInfo* main = figures[0];
    const bool item = IsItem(main);
    const Element element = portal::FigureElement(*main);
    GradientRect(dl, a, b, ElementColor(element, 0.50f, 245), ElementColor(element, 0.18f, 245),
                 S(20));
    dl->AddRect(a, b, Rgba(255, 255, 255, 40), S(20), 0, S(1.5f));
    // Element band with the medallion.
    const float band = S(62);
    dl->AddRectFilled(a, {b.x, a.y + band}, ElementColor(element), S(20),
                      ImDrawFlags_RoundCornersTop);
    DrawElementBadge(dl, {a.x + S(40), a.y + band / 2}, S(25), element);
    Label(dl, BoldFont(), S(28), {a.x + S(76), a.y + S(14)}, kInk,
          item ? T(Text::kItems) : ElementLabel(element));

    // The picture, standing on a portal; a swapper combination shows both characters.
    float y = a.y + band + S(16);
    const float pic = std::min(b.x - a.x - S(80), S(170));
    const float cx = (a.x + b.x) / 2;
    const float portal_y = y + pic - pic * 0.07f;
    const float portal_bottom =
        DrawPortalOfPower(dl, {cx, portal_y}, pic * 1.5f,
                          item ? IM_COL32(120, 210, 255, 255) : ElementColor(element));
    const bool combo = figures.size() == 2 && portal::SwapperPartner(*figures[0]) != figures[1];
    if (combo) {
      const float s = pic * 0.58f;
      DrawFigurePicture(dl, {cx - s - S(5), y + pic - s - S(8)}, {cx - S(5), y + pic - S(8)},
                        figures[0], S(14));
      DrawFigurePicture(dl, {cx + S(5), y + pic - s - S(8)}, {cx + s + S(5), y + pic - S(8)},
                        figures[1], S(14));
    } else {
      DrawFigurePicture(dl, {cx - pic / 2, y}, {cx + pic / 2, y + pic}, main, S(18));
    }
    y = portal_bottom + S(14);

    const float wrap = b.x - a.x - S(44);
    const std::string name = ComboName(figures);
    OutlinedLabel(dl, TitleFont(), S(38), {a.x + S(22), y}, kWhite, name, S(2));
    y += TextSize(TitleFont(), S(38), name, wrap).y + S(8);
    // Badges.
    float bx = a.x + S(22);
    std::vector<std::string> badges;
    if (!item) badges.push_back(BadgeLabel(portal::VersionBadge(*main)));
    badges.emplace_back(GameShortName(main->game));
    for (const std::string& text : badges) {
      const ImVec2 ts = TextSize(BoldFont(), S(18), text);
      dl->AddRectFilled({bx, y}, {bx + ts.x + S(20), y + S(30)}, Rgba(255, 255, 255, 40), S(15));
      Label(dl, BoldFont(), S(18), {bx + S(10), y + S(4)}, kWhite, text);
      bx += ts.x + S(30);
    }
    if (auto trait = TraitOf(figures.back(), OwnedStats(figures.back()))) {
      const ImVec2 ts = TextSize(BoldFont(), S(18), trait->name);
      dl->AddRectFilled({bx, y}, {bx + ts.x + S(48), y + S(30)}, Rgba(244, 196, 84, 60), S(15));
      DrawTraitSymbol(dl, {bx + S(16), y + S(15)}, S(13), *trait);
      Label(dl, BoldFont(), S(18), {bx + S(36), y + S(4)}, kWhite, trait->name);
    }
    y += S(44);

    if (figures.size() == 2) {
      for (int i = 0; i < 2; ++i) {
        const SkylanderInfo* half = figures[i];
        const float bh = S(56);
        dl->AddRectFilled({a.x + S(22), y}, {b.x - S(22), y + bh},
                          ElementColor(portal::FigureElement(*half), 0.62f, 240), S(12));
        Label(dl, BoldFont(), S(15), {a.x + S(36), y + S(5)}, Rgba(232, 232, 245),
              T(i == 0 ? Text::kTop : Text::kBottom));
        Label(dl, BoldFont(), S(21), {a.x + S(36), y + S(25)}, kWhite,
              portal::DisplayName(half->name), wrap - S(28));
        y += bh + S(8);
      }
      y += S(4);
    }

    const OwnedFigure* owned = library_.Owned(main);
    if (owned && !item && owned->stats) {
      const portal::FigureStats& stats = *owned->stats;
      const float width = b.x - a.x - S(44);
      y = DrawLevelBar(dl, {a.x + S(22), y}, width, stats);
      if (stats.hat) y = DrawHat(dl, {a.x + S(22), y}, width, stats.hat, true);
      // The upgrades bought, as the game's icons in a row.
      if (auto upgrades = UpgradesFor(*main)) {
        const float size = S(46), gap = S(7);
        float x = a.x + S(22);
        int shown = 0, path_index = 0;
        for (const FigureUpgradePath& path : upgrades->paths) {
          for (const FigureUpgrade& u : path.upgrades) {
            if (path.kind == FigureUpgradePath::Kind::kStart) continue;
            if (!UpgradeOwned(stats, path, path_index, u)) continue;
            if (x + size > b.x - S(22)) {
              x = a.x + S(22);
              y += size + gap;
            }
            if (y + size > b.y - S(16)) break;
            DrawUpgradeIcon(dl, {x, y}, size, u, true, false);
            x += size + gap;
            ++shown;
          }
          if (path.kind == FigureUpgradePath::Kind::kPath) ++path_index;
        }
        if (shown) {
          y += size + gap;
        } else {
          Label(dl, BoldFont(), S(18), {a.x + S(22), y}, kDim, T(Text::kNoUpgrades), wrap);
          y += S(28);
        }
      }
      if (!stats.nickname.empty() && y + S(30) < b.y) {
        Label(dl, BoldFont(), S(21), {a.x + S(22), y + S(4)}, kDim, "\"" + stats.nickname + "\"",
              wrap);
      }
    }
  }

  void DrawOverlayPanel(ImDrawList* dl, ImVec2 area0, ImVec2 area1, ImVec2 size, ImVec2& a,
                        ImVec2& b) {
    const ImVec2 c{(area0.x + area1.x) / 2, (area0.y + area1.y) / 2};
    a = {c.x - size.x / 2, c.y - size.y / 2};
    b = {c.x + size.x / 2, c.y + size.y / 2};
    dl->AddRectFilled({area0.x - S(12), area0.y - S(12)}, {area1.x + S(12), area1.y + S(12)},
                      Rgba(6, 10, 30, 160), S(18));
    dl->AddRectFilled(a, b, Rgba(22, 30, 76, 250), S(20));
    dl->AddRect(a, b, kGold, S(20), 0, S(3));
  }

  void DrawChoices(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    const float row_h = S(70);
    const size_t visible = std::min<size_t>(choices_.size(), 6);
    ImVec2 a, b;
    DrawOverlayPanel(dl, area0, area1, {S(640), S(90) + visible * (row_h + S(8))}, a, b);
    Label(dl, BoldFont(), S(28), {a.x + S(28), a.y + S(22)}, kGold, T(Text::kChooseVersion));
    if (choice_ < choice_scroll_) choice_scroll_ = choice_;
    if (choice_ >= choice_scroll_ + visible) choice_scroll_ = choice_ - visible + 1;
    for (size_t i = choice_scroll_; i < choices_.size() && i < choice_scroll_ + visible; ++i) {
      const Choice& choice = choices_[i];
      const ImVec2 r0{a.x + S(24), a.y + S(78) + (i - choice_scroll_) * (row_h + S(8))};
      const ImVec2 r1{b.x - S(24), r0.y + row_h};
      const bool active = i == choice_;
      if (state_ == State::kChoosing) {
        if (MouseSelects(r0, r1)) choice_ = i;
        if (ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseClicked(0)) {
          Confirm(choice.figures);
          return;
        }
      }
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      const float ph = row_h - S(12);
      DrawFigurePicture(dl, {r0.x + S(8), r0.y + S(6)}, {r0.x + S(8) + ph, r0.y + S(6) + ph},
                        choice.figures[0], S(8));
      const float tx = r0.x + S(20) + ph;
      Label(dl, BoldFont(), S(23), {tx, r0.y + S(8)}, kWhite, choice.label);
      std::string sub = BadgeLabel(choice.badge);
      if (choice.figures[0]->game != CurrentTab().title) {
        sub += "   " + std::string(GameShortName(choice.figures[0]->game));
      }
      if (const OwnedFigure* owned = library_.Owned(choice.figures[0]); owned && owned->stats) {
        sub += "   Lv " + std::to_string(owned->stats->level);
      }
      Label(dl, BoldFont(), S(17), {tx, r0.y + S(39)}, kDim, sub);
    }
  }

  // The two grids, the top one above, each with the chosen version's name; the preview shows
  // the combination.
  void DrawBuilder(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    ImVec2 a, b;
    const float w = std::min(area1.x - area0.x, S(1240));
    DrawOverlayPanel(dl, area0, area1, {w, area1.y - area0.y}, a, b);
    Label(dl, BoldFont(), S(26), {a.x + S(28), a.y + S(20)}, kGold, T(Text::kBuildSwapper));
    const SkylanderInfo* top = BuiltHalf(0);
    const SkylanderInfo* bottom = BuiltHalf(1);
    if (top && bottom) {
      const std::string combo = ComboName({top, bottom});
      const ImVec2 cs = TextSize(TitleFont(), S(34), combo);
      OutlinedLabel(dl, TitleFont(), S(34), {b.x - S(28) - cs.x, a.y + S(14)}, kWhite, combo,
                    S(2));
    }

    // Tiles as large as both grids allow, the columns centred.
    size_t cols = 1, rows[2] = {1, 1};
    for (int half = 0; half < 2; ++half) {
      cols = std::max(cols, build_[half].columns.size());
      for (const auto& column : build_[half].columns) rows[half] = std::max(rows[half], column.size());
    }
    const float gap = S(10), label_h = S(38), name_h = S(26), half_gap = S(12);
    const float grids_y = a.y + S(72);
    const float rows_h = b.y - S(18) - grids_y - 2 * label_h - half_gap -
                         float(rows[0] - 1 + rows[1] - 1) * gap;
    const float tile_w =
        std::min({S(150), (b.x - a.x - S(56) - float(cols - 1) * gap) / float(cols),
                  rows_h / float(rows[0] + rows[1]) - name_h});
    const float tile_h = tile_w + name_h;
    const float x0 = (a.x + b.x - (float(cols) * tile_w + float(cols - 1) * gap)) / 2;

    float y = grids_y;
    for (int half = 0; half < 2; ++half) {
      BuildHalf& grid = build_[half];
      const bool focused = build_focus_ == half;
      const char* title = T(half == 0 ? Text::kTop : Text::kBottom);
      Label(dl, BoldFont(), S(23), {x0, y + S(4)}, focused ? kGold : kDim, title);
      if (const BuildTile* tile = BuildTileAt(half)) {
        const float tx = x0 + TextSize(BoldFont(), S(23), title).x + S(18);
        Label(dl, BoldFont(), S(20), {tx, y + S(6)}, focused ? kWhite : kDim,
              portal::DisplayName(portal::SwapperBaseName(*tile->sky())));
      }
      y += label_h;
      for (size_t c = 0; c < grid.columns.size(); ++c) {
        for (size_t r = 0; r < grid.columns[c].size(); ++r) {
          const ImVec2 t0{x0 + float(c) * (tile_w + gap), y + float(r) * (tile_h + gap)};
          const ImVec2 t1{t0.x + tile_w, t0.y + tile_h};
          if (state_ == State::kBuilding &&
              (MouseSelects(t0, t1) ||
               (ImGui::IsMouseHoveringRect(t0, t1) && ImGui::IsMouseClicked(0)))) {
            build_focus_ = half;
            grid.col = c;
            grid.row = r;
          }
          const bool chosen = c == grid.col && r == grid.row;
          DrawBuildTile(dl, t0, t1, grid.columns[c][r], chosen, build_focus_ == half);
        }
      }
      y += float(rows[half]) * (tile_h + gap) - gap + half_gap;
    }
  }

  // A builder tile: the half's picture, the character's name below, the version's own word
  // ("Legendary") and one dot per version on the picture, the level. The other grid is dimmed but
  // for its choice.
  void DrawBuildTile(ImDrawList* dl, ImVec2 a, ImVec2 b, const BuildTile& tile, bool chosen,
                     bool focused) {
    const SkylanderInfo* sky = tile.sky();
    DrawCardFrame(dl, a, b, tile.element, chosen && focused);
    if (chosen && !focused) dl->AddRect(a, b, Rgba(255, 255, 255, 190), S(14), 0, S(3));
    const float pad = S(7);
    const ImVec2 p0{a.x + pad, a.y + pad};
    const ImVec2 p1{b.x - pad, a.y + pad + (b.x - a.x - 2 * pad)};
    DrawFigurePicture(dl, p0, p1, sky, S(10));
    const std::string base = portal::DisplayName(portal::SwapperBaseName(*sky));
    const std::string_view character = portal::CharacterName(*sky);
    const std::string name = character.empty() ? base : portal::DisplayName(character);
    dl->PushClipRect(a, b, true);
    const ImVec2 ts = TextSize(BoldFont(), S(16), name);
    Label(dl, BoldFont(), S(16), {std::max(a.x + S(5), (a.x + b.x - ts.x) / 2), p1.y + S(4)},
          kWhite, name);
    if (base.size() > name.size() &&
        base.compare(base.size() - name.size(), name.size(), name) == 0) {
      std::string word = base.substr(0, base.size() - name.size());
      while (!word.empty() && word.back() == ' ') word.pop_back();
      if (!word.empty()) {
        const float pw = PillWidth(word, 13);
        Pill(dl, {std::max(p0.x, (p0.x + p1.x - pw) / 2), p1.y - S(42)}, word, kGold, kInk, 13);
      }
    }
    if (tile.versions.size() > 1) {
      const float step = S(12);
      float x = (p0.x + p1.x) / 2 - step * float(tile.versions.size() - 1) / 2;
      for (size_t i = 0; i < tile.versions.size(); ++i, x += step) {
        dl->AddCircleFilled({x, p1.y - S(10)}, S(5.5f), Rgba(10, 12, 30, 210));
        dl->AddCircleFilled({x, p1.y - S(10)}, S(4),
                            i == tile.version ? kGold : Rgba(255, 255, 255, 120));
      }
    }
    if (const OwnedFigure* owned = library_.Owned(sky); owned && owned->stats) {
      const std::string lv = "Lv " + std::to_string(owned->stats->level);
      Pill(dl, {p1.x - S(4) - PillWidth(lv, 13), p0.y + S(4)}, lv, kGold, kInk, 13);
    }
    dl->PopClipRect();
    if (!focused && !chosen) dl->AddRectFilled(a, b, Rgba(8, 10, 26, 110), S(14));
    if (auto owner = TakenBy(sky)) DrawTaken(dl, a, b, *owner);
  }

  // The details view, for one figure at a time (a swapper's halves with LB / RB): level, hat and
  // the other facts on the left, the upgrades as the game's upgrade screen shows them on the right,
  // with the one the cursor is on described below them.
  void DrawDetails(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    const auto figures = PreviewFigures();
    detail_icons_.clear();
    if (figures.empty()) return;
    detail_half_ = std::min(detail_half_, figures.size() - 1);
    const SkylanderInfo* sky = figures[detail_half_];
    const OwnedFigure* owned = library_.Owned(sky);
    const portal::FigureStats* stats = owned && owned->stats && !IsItem(sky) ? &*owned->stats : nullptr;
    // Items and sidekicks keep nothing on the figure: a smaller panel with what they do. Traps
    // (their villain) and vehicles (their mods) do, and get the usual one.
    const bool simple = IsItem(sky) || (IsCompanion(sky) && GameCompanionKind() == CompanionKind::kSidekick);
    const auto upgrades = !simple ? UpgradesFor(*sky) : nullptr;

    const float avail_w = area1.x - area0.x - S(24), avail_h = area1.y - area0.y - S(24);
    ImVec2 a, b;
    DrawOverlayPanel(dl, area0, area1,
                     simple ? ImVec2{std::min(S(860), avail_w), std::min(S(330), avail_h)}
                            : ImVec2{std::min(S(1240), avail_w), std::min(S(800), avail_h)},
                     a, b);

    // Header: picture, name, version / element / game, the halves.
    const Element element = portal::FigureElement(*sky);
    DrawFigurePicture(dl, {a.x + S(26), a.y + S(22)}, {a.x + S(110), a.y + S(106)}, sky, S(14));
    OutlinedLabel(dl, TitleFont(), S(34), {a.x + S(126), a.y + S(26)}, kWhite, FigureName(*sky),
                  S(2));
    std::string line = IsAdventurePack(sky) ? AdventurePackName()
                       : IsItem(sky)          ? std::string(T(Text::kItems))
                                              : BadgeLabel(portal::VersionBadge(*sky)) + "   " +
                                           std::string(ElementLabel(element));
    line += "   " + std::string(sky->game);
    DrawElementBadge(dl, {a.x + S(140), a.y + S(88)}, S(14), element);
    Label(dl, BoldFont(), S(19), {a.x + S(162), a.y + S(77)}, kDim, line);
    // The trait (a swapper's from its bottom half) after the line, with its symbol.
    const SkylanderInfo* trait_sky = figures.empty() ? sky : figures.back();
    if (auto trait = TraitOf(trait_sky, OwnedStats(trait_sky))) {
      const float tx = a.x + S(162) + TextSize(BoldFont(), S(19), line).x + S(28);
      DrawTraitSymbol(dl, {tx, a.y + S(88)}, S(14), *trait);
      Label(dl, BoldFont(), S(19), {tx + S(20), a.y + S(77)}, kGold, trait->name);
    }
    if (figures.size() == 2) {
      float px = b.x - S(26);
      for (int i = 1; i >= 0; --i) {
        const char* text = T(i == 0 ? Text::kTop : Text::kBottom);
        const float w = PillWidth(text, 17);
        px -= w;
        Pill(dl, {px, a.y + S(30)}, text, size_t(i) == detail_half_ ? kGold : Rgba(255, 255, 255, 40),
             size_t(i) == detail_half_ ? kInk : kWhite, 17);
        px -= S(10);
      }
    }
    const float top = a.y + S(126);
    dl->AddLine({a.x + S(24), top - S(8)}, {b.x - S(24), top - S(8)}, Rgba(255, 255, 255, 30),
                S(1.5f));

    if (simple) {
      const float tx = a.x + S(28), tw = b.x - a.x - S(56);
      float ty = top + S(10);
      const char* info = T(IsAdventurePack(sky) ? Text::kAdventurePackInfo
                           : IsItem(sky)          ? Text::kItemInfo
                                                  : Text::kSidekickInfo);
      Label(dl, BoldFont(), S(20), {tx, ty}, kWhite, info, tw);
      ty += TextSize(BoldFont(), S(20), info, tw).y + S(14);
      if (const auto seconds = IsItem(sky) ? ItemDurationFor(*sky) : std::nullopt) {
        char text[64];
        std::snprintf(text, sizeof(text), T(Text::kItemDuration), *seconds);
        Label(dl, BoldFont(), S(20), {tx, ty}, kGold, text, tw);
      }
      if (owned) {
        Label(dl, BoldFont(), S(15), {tx, b.y - S(36)}, Rgba(160, 170, 205),
              std::string(T(Text::kFile)) + ": " + PathUtf8(owned->path.filename()), tw);
      }
      return;
    }

    // Left column.
    const float left_w = S(380);
    float y = top + S(6);
    const float lx = a.x + S(28);
    if (stats) {
      y = DrawLevelBar(dl, {lx, y}, left_w - S(20), *stats, 0.95f);
      if (stats->hat) y = DrawHat(dl, {lx, y}, left_w - S(20), stats->hat, false);
      for (const std::string& text : StatsLines(*stats)) {
        Label(dl, BoldFont(), S(17), {lx, y}, kDim, text, left_w - S(20));
        y += TextSize(BoldFont(), S(17), text, left_w - S(20)).y + S(8);
      }
    } else if (!IsItem(sky) && !IsCompanion(sky)) {
      Label(dl, BoldFont(), S(19), {lx, y}, kDim, T(owned ? Text::kNoProgress : Text::kNewFigure),
            left_w - S(20));
      y += S(80);
    }
    if (owned) {
      Label(dl, BoldFont(), S(15), {lx, b.y - S(36)}, Rgba(160, 170, 205),
            std::string(T(Text::kFile)) + ": " + PathUtf8(owned->path.filename()), left_w - S(10));
    }

    // Right column: the upgrade tree.
    const float rx = a.x + left_w + S(30), rw = b.x - S(28) - rx;
    dl->AddLine({rx - S(16), top}, {rx - S(16), b.y - S(24)}, Rgba(255, 255, 255, 26), S(1.5f));
    if (!upgrades) return;
    const float icon = S(62), gap = S(12);
    float ry = top + S(4);
    int path_index = 0;
    auto add_icon = [&](const FigureUpgradePath& path, int pi, const FigureUpgrade& u, ImVec2 at) {
      const bool owned_upgrade = stats ? UpgradeOwned(*stats, path, pi, u) : u.bit < 0;
      const bool focused = detail_icons_.size() == detail_focus_;
      DrawUpgradeIcon(dl, at, icon, u, owned_upgrade, focused);
      if (!owned_upgrade && u.cost) {
        // The price with a small coin before it, centred under the icon.
        const std::string cost = std::to_string(u.cost);
        const ImVec2 cs = TextSize(BoldFont(), S(14), cost);
        const float r = S(6), w = 2 * r + S(3) + cs.x;
        const float x0 = at.x + (icon - w) / 2;
        DrawGoldCoin(dl, {x0 + r, at.y + icon + S(2) + cs.y / 2}, r);
        Label(dl, BoldFont(), S(14), {x0 + 2 * r + S(3), at.y + icon + S(2)},
              Rgba(250, 205, 70), cost);
      }
      detail_icons_.push_back({&u, owned_upgrade, {at.x, at.y}, {at.x + icon, at.y + icon}});
    };
    // The starting attacks with the soul gem and Wow Pow beside them, the basic upgrades, the two
    // paths side by side.
    std::vector<std::pair<const FigureUpgradePath*, int>> paths, extras;
    const FigureUpgradePath* start = nullptr;
    const FigureUpgradePath* basic = nullptr;
    for (const FigureUpgradePath& path : upgrades->paths) {
      switch (path.kind) {
        case FigureUpgradePath::Kind::kStart: start = &path; break;
        case FigureUpgradePath::Kind::kBasic: basic = &path; break;
        case FigureUpgradePath::Kind::kPath: paths.emplace_back(&path, path_index++); break;
        default: extras.emplace_back(&path, 0); break;
      }
    }
    auto row = [&](const FigureUpgradePath& path, const std::string& title, float x, float width) {
      Label(dl, BoldFont(), S(17), {x, ry}, kGold, title, width);
      float ix = x;
      for (const FigureUpgrade& u : path.upgrades) {
        add_icon(path, 0, u, {ix, ry + S(24)});
        ix += icon + gap;
      }
      return ix;
    };
    if (start || !extras.empty()) {
      float x = rx;
      if (start) x = std::max(row(*start, T(Text::kStartingAttacks), x, S(300)), x + S(220)) + S(24);
      for (const auto& [path, pi] : extras) {
        x = std::max(row(*path, path->name, x, S(240)), x + S(200)) + S(24);
      }
      ry += S(24) + icon + S(22);
    }
    if (basic) {
      row(*basic, basic->name, rx, rw);
      ry += S(24) + icon + S(22);
    }
    if (!paths.empty()) {
      const float col_w = (rw - S(16)) / float(paths.size());
      const int chosen = stats ? portal::UpgradePath(*stats) : 0;
      for (size_t i = 0; i < paths.size(); ++i) {
        const auto& [path, pi] = paths[i];
        const float x0 = rx + i * (col_w + S(16));
        const bool is_chosen = chosen == pi + 1;
        const ImVec2 box0{x0 - S(8), ry - S(6)}, box1{x0 + col_w - S(4), ry + S(28) + icon + S(24)};
        dl->AddRectFilled(box0, box1, is_chosen ? Rgba(244, 196, 84, 38) : Rgba(255, 255, 255, 10),
                          S(12));
        if (is_chosen) dl->AddRect(box0, box1, Rgba(244, 196, 84, 170), S(12), 0, S(2));
        Label(dl, BoldFont(), S(17), {x0, ry}, is_chosen || chosen == 0 ? kGold : kDim, path->name,
              col_w - S(16));
        float x = x0;
        for (const FigureUpgrade& u : path->upgrades) {
          add_icon(*path, pi, u, {x, ry + S(28)});
          x += icon + gap;
        }
      }
      ry += S(28) + icon + S(32);
    }
    // The upgrade under the cursor.
    if (detail_icons_.empty()) return;
    detail_focus_ = std::min(detail_focus_, detail_icons_.size() - 1);
    const DetailIcon& focus = detail_icons_[detail_focus_];
    const float box_top = std::max(ry, b.y - S(140));
    dl->AddRectFilled({rx - S(6), box_top}, {b.x - S(24), b.y - S(22)}, Rgba(8, 10, 30, 170), S(12));
    Label(dl, BoldFont(), S(21), {rx + S(8), box_top + S(10)}, kWhite, focus.upgrade->name);
    char status[96];
    if (focus.owned) {
      std::snprintf(status, sizeof(status), "%s", focus.upgrade->bit < 0 ? "" : T(Text::kBought));
    } else {
      std::snprintf(status, sizeof(status), T(Text::kPrice), unsigned(focus.upgrade->cost));
    }
    const ImVec2 st = TextSize(BoldFont(), S(17), status);
    Label(dl, BoldFont(), S(17), {b.x - S(40) - st.x, box_top + S(13)},
          focus.owned ? Rgba(120, 220, 130) : Rgba(250, 205, 70), status);
    Label(dl, BoldFont(), S(17), {rx + S(8), box_top + S(42)}, kDim, focus.upgrade->description,
          b.x - S(48) - rx);
  }

  // Details view: the cursor moves to the nearest icon in that direction.
  void MoveDetailFocus(int dx, int dy) {
    if (detail_icons_.empty()) return;
    const DetailIcon& from = detail_icons_[std::min(detail_focus_, detail_icons_.size() - 1)];
    const ImVec2 c{(from.a.x + from.b.x) / 2, (from.a.y + from.b.y) / 2};
    size_t best = detail_focus_;
    float best_score = FLT_MAX;
    for (size_t i = 0; i < detail_icons_.size(); ++i) {
      const DetailIcon& to = detail_icons_[i];
      const ImVec2 t{(to.a.x + to.b.x) / 2, (to.a.y + to.b.y) / 2};
      const float ddx = t.x - c.x, ddy = t.y - c.y;
      if (dx && (ddx * dx <= S(4) || std::abs(ddy) > S(40))) continue;
      if (dy && ddy * dy <= S(4)) continue;
      const float score = dx ? std::abs(ddx) + std::abs(ddy) * 4 : std::abs(ddy) + std::abs(ddx) * 0.6f;
      if (score < best_score) {
        best_score = score;
        best = i;
      }
    }
    detail_focus_ = best;
  }

  std::function<void()> on_done_;
  DeviceId device_;
  uint32_t user_;
  bool keyboard_;
  bool auto_opened_;
  bool keyboard_controls_ = false;
  bool software_ = false;
  bool revealed_ = true;
  std::optional<Clock::time_point> reveal_at_;  // RevealSoon
  Clock::time_point reveal_time_ = Clock::now();
  bool leave_ = false;
  size_t option_ = 0;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  std::optional<ImVec2> mouse_start_;
  bool mouse_moved_ = false;
  State state_ = State::kBrowsing;
  State previous_state_ = State::kBrowsing;
  Library library_;
  std::vector<LiftedFigure> lifted_;
  std::unordered_map<std::string, uint32_t> taken_;  // character -> player who has it
  const SkylanderInfo* current_item_ = nullptr;
  Clock::time_point lift_time_ = Clock::now();
  std::vector<std::filesystem::path> pending_paths_;
  bool restore_ = false;
  bool item_change_ = false;
  std::optional<std::filesystem::path> item_path_;
  const SkylanderInfo* current_sidekick_ = nullptr;  // the sidekick on the portal
  // Recent's first cards: the item and the sidekick on the portal (character indices).
  std::vector<size_t> extras_;
  // Details view: the half shown, the icons drawn last frame and the one under the cursor.
  struct DetailIcon {
    const FigureUpgrade* upgrade = nullptr;
    bool owned = false;
    ImVec2 a, b;
  };
  size_t detail_half_ = 0;
  size_t detail_focus_ = 0;
  std::vector<DetailIcon> detail_icons_;
  bool sidekick_change_ = false;
  std::optional<std::filesystem::path> sidekick_path_;
  size_t tab_ = 0;
  size_t element_ = 0;  // row of the current tab
  size_t selected_ = 0;
  size_t scroll_row_ = 0;
  size_t columns_ = 1;
  std::vector<Choice> choices_;
  size_t choice_ = 0;
  size_t choice_scroll_ = 0;
  BuildHalf build_[2];  // the top grid, the bottom grid
  int build_focus_ = 0;
  std::optional<RecentEntry> build_keep_;  // the recent entry the builder changes
  std::unordered_map<uint64_t, PadReader> pad_readers_;  // by device
};

// UI thread only.
std::unique_ptr<PortalMenuDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

// A short message at the top of the screen, fading out after two seconds.
class Toast final : public rex::ui::ImGuiDialog {
 public:
  Toast(rex::ui::ImGuiDrawer* drawer, std::string text, std::function<void()> on_done)
      : ImGuiDialog(drawer), text_(std::move(text)), on_done_(std::move(on_done)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const float t = std::chrono::duration<float>(Clock::now() - start_).count();
    if (t > 2.4f) {
      if (on_done_) on_done_();
      on_done_ = nullptr;
      return;
    }
    SetScale(io.DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##portal_toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first = dl->VtxBuffer.Size;
    const ImVec2 ts = TextSize(BoldFont(), S(26), text_);
    const ImVec2 a{(io.DisplaySize.x - ts.x) / 2 - S(30), S(60)};
    const ImVec2 b{a.x + ts.x + S(60), a.y + ts.y + S(24)};
    dl->AddRectFilled(a, b, Rgba(16, 22, 60, 230), S(22));
    dl->AddRect(a, b, kGold, S(22), 0, S(3));
    Label(dl, BoldFont(), S(26), {a.x + S(30), a.y + S(12)}, kWhite, text_);
    FadeVertices(dl, first, std::min(t / 0.15f, (2.4f - t) / 0.4f));
    ImGui::End();
  }

 private:
  std::string text_;
  std::function<void()> on_done_;
  Clock::time_point start_ = Clock::now();
};

void OpenMenu(DeviceId device, uint32_t user, bool keyboard, bool auto_opened = false) {
  if (g_menu || !g_drawer) return;
  g_menu = std::make_unique<PortalMenuDialog>(
      g_drawer,
      [] {
        // The dialog finishes inside its own OnDraw; destroy it afterwards.
        g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
      },
      device, user, keyboard, auto_opened);
}

std::unique_ptr<Toast> g_toast;  // UI thread only

// --- Portal check -------------------------------------------------------------------------------

// The game's EMagicMomentReason values the check tells apart.
constexpr uint32_t kReasonTooManyCharacters = 1;
constexpr uint32_t kReasonTooManyItems = 2;

// What is on the emulated portal: one entry per player's Skylander (both halves of a swapper),
// then every other figure (the item, the sidekick, figures placed some other way).
struct PortalEntry {
  enum class Kind { kSkylander, kItem, kSidekick } kind = Kind::kSkylander;
  std::vector<int> slots;
  std::vector<const SkylanderInfo*> figures;  // null for a figure the catalog doesn't know
  std::optional<uint32_t> player;              // whose Skylander
  bool remove = false;
};

std::vector<PortalEntry> PortalEntries() {
  std::vector<PortalEntry> entries;
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return entries;
  std::vector<bool> seen(portal::kMaxFigures, false);
  for (uint32_t u = 0; u < g_player_slots.size(); ++u) {
    PortalEntry entry;
    entry.player = u;
    for (int slot : g_player_slots[u]) {
      if (slot < 0 || slot >= portal::kMaxFigures || seen[slot] || !software->HasFigure(slot)) {
        continue;
      }
      entry.slots.push_back(slot);
      entry.figures.push_back(FigureIn(slot));
      seen[slot] = true;
    }
    if (!entry.slots.empty()) entries.push_back(std::move(entry));
  }
  for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
    if (seen[slot] || !software->HasFigure(slot)) continue;
    PortalEntry entry;
    const SkylanderInfo* sky = FigureIn(slot);
    if (IsItem(sky) || IsItemSlot(slot)) {
      entry.kind = PortalEntry::Kind::kItem;
    } else if (IsCompanion(sky) || IsSidekickSlot(slot)) {
      entry.kind = PortalEntry::Kind::kSidekick;
    }
    entry.slots.push_back(slot);
    entry.figures.push_back(sky);
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Opens when the game shows its portal error and stays until the player continues or closes it:
// everything on the portal, what there is too much of marked, A marks a figure to take off,
// Continue takes the marked ones off.
class PortalCheckDialog final : public rex::ui::ImGuiDialog {
 public:
  PortalCheckDialog(rex::ui::ImGuiDrawer* drawer, uint32_t reason, std::function<void()> on_done)
      : ImGuiDialog(drawer), reason_(reason), on_done_(std::move(on_done)) {
    g_german = menu_draw::GermanText();
    menu_input::SetMenuOpen(true);
    menu_input::SetMenuCloser([this] { Close(); });
    entries_ = PortalEntries();
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (TooMany(entries_[i])) {
        cursor_ = i;
        break;
      }
    }
    std::string list;
    for (const PortalEntry& e : entries_) list += (list.empty() ? "" : ", ") + NameOf(e);
    REXLOG_INFO("Portal check: the game reports a problem with the figures (reason {}); on the "
                "portal: {}",
                reason_, list);
  }

  ~PortalCheckDialog() override {
    if (!done_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    menu_input::SetMenuOpen(false);
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    SetScale(io.DisplaySize.y / 1080.0f);
    HandleInput(ReadActions());
    if (done_) return;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##portal_check", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    Draw(dl, io.DisplaySize);
    FadeVertices(dl, first_vertex,
                 std::chrono::duration<float>(Clock::now() - open_time_).count() / 0.15f);
    ImGui::End();
  }

 private:
  size_t Count(PortalEntry::Kind kind) const {
    return size_t(std::count_if(entries_.begin(), entries_.end(),
                                [&](const PortalEntry& e) { return e.kind == kind; }));
  }

  // One sidekick and one item at a time; a Skylander nobody plays, and for "too many Skylanders"
  // every one, is for the player to look at.
  bool TooMany(const PortalEntry& e) const {
    switch (e.kind) {
      case PortalEntry::Kind::kItem: return Count(PortalEntry::Kind::kItem) > 1;
      case PortalEntry::Kind::kSidekick: return Count(PortalEntry::Kind::kSidekick) > 1;
      default: return !e.player || reason_ == kReasonTooManyCharacters;
    }
  }

  static std::string NameOf(const PortalEntry& e) {
    std::vector<const SkylanderInfo*> known;
    for (const SkylanderInfo* sky : e.figures) {
      if (sky) known.push_back(sky);
    }
    return known.empty() ? std::string("?") : ComboName(known);
  }

  std::string KindOf(const PortalEntry& e) const {
    char text[64];
    std::string kind;
    switch (e.kind) {
      case PortalEntry::Kind::kItem:
        kind = !e.figures.empty() && IsAdventurePack(e.figures.front()) ? AdventurePackName()
                                                                        : T(Text::kCheckItem);
        break;
      case PortalEntry::Kind::kSidekick: kind = T(Text::kCheckSidekick); break;
      default:
        if (!e.player) {
          kind = T(Text::kCheckStraySkylander);
        } else {
          std::snprintf(text, sizeof(text), T(Text::kCheckPlayerSkylander), *e.player + 1);
          kind = text;
        }
        break;
    }
    if (!e.slots.empty() && IsRealPortalCopy(e.slots[0])) {
      kind += std::string(" \xC2\xB7 ") + T(Text::kCheckRealPortal);
    }
    return kind;
  }

  Actions ReadActions() {
    Actions actions;
    bool released = true;
    for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
      if (!device.pollable) continue;
      const auto pad = menu_input::ReadPad(device.id);
      if (!pad) continue;
      PadReader& reader = pads_[uint64_t(device.id)];
      released = released && reader.Released(*pad);
      const Actions read = reader.Read(*pad);
      if (!waiting_for_release_) Merge(actions, read);
    }
    if (waiting_for_release_) {
      if (released) waiting_for_release_ = false;
      return {};
    }
    Merge(actions, ReadKeyboard());
    return actions;
  }

  void HandleInput(const Actions& a) {
    const size_t rows = entries_.size() + 1;  // the figures, then Continue
    if (a.up) cursor_ = (cursor_ + rows - 1) % rows;
    if (a.down) cursor_ = (cursor_ + 1) % rows;
    if (a.back) {
      REXLOG_INFO("Portal check: closed without changes");
      Close();
      return;
    }
    if (a.accept) Choose(cursor_);
  }

  void Choose(size_t row) {
    if (row < entries_.size()) {
      entries_[row].remove = !entries_[row].remove;
      return;
    }
    // Continue: the marked figures leave the portal, so the game sees them go.
    for (const PortalEntry& e : entries_) {
      if (!e.remove) continue;
      for (int slot : e.slots) {
        RemoveFigureFromSlot(slot);
        if (slot == g_item_slot) g_item_slot = -1;
        if (slot == g_sidekick_slot) g_sidekick_slot = -1;
        for (int& vehicle : g_vehicle_slots) {
          if (slot == vehicle) vehicle = -1;
        }
        for (auto& slots : g_player_slots) {
          slots.erase(std::remove(slots.begin(), slots.end(), slot), slots.end());
        }
      }
      REXLOG_INFO("Portal check: took off {}", NameOf(e));
    }
    Close();
  }

  // An adventure pack among the items: the game counts it with them ("Zu viele Spielzeuge auf
  // dem Portal of Power" in Spyro's Adventure for a magic item and a location piece).
  bool PackAmongItems() const {
    return std::any_of(entries_.begin(), entries_.end(), [](const PortalEntry& e) {
      return e.kind == PortalEntry::Kind::kItem && !e.figures.empty() &&
             IsAdventurePack(e.figures.front());
    });
  }

  // The title for "too many items" names what there is too much of, so a game without sidekicks
  // (Spyro's Adventure) never mentions them; both only when both are doubled, or when the game
  // complains and a sidekick is on the portal but nothing looks doubled here.
  const char* ItemsTitle() const {
    const size_t items = Count(PortalEntry::Kind::kItem);
    const size_t sidekicks = Count(PortalEntry::Kind::kSidekick);
    if (sidekicks > 1 && items <= 1) return T(Text::kCheckTitleSidekicks);
    if (sidekicks <= 1 && (items > 1 || sidekicks == 0)) {
      return T(items > 1 && PackAmongItems() ? Text::kCheckTitleToys : Text::kCheckTitleMagicItems);
    }
    return T(Text::kCheckTitleItems);
  }

  void Draw(ImDrawList* dl, ImVec2 size) {
    dl->AddRectFilled({0, 0}, size, Rgba(6, 10, 30, 150));
    const float row_h = S(84), gap = S(10);
    std::vector<std::string> notes;
    char text[96];
    if (Count(PortalEntry::Kind::kSidekick) > 1) {
      std::snprintf(text, sizeof(text), T(Text::kCheckSidekicks),
                    unsigned(Count(PortalEntry::Kind::kSidekick)));
      notes.push_back(text);
    }
    if (Count(PortalEntry::Kind::kItem) > 1) {
      std::snprintf(text, sizeof(text), T(PackAmongItems() ? Text::kCheckToys : Text::kCheckItems),
                    unsigned(Count(PortalEntry::Kind::kItem)));
      notes.push_back(text);
    }
    if (reason_ == kReasonTooManyCharacters) {
      std::snprintf(text, sizeof(text), T(Text::kCheckSkylanders),
                    unsigned(Count(PortalEntry::Kind::kSkylander)));
      notes.push_back(text);
    }
    const float w = std::min(S(1000), size.x - S(80));
    const float list_top = S(150) + float(notes.size()) * S(32);
    const float h = std::min(size.y - S(60), list_top + float(entries_.size() + 1) * (row_h + gap) +
                                                  S(96));
    const ImVec2 a{(size.x - w) / 2, (size.y - h) / 2}, b{a.x + w, a.y + h};
    DrawPanel(dl, a, b);
    const char* title = reason_ == kReasonTooManyCharacters ? T(Text::kCheckTitleSkylanders)
                        : reason_ == kReasonTooManyItems    ? ItemsTitle()
                                                            : T(Text::kCheckTitleOther);
    OutlinedLabel(dl, TitleFont(), S(36), {a.x + S(36), a.y + S(26)}, kGold, title, S(2));
    Label(dl, BoldFont(), S(21), {a.x + S(36), a.y + S(86)}, kDim, T(Text::kCheckHint), w - S(72));
    float y = a.y + S(124);
    for (const std::string& note : notes) {
      Label(dl, BoldFont(), S(21), {a.x + S(36), y}, Rgba(255, 150, 70), note, w - S(72));
      y += S(32);
    }
    y = a.y + list_top;
    for (size_t i = 0; i <= entries_.size(); ++i) {
      const ImVec2 r0{a.x + S(36), y}, r1{b.x - S(36), y + row_h};
      if (r1.y > b.y - S(70)) break;
      const bool active = i == cursor_;
      if (ImGui::IsMouseHoveringRect(r0, r1)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) cursor_ = i;
        if (ImGui::IsMouseClicked(0)) {
          Choose(i);
          if (done_) return;
        }
      }
      if (i == entries_.size()) {
        // Continue.
        dl->AddRectFilled(r0, r1, active ? kGold : Rgba(255, 255, 255, 26), S(16));
        const char* go = T(Text::kCheckContinue);
        const ImVec2 ts = TextSize(TitleFont(), S(28), go);
        Label(dl, TitleFont(), S(28), {(r0.x + r1.x - ts.x) / 2, (r0.y + r1.y - ts.y) / 2},
              active ? kInk : kWhite, go);
        break;
      }
      const PortalEntry& e = entries_[i];
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      const float pic = row_h - S(16);
      DrawFigurePicture(dl, {r0.x + S(10), r0.y + S(8)}, {r0.x + S(10) + pic, r0.y + S(8) + pic},
                        e.figures.empty() ? nullptr : e.figures[0], S(10));
      const float tx = r0.x + S(24) + pic;
      Label(dl, BoldFont(), S(24), {tx, r0.y + S(12)}, kWhite, NameOf(e));
      Label(dl, BoldFont(), S(18), {tx, r0.y + S(46)}, kDim, KindOf(e));
      float px = r1.x - S(16);
      const char* state = e.remove ? T(Text::kCheckOff) : T(Text::kCheckStays);
      px -= PillWidth(state, 17);
      Pill(dl, {px, r0.y + S(26)}, state, e.remove ? kBackRed : Rgba(255, 255, 255, 40), kWhite, 17);
      if (TooMany(e)) {
        const char* warn = T(Text::kCheckTooMany);
        px -= PillWidth(warn, 17) + S(10);
        Pill(dl, {px, r0.y + S(26)}, warn, Rgba(230, 120, 40), kWhite, 17);
      }
      y += row_h + gap;
    }
    float x = a.x + S(36);
    const float fy = b.y - S(56);
    const bool on_continue = cursor_ >= entries_.size();
    x += Hint(dl, {x, fy}, "A", kAcceptGreen,
              on_continue ? T(Text::kCheckContinue) : T(Text::kCheckToggle), true, "Enter");
    Hint(dl, {x, fy}, "B", kBackRed, T(Text::kClose), true, "Esc");
  }

  uint32_t reason_;
  std::function<void()> on_done_;
  std::vector<PortalEntry> entries_;
  size_t cursor_ = 0;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadReader> pads_;  // by device
};

std::unique_ptr<PortalCheckDialog> g_check;  // UI thread only

void ShowToast(std::string text) {
  if (!g_drawer || !g_app_context) return;
  g_toast = std::make_unique<Toast>(g_drawer, std::move(text), [] {
    g_app_context->CallInUIThreadDeferred([] { g_toast.reset(); });
  });
}

// portal_item_off_buttons / bind_portal_item_off: takes the item off without opening the menu and
// without lifting anyone's Skylander. With the menu open, its own Y does that.
void TakeItemOff() {
  if (g_menu || !PortalItem()) return;
  g_german = menu_draw::GermanText();
  REXLOG_INFO("Portal menu: item taken off with the item button");
  SetPortalItem(0, std::nullopt);
  ShowToast(T(Text::kItemTakenOff));
}

}  // namespace

bool CaptureGamePicture(int& width, int& height, std::vector<uint8_t>& rgba) {
  rex::ui::Presenter* presenter = g_immediate_drawer ? g_immediate_drawer->presenter() : nullptr;
  rex::ui::RawImage image;
  if (!presenter || !presenter->CaptureGuestOutput(image) || !image.width || !image.height) return false;
  width = int(image.width);
  height = int(image.height);
  rgba.resize(size_t(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    const uint8_t* src = image.data.data() + size_t(y) * image.stride;
    uint8_t* dst = rgba.data() + size_t(y) * width * 4;
    for (int x = 0; x < width; ++x) {
      dst[x * 4 + 0] = src[x * 4 + 0];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4 + 2];
      dst[x * 4 + 3] = 255;  // R8 G8 B8 X8
    }
  }
  return true;
}

std::string PortalFiguresText() {
  std::string text;
  auto add = [&](const std::string& part) { text += (text.empty() ? "" : ", ") + part; };
  portal::SoftwarePortal* software = GetSoftwarePortal();
  for (int slot = 0; software && slot < portal::kMaxFigures; ++slot) {
    const SkylanderInfo* sky = software->HasFigure(slot) ? FigureIn(slot) : nullptr;
    if (!sky) continue;
    // Whose it is when the menu put it there (figures put on some other way have no player).
    std::string who;
    for (uint32_t user = 0; user < g_player_slots.size(); ++user) {
      const auto& slots = g_player_slots[user];
      if (std::find(slots.begin(), slots.end(), slot) != slots.end()) who = " (player " + std::to_string(user + 1) + ")";
    }
    if (IsItemSlot(slot)) who = " (item)";
    add(FigureName(*sky) + who);
  }
  return text;
}

void ShowPortalToast(std::string text) { ShowToast(std::move(text)); }

ImTextureID menu_draw::GameArtTexture(const std::string& key) { return ArtTexture(key); }

void AutoOpenPortalMenu(uint32_t user) {
  if (!g_app_context || user >= rex::input::kMaxGuestUsers) return;
  g_app_context->CallInUIThread([user] {
    // Opened with the button for this player and waiting for the game's portal screen: here it is.
    if (g_menu) {
      if (g_menu->user() == user) g_menu->Reveal();
      return;
    }
    if (!REXCVAR_GET(portal_menu_auto_open) || !GetSoftwarePortal()) return;
    // The player's controller steers it; without one, the keyboard.
    const auto device = menu_input::DeviceForUser(user);
    OpenMenu(device ? device->id : DeviceId::kInvalid, user, !device || device->keyboard, true);
  });
}

void AutoOpenPortalMenuForPlayerWithoutFigure() {
  if (!g_app_context) return;
  g_app_context->CallInUIThread([] {
    portal::SoftwarePortal* software = GetSoftwarePortal();
    if (!software) return;
    uint32_t user = 0;
    while (user + 1 < g_player_slots.size() &&
           std::any_of(g_player_slots[user].begin(), g_player_slots[user].end(),
                       [&](int slot) { return software->HasFigure(slot); })) {
      ++user;
    }
    AutoOpenPortalMenu(user);
  });
}

namespace {

// hybrid: a figure from the real portal arrived in `slot`. A Skylander belongs to the player whose
// menu is open (the game waits for that player's figure, and the menu closes), else to the player
// with the other half for a swapper half, else to the first player without a figure. Items and
// sidekicks belong to no one, as from the menu.
void RealFigureArrived(int slot) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  const SkylanderInfo* sky = FigureIn(slot);
  if (!software || !sky || IsItem(sky) || IsCompanion(sky)) return;
  auto has_figure = [&](uint32_t u) {
    return std::any_of(g_player_slots[u].begin(), g_player_slots[u].end(),
                       [&](int s) { return s != slot && software->HasFigure(s); });
  };
  std::optional<uint32_t> owner;
  if (g_menu) {
    owner = g_menu->user();
    g_menu->RealFigurePlaced();
  }
  const SwapperHalf half = portal::GetSwapperHalf(*sky);
  for (uint32_t u = 0; !owner && half != SwapperHalf::kNone && u < g_player_slots.size(); ++u) {
    for (int s : g_player_slots[u]) {
      const SkylanderInfo* other = s != slot && software->HasFigure(s) ? FigureIn(s) : nullptr;
      const SwapperHalf other_half = other ? portal::GetSwapperHalf(*other) : SwapperHalf::kNone;
      if (other_half != SwapperHalf::kNone && other_half != half) owner = u;
    }
  }
  for (uint32_t u = 0; !owner && u < g_player_slots.size(); ++u) {
    if (!has_figure(u)) owner = u;
  }
  for (auto& slots : g_player_slots) slots.erase(std::remove(slots.begin(), slots.end(), slot), slots.end());
  if (!owner) return;
  g_player_slots[*owner].push_back(slot);
  REXLOG_INFO("Portal menu: {} from the real portal is player {}'s", sky->name, *owner + 1);
}

}  // namespace

void ShowPortalCheck(uint32_t reason) {
  if (!g_app_context) return;
  g_app_context->CallInUIThread([reason] {
    if (g_check || g_menu || !g_drawer || menu_input::MenuOpen() || !GetSoftwarePortal()) return;
    g_check = std::make_unique<PortalCheckDialog>(g_drawer, reason, [] {
      g_app_context->CallInUIThreadDeferred([] { g_check.reset(); });
    });
  });
}

void ClosePortalCheck() {
  if (!g_app_context) return;
  g_app_context->CallInUIThread([] {
    if (g_check) g_check->Close();
  });
}

void CloseAutoOpenedPortalMenu() {
  if (!g_app_context) return;
  g_app_context->CallInUIThread([] {
    if (g_menu && g_menu->auto_opened()) g_menu->Cancel();
  });
}

void SetPortalMenuConfigPath(std::filesystem::path config_path) {
  g_config_path = std::move(config_path);
}

void PortalScreenComing(uint32_t user) {
  if (!g_app_context) return;
  g_app_context->CallInUIThread([user] {
    if (g_menu && g_menu->user() == user) g_menu->RevealSoon();
  });
}

// --- The figure picture cache for the launcher (StartFigurePictureCache) -------------------------

std::atomic<bool> g_picture_cache_stop{false};

void ShutdownPortalMenu() {
  g_picture_cache_stop = true;
  StopItemThread();
  g_menu.reset();
  g_check.reset();
  g_toast.reset();
  g_textures.clear();
  g_art_textures.clear();
  g_immediate_drawer = nullptr;
}

void SetFigureImageProvider(FigureImageProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_image_provider = std::move(provider);
}

std::filesystem::path SharedPicturesDir(const std::filesystem::path& user_data_root, std::string_view game) {
  return SharedBase(user_data_root) / "Pictures" / std::filesystem::path(std::u8string(game.begin(), game.end()));
}

// One line of the upgrade cache's text: no tabs or line breaks inside a field.
std::string CacheField(std::string text) {
  for (char& c : text) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return text;
}

// The upgrade trees of the owner's figures for the launcher (StartFigurePictureCache), on the cache's
// thread: the figures in the folder next to Pictures, each figure's tree asked for on the UI thread
// until it comes (the games read some on demand) or ten quiet rounds pass.
void WriteUpgradeCache(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  using namespace std::chrono_literals;
  std::error_code ec;
  std::set<std::pair<uint16_t, uint16_t>> pending;
  const fs::path figures = dir.parent_path().parent_path() / "Figures";
  for (fs::recursive_directory_iterator it(figures, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    const auto data = portal::LoadFigureFile(it->path());
    if (!data) continue;
    const uint16_t id = portal::ReadFigureId(*data), variant = portal::ReadFigureVariant(*data);
    char name[48];
    std::snprintf(name, sizeof(name), "upgrades-%u-%u.txt", unsigned(id), unsigned(variant));
    if (!fs::exists(dir / name, ec)) pending.insert({id, variant});
  }
  size_t written = 0;
  int quiet = 0;
  // A tree whose pictures are still loading (the games read them on demand once asked) waits a few
  // rounds for them.
  std::map<std::pair<uint16_t, uint16_t>, int> waited;
  for (int round = 0; round < 40 && quiet < 10 && !pending.empty() && !g_picture_cache_stop; ++round) {
    struct Tree {
      uint16_t id, variant;
      std::shared_ptr<const FigureUpgrades> upgrades;
      std::vector<std::shared_ptr<const FigureImage>> images;  // per upgrade, in order
      bool complete = true;  // every upgrade with a picture key has its picture
    };
    auto found = std::make_shared<std::promise<std::vector<Tree>>>();
    auto result = found->get_future();
    auto batch = std::make_shared<std::vector<std::pair<uint16_t, uint16_t>>>(pending.begin(), pending.end());
    if (!g_app_context) return;
    g_app_context->CallInUIThread([found, batch] {
      FigureUpgradesProvider upgrades_provider;
      ArtImageProvider art_provider;
      {
        std::lock_guard lock(g_provider_mutex);
        upgrades_provider = g_upgrades_provider;
        art_provider = g_art_provider;
      }
      std::vector<Tree> out;
      for (const auto& [id, variant] : *batch) {
        auto upgrades = upgrades_provider ? upgrades_provider(id, variant) : nullptr;
        if (!upgrades) continue;
        Tree tree{id, variant, upgrades, {}, true};
        for (const auto& path : upgrades->paths) {
          for (const auto& u : path.upgrades) {
            tree.images.push_back(art_provider && !u.image.empty() ? art_provider(u.image) : nullptr);
            if (!u.image.empty() && !tree.images.back()) tree.complete = false;
          }
        }
        out.push_back(std::move(tree));
      }
      found->set_value(std::move(out));
    });
    if (result.wait_for(10s) != std::future_status::ready) continue;
    const auto trees = result.get();
    quiet = trees.empty() ? quiet + 1 : 0;
    for (const Tree& tree : trees) {
      if (!tree.complete && ++waited[{tree.id, tree.variant}] < 6) continue;
      char prefix[48];
      std::snprintf(prefix, sizeof(prefix), "%u-%u", unsigned(tree.id), unsigned(tree.variant));
      std::string text;
      size_t n = 0;
      int path_number = 0;
      for (const auto& path : tree.upgrades->paths) {
        const bool is_path = path.kind == FigureUpgradePath::Kind::kPath;
        text += "P\t" + std::to_string(int(path.kind)) + "\t" + std::to_string(is_path ? path_number++ : -1) + "\t" +
                CacheField(path.name) + "\t" + CacheField(path.description) + "\n";
        for (const auto& u : path.upgrades) {
          std::string picture;
          const auto& image = tree.images[n];
          if (image && image->width > 0 && image->height > 0 &&
              image->rgba.size() >= size_t(image->width) * image->height * 4) {
            picture = "up-" + std::string(prefix) + "-" + std::to_string(n) + ".png";
            if (!WritePng(dir / picture, image->width, image->height, image->rgba.data())) picture.clear();
          }
          text += "U\t" + CacheField(u.name) + "\t" + std::to_string(u.cost) + "\t" + std::to_string(u.bit) + "\t" +
                  std::to_string(u.alternate_bit) + "\t" + picture + "\t" + CacheField(u.description) + "\n";
          ++n;
        }
      }
      std::ofstream(dir / ("upgrades-" + std::string(prefix) + ".txt"), std::ios::binary) << text;
      pending.erase({tree.id, tree.variant});
      ++written;
    }
    for (int i = 0; i < 4 && !g_picture_cache_stop; ++i) std::this_thread::sleep_for(500ms);
  }
  if (written) REXLOG_INFO("Portal menu: the upgrades of {} figures for the launcher", written);
}

// The picture folder StartFigurePictureCache was given, for OtherGamePicture. UI thread.
std::filesystem::path g_own_pictures_dir;
std::map<uint32_t, std::shared_ptr<const FigureImage>> g_other_pictures;  // also the misses

std::shared_ptr<const FigureImage> OtherGamePicture(uint16_t id, uint16_t variant) {
  if (g_own_pictures_dir.empty()) return nullptr;
  const uint32_t key = uint32_t(id) << 16 | variant;
  if (auto it = g_other_pictures.find(key); it != g_other_pictures.end()) return it->second;
  // The games' picture folders, the newest game first: it knows the most figures.
  static constexpr std::string_view kGames[] = {"Imaginators", "SuperChargers", "Trap Team",
                                                "Swap Force", "Giants", "Spyro's Adventure"};
  char name[32];
  std::snprintf(name, sizeof(name), "%u-%u.png", unsigned(id), unsigned(variant));
  std::shared_ptr<const FigureImage> found;
  for (const std::string_view game : kGames) {
    const std::filesystem::path dir =
        g_own_pictures_dir.parent_path() / std::filesystem::path(std::u8string(game.begin(), game.end()));
    if (dir == g_own_pictures_dir) continue;
    std::error_code ec;
    if (!std::filesystem::exists(dir / name, ec)) continue;
    auto image = std::make_shared<FigureImage>();
    if (ReadPng(dir / name, image->width, image->height, image->rgba)) {
      found = image;
      break;
    }
  }
  g_other_pictures.emplace(key, found);
  return found;
}

namespace {
void RunFigurePictureCache(std::filesystem::path dir);

// --export_pictures: once the cache has run, the achievements' list is waited for (at most 20 s)
// and the game quits; nothing of it is needed any more (a hidden, muted run of the launcher's).
void FinishPictureExport(const std::filesystem::path& dir) {
  using namespace std::chrono_literals;
  std::error_code ec;
  for (int i = 0; i < 40 && !std::filesystem::exists(dir / "achievements.txt", ec); ++i) {
    std::this_thread::sleep_for(500ms);
  }
  size_t pictures = 0;
  for (std::filesystem::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
    pictures += it->path().extension() == ".png" ? 1 : 0;
  }
  REXLOG_INFO("Pictures exported: {} PNG files in {}", pictures, rex::cvar::GetFlagByName("export_pictures"));
  rex::FlushLogging();
  std::_Exit(0);
}
}  // namespace

void StartFigurePictureCache(std::filesystem::path dir) {
  // --export_pictures: into that folder, then quit (with a limit, should the game never get there).
  const std::string export_dir = REXCVAR_GET(export_pictures);
  if (!export_dir.empty()) {
    dir = std::filesystem::path(std::u8string(export_dir.begin(), export_dir.end()));
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(180));
      REXLOG_WARN("Pictures export: not done after 180 s, quitting");
      rex::FlushLogging();
      std::_Exit(3);
    }).detach();
  }
  g_own_pictures_dir = dir;
  // The achievements go into the same folder (achievements_ui.h).
  StartAchievementCache(dir);
  std::thread([dir] {
    RunFigurePictureCache(dir);
    if (!REXCVAR_GET(export_pictures).empty()) FinishPictureExport(dir);
  }).detach();
}

namespace {

// The cache's work, on its own thread: the figures' pictures, the hats, the upgrade trees.
void RunFigurePictureCache(std::filesystem::path dir) {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    auto file_of = [&](const SkylanderInfo& sky) {
      char name[32];
      std::snprintf(name, sizeof(name), "%u-%u.png", unsigned(sky.id), unsigned(sky.variant));
      return dir / name;
    };
    std::vector<const SkylanderInfo*> pending;
    for (const SkylanderInfo& sky : portal::AllSkylanders()) {
      if (!fs::exists(file_of(sky), ec)) pending.push_back(&sky);
    }
    // The game loads its pictures in the background first.
    for (int i = 0; i < 20 && !g_picture_cache_stop; ++i) std::this_thread::sleep_for(500ms);
    size_t written = 0;
    // Figures the game doesn't know never come: done once five rounds in a row add none after the
    // first pictures came (the game may still be loading before that, up to the 60 rounds).
    int figure_quiet = 0;
    for (int round = 0; round < 60 && figure_quiet < 5 && !pending.empty() && !g_picture_cache_stop; ++round) {
      using Found = std::vector<std::pair<const SkylanderInfo*, std::shared_ptr<const FigureImage>>>;
      auto found = std::make_shared<std::promise<Found>>();
      auto result = found->get_future();
      auto batch = std::make_shared<std::vector<const SkylanderInfo*>>(pending);
      if (!g_app_context) break;
      // The provider is meant for the UI thread: asked there, all at once (it only looks up).
      g_app_context->CallInUIThread([found, batch] {
        FigureImageProvider provider;
        {
          std::lock_guard lock(g_provider_mutex);
          provider = g_image_provider;
        }
        Found out;
        for (const SkylanderInfo* sky : *batch) {
          if (auto image = provider ? provider(sky->id, sky->variant) : nullptr) out.emplace_back(sky, image);
        }
        found->set_value(std::move(out));
      });
      if (result.wait_for(10s) != std::future_status::ready) continue;
      const Found images = result.get();
      figure_quiet = images.empty() && (written || pending.size() < portal::AllSkylanders().size()) ? figure_quiet + 1 : 0;
      for (const auto& [sky, image] : images) {
        if (g_picture_cache_stop) return;
        if (image->width > 0 && image->height > 0 &&
            image->rgba.size() >= size_t(image->width) * image->height * 4 &&
            WritePng(file_of(*sky), image->width, image->height, image->rgba.data())) {
          ++written;
        }
        std::erase(pending, sky);
      }
      for (int i = 0; i < 4 && !g_picture_cache_stop; ++i) std::this_thread::sleep_for(500ms);
    }
    if (written) REXLOG_INFO("Portal menu: {} figure pictures for the launcher in {}", written, PathUtf8(dir));
    // The hats, once: asked until three rounds in a row add none (the game's hat table loads in the
    // background; numbers it doesn't have never come).
    if (fs::exists(dir / "hats.txt", ec)) {
      WriteUpgradeCache(dir);
      return;
    }
    constexpr uint16_t kLastHat = 300;
    struct Hat {
      std::shared_ptr<const HatInfo> info;
      std::shared_ptr<const FigureImage> image;
    };
    std::map<uint16_t, Hat> hats;
    int quiet = 0;
    for (int round = 0; round < 30 && quiet < 3 && !g_picture_cache_stop; ++round) {
      using Found = std::vector<std::pair<uint16_t, Hat>>;
      auto found = std::make_shared<std::promise<Found>>();
      auto result = found->get_future();
      auto known = std::make_shared<std::vector<uint16_t>>();
      for (const auto& [hat, unused] : hats) known->push_back(hat);
      if (!g_app_context) return;
      g_app_context->CallInUIThread([found, known] {
        HatInfoProvider info_provider;
        ArtImageProvider art_provider;
        {
          std::lock_guard lock(g_provider_mutex);
          info_provider = g_hat_info_provider;
          art_provider = g_art_provider;
        }
        Found out;
        for (uint16_t hat = 1; info_provider && hat <= kLastHat; ++hat) {
          if (std::find(known->begin(), known->end(), hat) != known->end()) continue;
          auto info = info_provider(hat);
          if (!info || info->name.empty()) continue;
          out.emplace_back(hat, Hat{info, art_provider && !info->image.empty() ? art_provider(info->image) : nullptr});
        }
        found->set_value(std::move(out));
      });
      if (result.wait_for(10s) != std::future_status::ready) continue;
      const Found more = result.get();
      quiet = more.empty() && !hats.empty() ? quiet + 1 : 0;
      for (const auto& [hat, entry] : more) hats[hat] = entry;
      for (int i = 0; i < 4 && !g_picture_cache_stop; ++i) std::this_thread::sleep_for(500ms);
    }
    if (g_picture_cache_stop) return;
    if (hats.empty()) return WriteUpgradeCache(dir);
    // Pictures some games read on demand once asked: asked again for a few rounds.
    for (int round = 0; round < 6 && !g_picture_cache_stop; ++round) {
      auto missing = std::make_shared<std::vector<std::pair<uint16_t, std::string>>>();
      for (const auto& [hat, entry] : hats) {
        if (!entry.image && !entry.info->image.empty()) missing->emplace_back(hat, entry.info->image);
      }
      if (missing->empty()) break;
      using Found = std::vector<std::pair<uint16_t, std::shared_ptr<const FigureImage>>>;
      auto found = std::make_shared<std::promise<Found>>();
      auto result = found->get_future();
      if (!g_app_context) return;
      g_app_context->CallInUIThread([found, missing] {
        ArtImageProvider art_provider;
        {
          std::lock_guard lock(g_provider_mutex);
          art_provider = g_art_provider;
        }
        Found out;
        for (const auto& [hat, key] : *missing) {
          if (auto image = art_provider ? art_provider(key) : nullptr) out.emplace_back(hat, image);
        }
        found->set_value(std::move(out));
      });
      if (result.wait_for(10s) != std::future_status::ready) continue;
      for (const auto& [hat, image] : result.get()) hats[hat].image = image;
      for (int i = 0; i < 4 && !g_picture_cache_stop; ++i) std::this_thread::sleep_for(500ms);
    }
    std::string text;
    size_t pictures = 0;
    for (const auto& [hat, entry] : hats) {
      std::string bonuses;
      for (const HatStat& stat : entry.info->stats) {
        char value[32];
        std::snprintf(value, sizeof(value), stat.value == std::floor(stat.value) ? "%+.0f" : "%+.1f",
                      double(stat.value));
        bonuses += (bonuses.empty() ? "" : " / ") + std::string(value) + " " + stat.name;
      }
      text += std::to_string(hat) + "\t" + entry.info->name + "\t" + bonuses + "\n";
      const auto& image = entry.image;
      if (image && image->width > 0 && image->height > 0 &&
          image->rgba.size() >= size_t(image->width) * image->height * 4 &&
          WritePng(dir / ("hat-" + std::to_string(hat) + ".png"), image->width, image->height, image->rgba.data())) {
        ++pictures;
      }
    }
    std::ofstream(dir / "hats.txt", std::ios::binary) << text;
    REXLOG_INFO("Portal menu: {} hats ({} pictures) for the launcher", hats.size(), pictures);
    WriteUpgradeCache(dir);
}

}  // namespace

void SetFigureNameProvider(FigureNameProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_name_provider = std::move(provider);
}

void SetAdventurePackName(std::string name) {
  std::lock_guard lock(g_provider_mutex);
  g_pack_name = std::move(name);
}

void SetFigureTraitProvider(FigureTraitProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_trait_provider = std::move(provider);
}

void SetFigureStatsTraitProvider(FigureStatsTraitProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_stats_trait_provider = std::move(provider);
}

void SetArtImageProvider(ArtImageProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_art_provider = std::move(provider);
}

void SetFigureUpgradesProvider(FigureUpgradesProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_upgrades_provider = std::move(provider);
}

void SetHatInfoProvider(HatInfoProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_hat_info_provider = std::move(provider);
}

void SetItemDurationProvider(ItemDurationProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_item_duration_provider = std::move(provider);
}

void SetGameBusyProvider(std::function<bool()> busy) {
  std::lock_guard lock(g_provider_mutex);
  g_game_busy = std::move(busy);
}

void AddPortalMenuFonts(ImFontAtlas* atlas) { menu_draw::AddFonts(atlas); }

void RegisterPortalMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                        rex::ui::ImmediateDrawer* immediate_drawer) {
  g_drawer = drawer;
  g_app_context = app_context;
  g_immediate_drawer = immediate_drawer;
  g_item_app_context = app_context;
  menu_input::SetOpenHandler([](DeviceId device, uint32_t user) {
    if (!g_app_context) return;
    g_app_context->CallInUIThread([device, user] { OpenMenu(device, user, false); });
  });
  menu_input::SetButtonHandler(menu_input::ButtonAction::kItemOff, [](DeviceId, uint32_t) {
    if (g_app_context) g_app_context->CallInUIThread(TakeItemOff);
  });
  SetRealFigureArrivedHandler([](int slot) {
    if (g_app_context) g_app_context->CallInUIThread([slot] { RealFigureArrived(slot); });
  });
  rex::ui::RegisterBind("bind_portal_item_off", "F5",
                        "Take the item (magic item, adventure pack) off the portal", [] {
                          if (!menu_input::MenuOpen()) TakeItemOff();
                        });
  rex::ui::RegisterBind("bind_portal_menu", "F6",
                        "Open or close the portal menu for the keyboard's player", [] {
                          if (g_menu) {
                            g_menu->Cancel();
                          } else if (menu_input::MenuOpen()) {
                            // Another menu (the player menu) is open.
                          } else if (auto keyboard = menu_input::KeyboardDevice()) {
                            OpenMenu(*keyboard, menu_input::UserOf(*keyboard), true);
                          } else {
                            OpenMenu(DeviceId::kInvalid, 0, true);
                          }
                        });
}

}  // namespace skylanders
