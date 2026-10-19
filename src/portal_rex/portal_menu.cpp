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
#include <unordered_map>
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

#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"
#include "portal/figure_stats.h"
#include "portal/software/software_portal.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/portal_rex.h"
#include "portal_rex/settings_file.h"

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
    "Items",
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
    "Item taken off the portal",
};
constexpr const char* kGerman[] = {
    "PORTAL OF POWER",
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
    "Items",
    "Swapper zusammenbauen",
    "Oben / Unten",
    "Belegt \xC2\xB7 SP%u",
    "Auf dem Portal",
    "Runternehmen",
    "Kategorie",
    "Item runternehmen",
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
    "Item vom Portal genommen",
};
static_assert(std::size(kEnglish) == size_t(Text::kCount));
static_assert(std::size(kGerman) == size_t(Text::kCount));

bool g_german = false;

const char* T(Text text) { return (g_german ? kGerman : kEnglish)[size_t(text)]; }

// Element names as the German game shows them.
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
        return "Tech";
      case Element::kUndead:
        return "Untot";
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

std::string BadgeLabel(const std::string& badge) {
  if (g_german) {
    if (badge == "Variant") return "Variante";
    if (badge == "Chase") return "Chase-Variante";
    if (badge == "Giant") return "Gigant";
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

// The element as a medallion: gold ring, a disc shaded in the element colour, the symbol in
// white on top, like the badges on the game's own menus.
void DrawElementBadge(ImDrawList* dl, ImVec2 c, float r, Element element, int alpha = 255) {
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
// glowing top, with light rising from it. `c` is the middle of the top, `w` the width. Returns
// the lowest point drawn.
float DrawPortalOfPower(ImDrawList* dl, ImVec2 c, float w) {
  const float rx = w * 0.5f, ry = w * 0.15f, h = w * 0.12f;
  // Light rising from the top, behind the figure.
  const ImVec2 beam0{c.x - rx * 0.62f, c.y - w * 0.95f}, beam1{c.x + rx * 0.62f, c.y};
  dl->AddRectFilledMultiColor(beam0, beam1, IM_COL32(120, 210, 255, 0), IM_COL32(120, 210, 255, 0),
                              IM_COL32(120, 210, 255, 70), IM_COL32(120, 210, 255, 70));
  // A soft glow on the "floor".
  for (int i = 6; i >= 1; --i) {
    const float k = float(i) / 6.0f;
    dl->AddEllipseFilled({c.x, c.y + h}, {rx * (1.0f + 0.35f * k), ry * (1.0f + 0.35f * k)},
                         IM_COL32(80, 190, 255, int(14 * (1.0f - k) + 4)));
  }
  // The base: its side, then the top.
  const ImU32 side = IM_COL32(22, 24, 40, 255);
  dl->AddEllipseFilled({c.x, c.y + h}, {rx, ry}, side);
  dl->AddRectFilled({c.x - rx, c.y}, {c.x + rx, c.y + h}, side);
  dl->PathEllipticalArcTo({c.x, c.y + h}, {rx, ry}, 0, 0, 3.14159265f, 32);
  dl->PathStroke(IM_COL32(90, 100, 140, 220), 0, std::max(1.0f, w * 0.008f));
  dl->AddEllipseFilled(c, {rx, ry}, IM_COL32(52, 58, 86, 255));
  dl->AddEllipse(c, {rx, ry}, IM_COL32(190, 200, 225, 255), 0, 48, std::max(1.5f, w * 0.014f));
  dl->AddEllipse(c, {rx * 0.88f, ry * 0.88f}, IM_COL32(96, 106, 140, 255), 0, 48,
                 std::max(1.0f, w * 0.008f));
  // The glowing middle, from blue at the edge to white in the centre.
  for (int i = 0; i < 8; ++i) {
    const float k = float(i) / 7.0f;
    dl->AddEllipseFilled(c, {rx * (0.80f - 0.55f * k), ry * (0.80f - 0.55f * k)},
                         IM_COL32(int(40 + 190 * k), int(130 + 110 * k), 255, int(150 + 90 * k)));
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
  if (recent.size() > 16) recent.resize(16);
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
    auto listed = [&](const SkylanderInfo* sky, const SkylanderInfo* bottom) {
      if (Contains(games, sky->game) && !Contains(shown, sky->game)) return false;
      return !owned_only || Owned(sky) || (bottom && Owned(bottom));
    };

    // Characters with all their versions (catalog order is release order). Figures from later
    // games count too when they carry the id of a character this game knows: the game goes by the
    // id, so "Eon's Elite Chop Chop" (id 30) plays as Chop Chop in Swap Force, while a
    // SuperChargers driver (own id) or a character new in a later game is unknown to it.
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
      if (!IsPlayableCategory(sky.category)) {
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
        name = known->second;
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
    for (const std::string& game : shown) {
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
rex::ui::ImmediateDrawer* g_immediate_drawer = nullptr;
// Uploaded pictures by figure. UI thread only.
std::unordered_map<uint32_t, std::unique_ptr<rex::ui::ImmediateTexture>> g_textures;

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

bool IsItemSlot(int slot) { return slot >= 0 && slot == g_item_slot; }

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
    if (!software->HasFigure(slot) || IsItemSlot(slot)) continue;
    const SkylanderInfo* sky = FigureIn(slot);
    const uint32_t owner = OwnerOf(slot);
    if (sky && !IsItem(sky) && owner != user) figures.emplace_back(owner, sky);
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
      if (software->HasFigure(slot) && !OwnedByOther(slot, user) && !IsItemSlot(slot) &&
          !IsItem(FigureIn(slot))) {
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

// Puts an item on the portal in place of the previous one; no path just takes the previous one
// off. A different slot each time, so the game sees the old one leave.
void SetPortalItem(uint32_t user, const std::optional<std::filesystem::path>& path) {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return;
  int& slot = g_item_slot;
  const int old_slot = slot;
  if (slot >= 0 && software->HasFigure(slot)) RemoveFigureFromSlot(slot);
  slot = -1;
  if (!path) return;
  const int free = FreeSlot(user, {}, {old_slot});
  if (free >= 0 && PlaceFigureFromFile(free, *path)) slot = free;
}

// An item change waits until the player's Skylander is back in the game: putting the figure back
// and taking the item off in the same moment made the game ask for a Skylander again. So the
// figure goes back first, and the item follows once the game is no longer busy with figures
// (SetGameBusyProvider), at least 0.7 s later and at most 10 s.
struct PendingItem {
  uint32_t user = 0;
  std::optional<std::filesystem::path> path;  // nullopt: take it off
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
        g_item_app_context->CallInUIThread([item] { SetPortalItem(item.user, item.path); });
      }
      break;
    }
  }
}

void ScheduleItemChange(uint32_t user, std::optional<std::filesystem::path> path) {
  std::lock_guard lock(g_item_mutex);
  g_pending_item = PendingItem{user, std::move(path), ++g_item_serial};
  if (!g_item_thread.joinable()) g_item_thread = std::thread(ItemThread);
  g_item_cv.notify_all();
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
    g_german = rex::cvar::GetFlagByName("language") == "german";
    menu_input::SetMenuOpen(true);
    // The keyboard steers the menu of the player it plays as.
    const std::optional<DeviceId> keyboard = menu_input::KeyboardDevice();
    keyboard_controls_ = keyboard_ || (keyboard && menu_input::UserOf(*keyboard) == user_);
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
        // The game's own way out: B "leave the game" on its portal screen for this player.
        if (leave_) menu_input::TapButton(user_, rex::input::X_INPUT_GAMEPAD_B);
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

  // Recent opens first; with nothing recent, the lifted figure's character.
  void OpenFirstTab() {
    tab_ = element_ = selected_ = scroll_row_ = 0;
    if (library_.recent().empty() && library_.tabs().size() > 1) {
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
    }
    if (waiting_for_release_) {
      const bool keys_released =
          !ImGui::IsKeyDown(ImGuiKey_F6) && !ImGui::IsKeyDown(ImGuiKey_Enter);
      if (pads_released && keys_released) waiting_for_release_ = false;
      return {};
    }
    if (keyboard_controls_) Merge(actions, ReadKeyboard());
    return actions;
  }

  const Tab& CurrentTab() const { return library_.tabs()[tab_]; }

  size_t ItemCount() const {
    const Tab& tab = CurrentTab();
    if (tab.recent) return library_.recent().size();
    return tab.rows.empty() ? 0 : tab.rows[element_].size();
  }

  const Character* SelectedCharacter() const {
    const Tab& tab = CurrentTab();
    if (tab.recent || tab.rows.empty() || tab.rows[element_].empty()) return nullptr;
    const auto& chars = tab.rows[element_];
    return &library_.characters()[chars[std::min(selected_, chars.size() - 1)]];
  }

  const RecentEntry* SelectedRecent() const {
    if (!CurrentTab().recent || library_.recent().empty()) return nullptr;
    return &library_.recent()[std::min(selected_, library_.recent().size() - 1)];
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
      return {tops_[build_top_], bottoms_[build_bottom_]};
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
      return;
    }
    if (state_ == State::kOptions) {
      HandleOptions(a);
      return;
    }
    if (a.details) {
      previous_state_ = state_;
      state_ = State::kDetails;
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
    if (TakenBy(*character)) return;
    if (character->swapper) {
      OpenBuilder(*character);
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

  void OpenBuilder(const Character& character) {
    if (tops_.empty()) {
      for (const Character& c : library_.characters()) {
        if (!c.swapper) continue;
        for (const Version& v : c.versions) {
          if (v.bottom) {
            tops_.push_back(v.sky);
            bottoms_.push_back(v.bottom);
          }
        }
      }
    }
    if (tops_.empty()) return;
    // Start with the version the card shows, whole.
    const Version* start = &ShownVersion(character);
    build_top_ = size_t(std::find(tops_.begin(), tops_.end(), start->sky) - tops_.begin()) %
                 tops_.size();
    build_bottom_ =
        size_t(std::find(bottoms_.begin(), bottoms_.end(), start->bottom) - bottoms_.begin()) %
        bottoms_.size();
    build_column_ = 0;
    state_ = State::kBuilding;
  }

  void HandleBuilder(const Actions& a) {
    if (a.back) {
      state_ = State::kBrowsing;
      return;
    }
    if (a.left || a.lb) build_column_ = 0;
    if (a.right || a.rb) build_column_ = 1;
    size_t& index = build_column_ == 0 ? build_top_ : build_bottom_;
    const size_t count = build_column_ == 0 ? tops_.size() : bottoms_.size();
    if (a.up) index = (index + count - 1) % count;
    if (a.down) index = (index + 1) % count;
    if (a.accept) {
      const SkylanderInfo* top = tops_[build_top_];
      const SkylanderInfo* bottom = bottoms_[build_bottom_];
      if (!TakenBy(top) && !TakenBy(bottom)) Confirm({top, bottom});
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
    tops_.clear();
    bottoms_.clear();
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
      item_path_ = path;
      REXLOG_INFO("Portal menu: player {} puts on {}", user_ + 1, item.name);
    }
    item_change_ = true;
    StartCancel();
  }

  void Confirm(const std::vector<const SkylanderInfo*>& figures) {
    std::vector<std::filesystem::path> paths;
    std::vector<const SkylanderInfo*> placed;
    for (const SkylanderInfo* sky : figures) {
      if (!sky) continue;
      std::optional<std::filesystem::path> path;
      if (const OwnedFigure* owned = library_.Owned(sky)) {
        path = owned->path;
      } else {
        path = CreateFigureFile(*sky);
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
    Label(dl, BoldFont(), S(24), {c0.x + S(20), c0.y + S(7)}, kWhite, player);
  }

  void DrawMessage(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Actions& a) {
    const char* text = GetUsbPortal() || REXCVAR_GET(portal_mode) == "usb" ? T(Text::kUsbActive)
                                                                           : T(Text::kNoPortal);
    Label(dl, BoldFont(), S(30), {p0.x + S(60), (p0.y + p1.y) / 2 - S(30)}, kWhite, text,
          p1.x - p0.x - S(120));
    if (a.back || a.accept) Finish();
  }

  void DrawFooter(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const bool kb = keyboard_;
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    const ImU32 grey = Rgba(78, 86, 120);
    if (state_ == State::kMessage || state_ == State::kDetails) {
      Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kBack), !kb);
      return;
    }
    if (state_ == State::kOptions) {
      x += Hint(dl, {x, y}, kb ? "Enter" : "A", Rgba(76, 175, 80), T(Text::kToggle), !kb);
      Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kClose), !kb);
      return;
    }
    const Character* character = SelectedCharacter();
    const bool item = state_ == State::kBrowsing && character && character->item;
    const char* accept = item && current_item_ == character->versions.front().sky
                             ? T(Text::kTakeOff)
                             : T(Text::kPlace);
    x += Hint(dl, {x, y}, kb ? "Enter" : "A", Rgba(76, 175, 80), accept, !kb);
    x += Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kBack), !kb);
    x += Hint(dl, {x, y}, kb ? "L" : "X", Rgba(46, 134, 222), T(Text::kDetails), !kb);
    if (state_ == State::kBuilding) {
      Hint(dl, {x, y}, kb ? "A / D" : "< / >", grey, T(Text::kHalf), false);
    } else if (state_ == State::kBrowsing) {
      x += Hint(dl, {x, y}, kb ? "Q / E" : "LT / RT", grey, T(Text::kCategory), false);
      if (!CurrentTab().recent) {
        x += Hint(dl, {x, y}, kb ? "1 / 3" : "LB / RB", grey,
                  CurrentTab().items ? T(Text::kGame) : T(Text::kElement), false);
      }
      if (current_item_) {
        x += Hint(dl, {x, y}, kb ? "P" : "Y", Rgba(230, 180, 40), T(Text::kTakeOffItem), !kb);
      }
      // Options last, at the right edge when there is room.
      const std::string options = user_ > 0
                                      ? std::string(T(Text::kOptions)) + " / " + T(Text::kLeave)
                                      : std::string(T(Text::kOptions));
      const char* glyph = kb ? "O" : "Start";
      const float w = HintWidth(glyph, options, false);
      Hint(dl, {std::max(x, p1.x - S(38) - w + S(28)), y}, glyph, grey, options, false);
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
      Label(dl, BoldFont(), S(22), {a.x + S(20), a.y + S(10)}, active ? kInk : kDim, title);
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
      if (tab.recent) {
        DrawRecentCard(dl, a, b, library_.recent()[i], i == selected_);
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
        Label(dl, BoldFont(), S(21), {x + (tab.items ? S(20) : S(60)), a.y + S(15)},
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

  // A small rounded label; returns its width.
  float Pill(ImDrawList* dl, ImVec2 pos, std::string_view text, ImU32 bg, ImU32 fg,
             float size = 14) {
    const ImVec2 ts = TextSize(BoldFont(), S(size), text);
    const ImVec2 end{pos.x + ts.x + S(14), pos.y + ts.y + S(4)};
    dl->AddRectFilled(pos, end, bg, S(8));
    Label(dl, BoldFont(), S(size), {pos.x + S(7), pos.y + S(2)}, fg, text);
    return end.x - pos.x;
  }

  float PillWidth(std::string_view text, float size = 14) {
    return TextSize(BoldFont(), S(size), text).x + S(14);
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
    if (character.versions.size() > 1) {
      Pill(dl, {p0.x + S(6), p1.y - S(27)},
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
    if (const OwnedFigure* owned = library_.OwnedAt(entry.paths[0]); owned && owned->stats) {
      const std::string lv = "Lv " + std::to_string(owned->stats->level);
      Pill(dl, {p1.x - S(6) - PillWidth(lv), p0.y + S(6)}, lv, kGold, kInk);
    }
    if (auto owner = TakenBy(entry)) DrawTaken(dl, a, b, *owner);
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
    const float portal_bottom = DrawPortalOfPower(dl, {cx, portal_y}, pic * 1.5f);
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
    if (owned && !item) {
      if (owned->stats) {
        std::string stats = "Level " + std::to_string(owned->stats->level) + "   " +
                            std::to_string(owned->stats->gold) + " Gold";
        Label(dl, BoldFont(), S(23), {a.x + S(22), y}, kWhite, stats);
        y += S(32);
        if (!owned->stats->nickname.empty()) {
          Label(dl, BoldFont(), S(21), {a.x + S(22), y}, kDim,
                "\"" + owned->stats->nickname + "\"", wrap);
        }
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

  // Two lists side by side, tops and bottoms; the preview shows the combination.
  void DrawBuilder(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    ImVec2 a, b;
    const float w = std::min(area1.x - area0.x, S(980));
    DrawOverlayPanel(dl, area0, area1, {w, area1.y - area0.y}, a, b);
    Label(dl, BoldFont(), S(26), {a.x + S(28), a.y + S(20)}, kGold, T(Text::kBuildSwapper));
    const std::string combo = ComboName({tops_[build_top_], bottoms_[build_bottom_]});
    const ImVec2 cs = TextSize(TitleFont(), S(34), combo);
    OutlinedLabel(dl, TitleFont(), S(34), {b.x - S(28) - cs.x, a.y + S(14)}, kWhite, combo, S(2));

    const float col_gap = S(20);
    const float col_w = (b.x - a.x - S(56) - col_gap) / 2;
    const float list_top = a.y + S(110);
    const float row_h = S(54), row_gap = S(8);
    const size_t visible =
        std::max<size_t>(1, size_t((b.y - S(20) - list_top + row_gap) / (row_h + row_gap)));
    for (int column = 0; column < 2; ++column) {
      const auto& list = column == 0 ? tops_ : bottoms_;
      size_t& index = column == 0 ? build_top_ : build_bottom_;
      size_t& scroll = column == 0 ? top_scroll_ : bottom_scroll_;
      const bool focused = build_column_ == column;
      const float x0 = a.x + S(28) + column * (col_w + col_gap);
      Label(dl, BoldFont(), S(22), {x0 + S(6), list_top - S(38)}, focused ? kGold : kDim,
            T(column == 0 ? Text::kTop : Text::kBottom));
      if (index < scroll) scroll = index;
      if (index >= scroll + visible) scroll = index - visible + 1;
      for (size_t i = scroll; i < list.size() && i < scroll + visible; ++i) {
        const SkylanderInfo* sky = list[i];
        const Element element = portal::FigureElement(*sky);
        const ImVec2 r0{x0, list_top + (i - scroll) * (row_h + row_gap)};
        const ImVec2 r1{x0 + col_w, r0.y + row_h};
        const bool chosen = i == index;
        if (state_ == State::kBuilding && (MouseSelects(r0, r1) ||
                                           (ImGui::IsMouseHoveringRect(r0, r1) &&
                                            ImGui::IsMouseClicked(0)))) {
          build_column_ = column;
          index = i;
        }
        dl->AddRectFilled(r0, r1,
                          chosen ? ElementColor(element, focused ? 0.80f : 0.55f, 255)
                                 : Rgba(255, 255, 255, 14),
                          S(12));
        if (chosen) dl->AddRect(r0, r1, focused ? kGold : Rgba(255, 255, 255, 90), S(12), 0,
                                focused ? S(3) : S(1.5f));
        DrawFigurePicture(dl, {r0.x + S(5), r0.y + S(5)}, {r0.x + S(49), r0.y + S(49)}, sky, S(8));
        Label(dl, BoldFont(), S(21), {r0.x + S(56), r0.y + S(14)}, kWhite,
              portal::DisplayName(portal::SwapperBaseName(*sky)), col_w - S(130));
        if (auto owner = TakenBy(sky)) {
          dl->AddRectFilled(r0, r1, Rgba(8, 10, 26, 150), S(12));
          char text[48];
          std::snprintf(text, sizeof(text), T(Text::kTaken), *owner + 1);
          const ImVec2 ts = TextSize(BoldFont(), S(16), text);
          dl->AddRectFilled({r1.x - ts.x - S(30), r0.y + S(13)}, {r1.x - S(10), r1.y - S(13)},
                            PlayerColor(*owner), S(14));
          Label(dl, BoldFont(), S(16), {r1.x - ts.x - S(20), r0.y + S(16)}, kWhite, text);
        } else if (const OwnedFigure* owned = library_.Owned(sky); owned && owned->stats) {
          const std::string lv = "Lv " + std::to_string(owned->stats->level);
          const ImVec2 ts = TextSize(BoldFont(), S(18), lv);
          Label(dl, BoldFont(), S(18), {r1.x - ts.x - S(16), r0.y + S(16)}, kGold, lv);
        }
      }
    }
  }

  void DrawDetails(ImDrawList* dl, ImVec2 area0, ImVec2 area1) {
    const auto figures = PreviewFigures();
    ImVec2 a, b;
    DrawOverlayPanel(dl, area0, area1, {S(720), S(140) + figures.size() * S(190)}, a, b);
    Label(dl, BoldFont(), S(28), {a.x + S(28), a.y + S(22)}, kGold, T(Text::kDetails));
    float y = a.y + S(76);
    for (const SkylanderInfo* sky : figures) {
      const Element element = portal::FigureElement(*sky);
      DrawFigurePicture(dl, {a.x + S(22), y}, {a.x + S(78), y + S(56)}, sky, S(10));
      Label(dl, BoldFont(), S(26), {a.x + S(88), y + S(6)}, kWhite, portal::DisplayName(sky->name));
      y += S(48);
      std::string line = IsItem(sky) ? std::string(T(Text::kItems))
                                     : BadgeLabel(portal::VersionBadge(*sky)) + "   " +
                                           std::string(ElementLabel(element));
      line += "   " + std::string(sky->game);
      Label(dl, BoldFont(), S(19), {a.x + S(88), y}, kDim, line);
      y += S(32);
      if (const OwnedFigure* owned = library_.Owned(sky)) {
        if (owned->stats && !IsItem(sky)) {
          std::string stats = "Level " + std::to_string(owned->stats->level) + "   " +
                              std::to_string(owned->stats->gold) + " Gold";
          if (!owned->stats->nickname.empty()) {
            stats += "   " + std::string(T(Text::kNickname)) + ": " + owned->stats->nickname;
          }
          Label(dl, BoldFont(), S(21), {a.x + S(88), y}, kWhite, stats);
          y += S(32);
        }
        Label(dl, BoldFont(), S(17), {a.x + S(88), y}, Rgba(180, 190, 220),
              std::string(T(Text::kFile)) + ": " + PathUtf8(owned->path.filename()));
      } else {
        Label(dl, BoldFont(), S(19), {a.x + S(88), y}, kDim, T(Text::kNewFigure));
      }
      y += S(78);
    }
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
  size_t tab_ = 0;
  size_t element_ = 0;  // row of the current tab
  size_t selected_ = 0;
  size_t scroll_row_ = 0;
  size_t columns_ = 1;
  std::vector<Choice> choices_;
  size_t choice_ = 0;
  size_t choice_scroll_ = 0;
  std::vector<const SkylanderInfo*> tops_, bottoms_;
  int build_column_ = 0;
  size_t build_top_ = 0, build_bottom_ = 0;
  size_t top_scroll_ = 0, bottom_scroll_ = 0;
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
  g_german = rex::cvar::GetFlagByName("language") == "german";
  REXLOG_INFO("Portal menu: item taken off with the item button");
  SetPortalItem(0, std::nullopt);
  ShowToast(T(Text::kItemTakenOff));
}

}  // namespace

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

void ShutdownPortalMenu() {
  StopItemThread();
  g_menu.reset();
  g_toast.reset();
  g_textures.clear();
  g_immediate_drawer = nullptr;
}

void SetFigureImageProvider(FigureImageProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_image_provider = std::move(provider);
}

void SetFigureNameProvider(FigureNameProvider provider) {
  std::lock_guard lock(g_provider_mutex);
  g_name_provider = std::move(provider);
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
