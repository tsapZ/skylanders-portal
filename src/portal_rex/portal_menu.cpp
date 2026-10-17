#include "portal_rex/portal_menu.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/device.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/windowed_app_context.h>

#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"
#include "portal/figure_stats.h"
#include "portal/software/software_portal.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/portal_rex.h"

REXCVAR_DEFINE_STRING(portal_menu_games, "", "Portal",
                      "Games whose Skylanders the portal menu offers, separated by ';' (empty: "
                      "all games)");

namespace skylanders {
namespace {

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
  kWhole,
  kTopOnly,
  kBottomOnly,
  kTop,
  kBottom,
  kNewFigure,
  kYourFigure,
  kUsbActive,
  kNoPortal,
  kNothingRecent,
  kChooseVersion,
  kKeeps,
  kVersions,
  kNickname,
  kFile,
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
    "whole",
    "top half only",
    "bottom half only",
    "Top",
    "Bottom",
    "New figure, created when placed",
    "Your figure",
    "The real USB portal is active. Put your figures on it.",
    "No portal is active (portal_mode = none).",
    "Nothing placed yet. Choose a game with LT / RT.",
    "Which version?",
    "keeps",
    "versions",
    "Nickname",
    "File",
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
    "ganz",
    "nur obere H\xC3\xA4lfte",
    "nur untere H\xC3\xA4lfte",
    "Oben",
    "Unten",
    "Neue Figur, wird beim Auflegen angelegt",
    "Deine Figur",
    "Das echte USB-Portal ist aktiv. Stell deine Figuren direkt aufs Portal.",
    "Kein Portal aktiv (portal_mode = none).",
    "Noch nichts aufgelegt. W\xC3\xA4hle mit LT / RT ein Spiel.",
    "Welche Fassung?",
    "bleibt",
    "Fassungen",
    "Spitzname",
    "Datei",
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

// --- Fonts and colours ------------------------------------------------------------------------

ImFont* g_font_title = nullptr;
ImFont* g_font_bold = nullptr;
// Baked large so they stay sharp when drawn smaller on 1080p and 1440p.
constexpr float kTitleBakeSize = 72.0f;
constexpr float kBoldBakeSize = 44.0f;

ImFont* TitleFont() { return g_font_title ? g_font_title : ImGui::GetFont(); }
ImFont* BoldFont() { return g_font_bold ? g_font_bold : ImGui::GetFont(); }

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

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
  return {0.50f, 0.50f, 0.56f, 1};
}

ImU32 ElementColor(Element element, float brightness = 1.0f, int alpha = 255) {
  ImVec4 c = ElementRgb(element);
  return IM_COL32(int(std::min(1.0f, c.x * brightness) * 255),
                  int(std::min(1.0f, c.y * brightness) * 255),
                  int(std::min(1.0f, c.z * brightness) * 255), alpha);
}

ImU32 PlayerColor(uint32_t user) {
  switch (user) {
    case 1:
      return Rgba(235, 77, 77);
    case 2:
      return Rgba(61, 200, 120);
    case 3:
      return Rgba(240, 190, 50);
    default:
      return Rgba(61, 139, 255);
  }
}

const ImU32 kGold = Rgba(244, 196, 84);
const ImU32 kGoldDark = Rgba(150, 104, 30);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(205, 214, 238);
const ImU32 kInk = Rgba(22, 20, 32);

std::string_view GameShortName(std::string_view game) {
  if (game == "Spyro's Adventure") return "SSA";
  if (game == "Swap Force") return "SWAP Force";
  return game;
}

// --- Drawing helpers ----------------------------------------------------------------------------

float g_s = 1.0f;  // pixels per 1080p unit
float S(float v) { return v * g_s; }

void Label(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, std::string_view text,
           float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.data(), text.data() + text.size(), wrap);
}

// Text with a dark outline, for titles over busy backgrounds.
void OutlinedLabel(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
                   std::string_view text, float outline) {
  const ImU32 shadow = Rgba(10, 8, 24, 220);
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      if (dx || dy) Label(dl, font, size, {pos.x + dx * outline, pos.y + dy * outline}, shadow, text);
    }
  }
  Label(dl, font, size, {pos.x, pos.y + outline * 1.5f}, shadow, text);
  Label(dl, font, size, pos, col, text);
}

ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap = 0.0f) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
}

// Self-drawn element symbols (the official ones can't be used): star, boulder, drop, flame, gear,
// skull, leaf, swirl, sun, crescent.
void DrawElementIcon(ImDrawList* dl, ImVec2 c, float r, Element element, ImU32 col) {
  const int alpha = int((col >> IM_COL32_A_SHIFT) & 0xFF);
  const ImU32 detail = IM_COL32(10, 8, 20, alpha * 2 / 3);
  auto P = [&](float x, float y) { return ImVec2{c.x + x * r, c.y + y * r}; };
  std::vector<ImVec2> pts;
  switch (element) {
    case Element::kMagic: {
      for (int i = 0; i < 10; ++i) {
        const float a = -1.5707963f + i * 3.14159265f / 5.0f;
        const float rr = (i % 2) ? 0.42f : 1.0f;
        pts.push_back(P(std::cos(a) * rr, std::sin(a) * rr));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), col);
      break;
    }
    case Element::kEarth: {
      pts = {P(-0.92f, 0.55f), P(-0.72f, -0.25f), P(-0.25f, -0.82f), P(0.40f, -0.80f),
             P(0.88f, -0.22f), P(0.92f, 0.52f),  P(0.25f, 0.88f),   P(-0.50f, 0.86f)};
      dl->AddConvexPolyFilled(pts.data(), int(pts.size()), col);
      const ImVec2 crack[] = {P(-0.30f, -0.55f), P(-0.05f, -0.05f), P(-0.35f, 0.35f)};
      dl->AddPolyline(crack, 3, detail, 0, r * 0.10f);
      const ImVec2 crack2[] = {P(0.35f, -0.35f), P(0.15f, 0.15f), P(0.50f, 0.50f)};
      dl->AddPolyline(crack2, 3, detail, 0, r * 0.08f);
      break;
    }
    case Element::kWater: {
      pts.push_back(P(0.0f, -1.0f));
      for (int i = 0; i <= 16; ++i) {
        const float a = -0.55f + i * (3.14159265f + 1.1f) / 16.0f;
        pts.push_back(P(std::cos(a) * 0.64f, 0.30f + std::sin(a) * 0.64f));
      }
      dl->AddConvexPolyFilled(pts.data(), int(pts.size()), col);
      dl->AddCircleFilled(P(-0.22f, 0.25f), r * 0.14f, IM_COL32(255, 255, 255, alpha / 2));
      break;
    }
    case Element::kFire: {
      pts = {P(0.0f, -1.0f),   P(0.22f, -0.50f), P(0.52f, -0.68f), P(0.48f, -0.18f),
             P(0.76f, 0.18f),  P(0.62f, 0.72f),  P(0.0f, 0.98f),   P(-0.62f, 0.72f),
             P(-0.76f, 0.18f), P(-0.50f, -0.22f), P(-0.36f, -0.56f), P(-0.16f, -0.30f)};
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), col);
      const ImVec2 inner[] = {P(0.0f, 0.05f), P(0.30f, 0.45f), P(0.0f, 0.82f), P(-0.30f, 0.45f)};
      dl->AddConvexPolyFilled(inner, 4, detail);
      break;
    }
    case Element::kTech: {
      const int teeth = 8;
      for (int i = 0; i < teeth; ++i) {
        const float a = i * 6.2831853f / teeth;
        const float step = 6.2831853f / teeth;
        pts.push_back(P(std::cos(a - step * 0.18f), std::sin(a - step * 0.18f)));
        pts.push_back(P(std::cos(a + step * 0.18f), std::sin(a + step * 0.18f)));
        pts.push_back(P(std::cos(a + step * 0.32f) * 0.74f, std::sin(a + step * 0.32f) * 0.74f));
        pts.push_back(P(std::cos(a + step * 0.68f) * 0.74f, std::sin(a + step * 0.68f) * 0.74f));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), col);
      dl->AddCircleFilled(c, r * 0.30f, detail);
      break;
    }
    case Element::kUndead: {
      dl->AddCircleFilled(P(0.0f, -0.12f), r * 0.78f, col);
      dl->AddRectFilled(P(-0.44f, 0.30f), P(0.44f, 0.88f), col, r * 0.14f);
      dl->AddCircleFilled(P(-0.30f, -0.10f), r * 0.21f, detail);
      dl->AddCircleFilled(P(0.30f, -0.10f), r * 0.21f, detail);
      const ImVec2 nose[] = {P(0.0f, 0.14f), P(0.11f, 0.34f), P(-0.11f, 0.34f)};
      dl->AddConvexPolyFilled(nose, 3, detail);
      dl->AddLine(P(-0.15f, 0.62f), P(-0.15f, 0.88f), detail, r * 0.07f);
      dl->AddLine(P(0.15f, 0.62f), P(0.15f, 0.88f), detail, r * 0.07f);
      break;
    }
    case Element::kLife: {
      // A leaf: two arcs, tipped at the top right and bottom left.
      for (int i = 0; i <= 14; ++i) {
        const float t = float(i) / 14.0f;
        pts.push_back(P(-0.82f + 1.64f * t - 0.55f * std::sin(t * 3.14159265f),
                        0.82f - 1.64f * t - 0.55f * std::sin(t * 3.14159265f)));
      }
      for (int i = 13; i >= 1; --i) {
        const float t = float(i) / 14.0f;
        pts.push_back(P(-0.82f + 1.64f * t + 0.55f * std::sin(t * 3.14159265f),
                        0.82f - 1.64f * t + 0.55f * std::sin(t * 3.14159265f)));
      }
      dl->AddConvexPolyFilled(pts.data(), int(pts.size()), col);
      dl->AddLine(P(-0.82f, 0.82f), P(0.50f, -0.50f), detail, r * 0.09f);
      break;
    }
    case Element::kAir: {
      for (int i = 0; i <= 40; ++i) {
        const float t = float(i) / 40.0f;
        const float a = t * 3.14159265f * 3.2f;
        const float rr = 0.12f + 0.82f * t;
        pts.push_back(P(std::cos(a) * rr, std::sin(a) * rr * 0.85f));
      }
      dl->AddPolyline(pts.data(), int(pts.size()), col, 0, r * 0.20f);
      break;
    }
    case Element::kLight: {
      dl->AddCircleFilled(c, r * 0.48f, col);
      for (int i = 0; i < 8; ++i) {
        const float a = i * 0.7853982f;
        const ImVec2 ray[] = {P(std::cos(a) * 0.98f, std::sin(a) * 0.98f),
                              P(std::cos(a + 0.22f) * 0.60f, std::sin(a + 0.22f) * 0.60f),
                              P(std::cos(a - 0.22f) * 0.60f, std::sin(a - 0.22f) * 0.60f)};
        dl->AddConvexPolyFilled(ray, 3, col);
      }
      break;
    }
    case Element::kDark: {
      for (int i = 0; i <= 20; ++i) {
        const float a = 0.6f + i * (6.2831853f - 1.2f) / 20.0f;
        pts.push_back(P(std::cos(a) * 0.95f, std::sin(a) * 0.95f));
      }
      for (int i = 20; i >= 0; --i) {
        const float a = 1.1f + i * (6.2831853f - 2.2f) / 20.0f;
        pts.push_back(P(0.42f + std::cos(a) * 0.70f, -0.18f + std::sin(a) * 0.70f));
      }
      dl->AddConcavePolyFilled(pts.data(), int(pts.size()), col);
      break;
    }
    case Element::kNone:
      dl->AddCircle(c, r * 0.8f, col, 0, r * 0.15f);
      break;
  }
}

// A controller button or key hint: a coloured round glyph for face buttons, a pill otherwise.
float Hint(ImDrawList* dl, ImVec2 pos, std::string_view glyph, ImU32 glyph_col,
           std::string_view label, bool round) {
  const float h = S(30);
  const float glyph_w = round ? h : std::max(h, TextSize(BoldFont(), S(18), glyph).x + S(16));
  if (round) {
    dl->AddCircleFilled({pos.x + h / 2, pos.y + h / 2}, h / 2, glyph_col);
  } else {
    dl->AddRectFilled(pos, {pos.x + glyph_w, pos.y + h}, glyph_col, S(8));
  }
  ImVec2 gs = TextSize(BoldFont(), S(18), glyph);
  Label(dl, BoldFont(), S(18), {pos.x + (glyph_w - gs.x) / 2, pos.y + (h - gs.y) / 2}, kWhite,
        glyph);
  Label(dl, BoldFont(), S(20), {pos.x + glyph_w + S(8), pos.y + S(3)}, kDim, label);
  return glyph_w + S(8) + TextSize(BoldFont(), S(20), label).x + S(28);
}

// The glowing ring of a Portal of Power, drawn under the preview figure.
void DrawPortalGlow(ImDrawList* dl, ImVec2 c, float w) {
  for (int i = 8; i >= 1; --i) {
    const float k = float(i) / 8.0f;
    dl->AddEllipseFilled(c, {w * 0.5f * (0.6f + 0.6f * k), w * 0.11f * (0.6f + 0.6f * k)},
                         IM_COL32(80, 190, 255, int(26 * (1.0f - k) + 6)));
  }
  dl->AddEllipse(c, {w * 0.42f, w * 0.085f}, IM_COL32(140, 225, 255, 210), 0, 0, S(3));
  dl->AddEllipse(c, {w * 0.30f, w * 0.060f}, IM_COL32(244, 196, 84, 160), 0, 0, S(2));
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

struct Character {
  std::string name;
  Element element = Element::kNone;
  std::string debut_game;
  bool swapper = false;
  std::vector<Version> versions;
};

// A recently placed combination of figure files (two for a swapper).
struct RecentEntry {
  std::vector<std::filesystem::path> paths;
  std::vector<const SkylanderInfo*> figures;
};

struct Tab {
  std::string title;
  bool recent = false;
  std::vector<Element> elements;
  std::vector<std::vector<size_t>> by_element;  // character indices per entry of `elements`
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

bool IsPlayableCategory(std::string_view category) {
  return category != "Items" && category != "Unreleased";
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

    // Characters, each under the game it debuted in (catalog order is release order).
    std::unordered_map<std::string, size_t> index;
    for (const SkylanderInfo& sky : portal::AllSkylanders()) {
      if (std::find(games.begin(), games.end(), sky.game) == games.end() ||
          !IsPlayableCategory(sky.category) ||
          portal::GetSwapperHalf(sky) == SwapperHalf::kBottom) {
        continue;
      }
      std::string name(portal::CharacterName(sky));
      if (name.empty()) name = portal::DisplayName(portal::SwapperBaseName(sky));
      auto [it, inserted] = index.emplace(name, characters_.size());
      if (inserted) {
        Character character;
        character.name = name;
        character.element = portal::FigureElement(sky);
        character.debut_game = sky.game;
        character.swapper = portal::GetSwapperHalf(sky) == SwapperHalf::kTop;
        characters_.push_back(std::move(character));
      }
      Version version{&sky, nullptr};
      if (portal::GetSwapperHalf(sky) == SwapperHalf::kTop) {
        version.bottom = portal::SwapperPartner(sky);
      }
      characters_[it->second].versions.push_back(version);
    }

    // Recent first, then one tab per game, its characters grouped by element.
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
      if (!entry.figures.empty() &&
          std::find(games.begin(), games.end(), entry.figures[0]->game) != games.end()) {
        recent_.push_back(std::move(entry));
      }
    }
    for (const std::string& game : games) {
      Tab tab;
      tab.title = game;
      for (size_t i = 0; i < characters_.size(); ++i) {
        if (characters_[i].debut_game != game) continue;
        auto e = std::find(tab.elements.begin(), tab.elements.end(), characters_[i].element);
        if (e == tab.elements.end()) {
          tab.elements.push_back(characters_[i].element);
          tab.by_element.emplace_back();
          e = tab.elements.end() - 1;
        }
        tab.by_element[size_t(e - tab.elements.begin())].push_back(i);
      }
      // Elements in the game's order; within one, characters with an owned version first.
      std::vector<size_t> order(tab.elements.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = i;
      std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return ElementRank(tab.elements[a]) < ElementRank(tab.elements[b]);
      });
      Tab sorted;
      sorted.title = tab.title;
      for (size_t i : order) {
        sorted.elements.push_back(tab.elements[i]);
        std::vector<size_t> chars = tab.by_element[i];
        std::stable_sort(chars.begin(), chars.end(), [&](size_t a, size_t b) {
          return (BestOwned(characters_[a]) != nullptr) > (BestOwned(characters_[b]) != nullptr);
        });
        sorted.by_element.push_back(std::move(chars));
      }
      if (!sorted.elements.empty()) tabs_.push_back(std::move(sorted));
    }
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

  // The version played last, if any version is owned.
  const OwnedFigure* BestOwned(const Character& character) const {
    const OwnedFigure* best = nullptr;
    for (const Version& v : character.versions) {
      for (const SkylanderInfo* sky : {v.sky, v.bottom}) {
        const OwnedFigure* owned = Owned(sky);
        if (owned && (!best || owned->mtime > best->mtime)) best = owned;
      }
    }
    return best;
  }

  // Finds the tab, element and position of the character a figure belongs to.
  bool Locate(const SkylanderInfo* sky, size_t& tab, size_t& element, size_t& position) const {
    for (size_t t = 0; t < tabs_.size(); ++t) {
      for (size_t e = 0; e < tabs_[t].by_element.size(); ++e) {
        const auto& chars = tabs_[t].by_element[e];
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
};

// --- Players on the emulated portal -----------------------------------------------------------

// Which portal slots hold each player's figure (two for a swapper). The game itself only sees
// slots and assigns players by the order figures arrive; when one player's figure is lifted and
// another placed, that player gets it. UI thread only.
std::array<std::vector<int>, rex::input::kMaxGuestUsers> g_player_slots;

bool OwnedByOther(int slot, uint32_t user) {
  for (uint32_t u = 0; u < g_player_slots.size(); ++u) {
    if (u != user && std::find(g_player_slots[u].begin(), g_player_slots[u].end(), slot) !=
                         g_player_slots[u].end()) {
      return true;
    }
  }
  return false;
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
      if (software->HasFigure(slot) && !OwnedByOther(slot, user)) slots.push_back(slot);
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

// --- Menu input ---------------------------------------------------------------------------------

struct Actions {
  bool up = false, down = false, left = false, right = false;
  bool accept = false, back = false, details = false, lb = false, rb = false, lt = false,
       rt = false;
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
  // L = X, 1/3 = LB/RB, Q/E = LT/RT.
  a.accept = once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
  a.back = once(ImGuiKey_Escape) || once(ImGuiKey_Backspace);
  a.details = once(ImGuiKey_L) || once(ImGuiKey_Tab);
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
}

// --- The dialog ---------------------------------------------------------------------------------

// One line of the version list: what it puts on the portal.
struct Choice {
  std::string label;
  std::string badge;
  std::string detail;
  std::vector<const SkylanderInfo*> figures;
};

class PortalMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  PortalMenuDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done, DeviceId device,
                   uint32_t user, bool from_keyboard)
      : ImGuiDialog(drawer),
        on_done_(std::move(on_done)),
        device_(device),
        user_(user),
        keyboard_(from_keyboard) {
    g_german = rex::cvar::GetFlagByName("language") == "german";
    menu_input::SetMenuOpen(true);
    software_ = GetSoftwarePortal() != nullptr;
    if (software_) {
      library_.Build(user_);
      lifted_ = LiftPlayer(user_);
      lift_time_ = Clock::now();
      // Recent opens first; with nothing recent, the lifted figure's character.
      tab_ = 0;
      if (library_.recent().empty() && library_.tabs().size() > 1) {
        tab_ = 1;
        for (const LiftedFigure& figure : lifted_) {
          if (figure.sky && library_.Locate(figure.sky, tab_, element_, selected_)) break;
        }
      }
    }
    REXLOG_INFO("Portal menu: opened for player {}", user_ + 1);
  }

  ~PortalMenuDialog() override {
    if (state_ != State::kDone) menu_input::SetMenuOpen(false);
  }

  // Closes like the Back button: the lifted figure goes back on the portal.
  void Cancel() {
    if (state_ == State::kBrowsing || state_ == State::kChoosing || state_ == State::kDetails ||
        state_ == State::kMessage) {
      StartCancel();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    g_s = io.DisplaySize.y / 1080.0f;
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
        Finish();
      }
      return;
    }
    if (state_ == State::kDone) return;

    Actions actions = ReadActions();
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
    ImGui::End();
  }

 private:
  enum class State { kBrowsing, kChoosing, kDetails, kMessage, kWaiting, kDone };

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
    std::optional<rex::input::X_INPUT_GAMEPAD> pad;
    if (!keyboard_) pad = menu_input::ReadPad(device_);
    if (waiting_for_release_) {
      const bool pad_released = !pad || pad_reader_.Released(*pad);
      const bool keys_released =
          !ImGui::IsKeyDown(ImGuiKey_F6) && !ImGui::IsKeyDown(ImGuiKey_Enter);
      if (pad_released && keys_released) waiting_for_release_ = false;
      if (pad) pad_reader_.Read(*pad);
      return actions;
    }
    if (pad) Merge(actions, pad_reader_.Read(*pad));
    Merge(actions, ReadKeyboard());
    return actions;
  }

  const Tab& CurrentTab() const { return library_.tabs()[tab_]; }

  size_t ItemCount() const {
    const Tab& tab = CurrentTab();
    if (tab.recent) return library_.recent().size();
    return tab.by_element.empty() ? 0 : tab.by_element[element_].size();
  }

  const Character* SelectedCharacter() const {
    const Tab& tab = CurrentTab();
    if (tab.recent || tab.by_element.empty() || tab.by_element[element_].empty()) return nullptr;
    const auto& chars = tab.by_element[element_];
    return &library_.characters()[chars[std::min(selected_, chars.size() - 1)]];
  }

  const RecentEntry* SelectedRecent() const {
    if (!CurrentTab().recent || library_.recent().empty()) return nullptr;
    return &library_.recent()[std::min(selected_, library_.recent().size() - 1)];
  }

  const SkylanderInfo* LiftedHalf(SwapperHalf half) const {
    for (const LiftedFigure& f : lifted_) {
      if (f.sky && portal::GetSwapperHalf(*f.sky) == half) return f.sky;
    }
    return nullptr;
  }

  std::vector<Choice> ChoicesFor(const Character& character) const {
    std::vector<Choice> choices;
    for (const Version& v : character.versions) {
      if (!v.bottom) {
        choices.push_back({portal::DisplayName(v.sky->name), portal::VersionBadge(*v.sky), {},
                           {v.sky}});
        continue;
      }
      const std::string base = portal::DisplayName(portal::SwapperBaseName(*v.sky));
      const std::string badge = portal::VersionBadge(*v.sky);
      choices.push_back({base + " - " + T(Text::kWhole), badge, {}, {v.sky, v.bottom}});
      const SkylanderInfo* kept_bottom = LiftedHalf(SwapperHalf::kBottom);
      const SkylanderInfo* kept_top = LiftedHalf(SwapperHalf::kTop);
      Choice top{base + " - " + T(Text::kTopOnly), badge, {},
                 {v.sky, kept_bottom ? kept_bottom : v.bottom}};
      if (kept_bottom) {
        top.detail = std::string(T(Text::kBottom)) + " " + T(Text::kKeeps) + ": " +
                     portal::DisplayName(kept_bottom->name);
      }
      Choice bottom{base + " - " + T(Text::kBottomOnly), badge, {},
                    {kept_top ? kept_top : v.sky, v.bottom}};
      if (kept_top) {
        bottom.detail = std::string(T(Text::kTop)) + " " + T(Text::kKeeps) + ": " +
                        portal::DisplayName(kept_top->name);
      }
      choices.push_back(std::move(top));
      choices.push_back(std::move(bottom));
    }
    return choices;
  }

  // What the preview shows: the highlighted choice, recent entry, or the character's version
  // played last (else its first).
  std::vector<const SkylanderInfo*> PreviewFigures() const {
    if (state_ == State::kChoosing && !choices_.empty()) {
      return choices_[std::min(choice_, choices_.size() - 1)].figures;
    }
    if (const RecentEntry* entry = SelectedRecent()) return entry->figures;
    if (const Character* character = SelectedCharacter()) {
      const OwnedFigure* best = library_.BestOwned(*character);
      for (const Version& v : character->versions) {
        if (best && (best->sky == v.sky || best->sky == v.bottom)) {
          return v.bottom ? std::vector<const SkylanderInfo*>{v.sky, v.bottom}
                          : std::vector<const SkylanderInfo*>{v.sky};
        }
      }
      const Version& v = character->versions.front();
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
  }

  void SwitchElement(int delta) {
    const Tab& tab = CurrentTab();
    if (tab.recent || tab.elements.empty()) return;
    element_ = (element_ + tab.elements.size() + delta) % tab.elements.size();
    selected_ = 0;
    scroll_row_ = 0;
  }

  void HandleInput(const Actions& a) {
    if (state_ == State::kDetails) {
      if (a.back || a.details) state_ = previous_state_;
      return;
    }
    if (a.details) {
      previous_state_ = state_;
      state_ = State::kDetails;
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
    if (a.lt) SwitchTab(-1);
    if (a.rt) SwitchTab(+1);
    if (a.lb) SwitchElement(-1);
    if (a.rb) SwitchElement(+1);
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
      ConfirmFiles(entry->paths, entry->figures);
      return;
    }
    const Character* character = SelectedCharacter();
    if (!character) return;
    choices_ = ChoicesFor(*character);
    if (choices_.size() == 1) {
      Confirm(choices_[0].figures);
      return;
    }
    choice_ = 0;
    state_ = State::kChoosing;
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
                figures.empty() ? std::string("?") : std::string(figures.front()->name));
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

  void DrawPanel(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const float r = S(26);
    dl->AddRectFilled({p0.x + S(8), p0.y + S(10)}, {p1.x + S(8), p1.y + S(10)}, Rgba(0, 0, 0, 80),
                      r);
    // Slightly see-through, with a magic glow from the top.
    dl->AddRectFilled(p0, p1, Rgba(16, 22, 60, 205), r);
    const ImVec2 glow{(p0.x + p1.x) / 2, p0.y + S(40)};
    for (int i = 6; i >= 1; --i) {
      const float k = float(i) / 6.0f;
      dl->AddEllipseFilled(glow, {(p1.x - p0.x) * 0.45f * k, S(220) * k},
                           IM_COL32(90, 70, 200, int(16 * (1.0f - k) + 4)));
    }
    dl->AddRect(p0, p1, kGold, r, 0, S(4));
    dl->AddRect({p0.x + S(9), p0.y + S(9)}, {p1.x - S(9), p1.y - S(9)}, kGoldDark, r - S(8), 0,
                S(1.5f));
  }

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
    x += Hint(dl, {x, y}, kb ? "Enter" : "A", Rgba(76, 175, 80), T(Text::kPlace), !kb);
    x += Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kBack), !kb);
    x += Hint(dl, {x, y}, kb ? "L" : "X", Rgba(46, 134, 222), T(Text::kDetails), !kb);
    if (state_ == State::kBrowsing) {
      x += Hint(dl, {x, y}, kb ? "Q / E" : "LT / RT", grey, T(Text::kGame), false);
      if (!CurrentTab().recent) {
        Hint(dl, {x, y}, kb ? "1 / 3" : "LB / RB", grey, T(Text::kElement), false);
      }
    }
  }

  void DrawBody(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const float preview_w = (p1.x - p0.x) * 0.27f;
    const ImVec2 prev0{p0.x + S(38), p0.y + S(100)};
    const ImVec2 prev1{prev0.x + preview_w, p1.y - S(86)};
    const ImVec2 grid0{prev1.x + S(36), p0.y + S(100)};
    const ImVec2 grid1{p1.x - S(38), p1.y - S(86)};

    // Game tabs.
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
      DrawElementRow(dl, area0, grid1.x, tab);
      area0.y += S(74);
    }
    DrawPreview(dl, prev0, prev1, PreviewFigures());

    const size_t count = ItemCount();
    if (!count) {
      Label(dl, BoldFont(), S(26), {area0.x, area0.y + S(10)}, kDim, T(Text::kNothingRecent));
      columns_ = 1;
      return;
    }
    const float gap = S(16);
    const float card_w = S(226), card_h = S(126);
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
        DrawCharacterCard(dl, a, b, library_.characters()[tab.by_element[element_][i]],
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

    if (state_ == State::kChoosing) DrawChoices(dl, area0, grid1);
    if (state_ == State::kDetails) DrawDetails(dl, area0, grid1);
  }

  void DrawElementRow(ImDrawList* dl, ImVec2 a, float right, const Tab& tab) {
    float x = a.x;
    const float h = S(56);
    for (size_t e = 0; e < tab.elements.size(); ++e) {
      const Element element = tab.elements[e];
      const std::string_view label = ElementLabel(element);
      const bool active = e == element_;
      const float w = S(64) + TextSize(BoldFont(), S(20), label).x;
      if (x + w > right) break;
      const ImVec2 b{x + w, a.y + h};
      dl->AddRectFilled({x, a.y}, b, active ? ElementColor(element, 1.0f, 255)
                                            : ElementColor(element, 0.45f, 200),
                        S(28));
      if (active) dl->AddRect({x, a.y}, b, kGold, S(28), 0, S(3));
      DrawElementIcon(dl, {x + S(28), a.y + h / 2}, S(16), element,
                      active ? kInk : ElementColor(element, 1.3f, 255));
      Label(dl, BoldFont(), S(20), {x + S(52), a.y + S(15)}, active ? kInk : kWhite, label);
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
    dl->AddRectFilledMultiColor(a, b, ElementColor(element, 0.62f, 255),
                                ElementColor(element, 0.62f, 255), ElementColor(element, 0.30f, 255),
                                ElementColor(element, 0.30f, 255));
    // Rounded corners over the gradient.
    dl->AddRect(a, b, Rgba(10, 14, 40, 255), S(14), 0, S(6));
    // The element as a faded watermark.
    DrawElementIcon(dl, {b.x - S(42), a.y + (b.y - a.y) * 0.52f}, S(46), element,
                    IM_COL32(255, 255, 255, 34));
    dl->AddRect(a, b, selected ? kGold : Rgba(255, 255, 255, 46), S(12), 0,
                selected ? S(4) : S(1.5f));
  }

  void DrawCharacterCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const Character& character,
                         bool selected) {
    DrawCardFrame(dl, a, b, character.element, selected);
    Label(dl, BoldFont(), S(24), {a.x + S(14), a.y + S(12)}, kWhite, character.name,
          b.x - a.x - S(60));
    if (character.swapper) {
      const ImVec2 ts = TextSize(BoldFont(), S(15), "SWAP");
      dl->AddRectFilled({a.x + S(14), b.y - S(58)}, {a.x + S(26) + ts.x, b.y - S(36)}, kGold, S(6));
      Label(dl, BoldFont(), S(15), {a.x + S(20), b.y - S(57)}, kInk, "SWAP");
    }
    std::string info = std::to_string(character.versions.size()) + " " + T(Text::kVersions);
    const OwnedFigure* owned = library_.BestOwned(character);
    if (owned && owned->stats) info += "   Lv " + std::to_string(owned->stats->level);
    Label(dl, BoldFont(), S(17), {a.x + S(14), b.y - S(28)},
          owned ? kGold : Rgba(210, 215, 235, 190), info);
  }

  std::string ComboName(const std::vector<const SkylanderInfo*>& figures) const {
    if (figures.size() == 2 && portal::SwapperPartner(*figures[0]) != figures[1]) {
      return portal::DisplayName(portal::SwapperBaseName(*figures[0])) + " / " +
             portal::DisplayName(portal::SwapperBaseName(*figures[1]));
    }
    return portal::DisplayName(figures.size() == 2 ? portal::SwapperBaseName(*figures[0])
                                                   : figures[0]->name);
  }

  void DrawRecentCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const RecentEntry& entry,
                      bool selected) {
    const Element element = portal::FigureElement(*entry.figures[0]);
    DrawCardFrame(dl, a, b, element, selected);
    Label(dl, BoldFont(), S(22), {a.x + S(14), a.y + S(12)}, kWhite, ComboName(entry.figures),
          b.x - a.x - S(60));
    std::string info(GameShortName(entry.figures[0]->game));
    const OwnedFigure* owned = library_.OwnedAt(entry.paths[0]);
    if (owned && owned->stats) info += "   Lv " + std::to_string(owned->stats->level);
    Label(dl, BoldFont(), S(17), {a.x + S(14), b.y - S(28)}, kGold, info);
  }

  void DrawPreview(ImDrawList* dl, ImVec2 a, ImVec2 b,
                   const std::vector<const SkylanderInfo*>& figures) {
    if (figures.empty()) {
      dl->AddRectFilled(a, b, Rgba(255, 255, 255, 12), S(20));
      return;
    }
    const SkylanderInfo* main = figures[0];
    const Element element = portal::FigureElement(*main);
    dl->AddRectFilledMultiColor(a, b, ElementColor(element, 0.50f, 245),
                                ElementColor(element, 0.50f, 245), ElementColor(element, 0.18f, 245),
                                ElementColor(element, 0.18f, 245));
    dl->AddRect(a, b, Rgba(16, 22, 60, 255), S(20), 0, S(8));
    // Element band with the symbol.
    const float band = S(58);
    dl->AddRectFilled(a, {b.x, a.y + band}, ElementColor(element), S(20),
                      ImDrawFlags_RoundCornersTop);
    DrawElementIcon(dl, {a.x + S(36), a.y + band / 2}, S(19), element, kInk);
    Label(dl, BoldFont(), S(28), {a.x + S(64), a.y + S(12)}, kInk, ElementLabel(element));
    // Until there are portraits: the element symbol stands on the portal.
    const ImVec2 portal_c{(a.x + b.x) / 2, b.y - S(70)};
    DrawPortalGlow(dl, portal_c, b.x - a.x);
    DrawElementIcon(dl, {portal_c.x, portal_c.y - S(92)}, S(78), element,
                    ElementColor(element, 1.25f, 90));

    float y = a.y + band + S(22);
    const float wrap = b.x - a.x - S(44);
    const std::string name = ComboName(figures);
    OutlinedLabel(dl, TitleFont(), S(40), {a.x + S(22), y}, kWhite, name, S(2));
    y += TextSize(TitleFont(), S(40), name, wrap).y + S(8);
    // Badges.
    float bx = a.x + S(22);
    for (const std::string& text : {BadgeLabel(portal::VersionBadge(*main)),
                                    std::string(GameShortName(main->game))}) {
      const ImVec2 ts = TextSize(BoldFont(), S(18), text);
      dl->AddRectFilled({bx, y}, {bx + ts.x + S(20), y + S(30)}, Rgba(255, 255, 255, 40), S(15));
      Label(dl, BoldFont(), S(18), {bx + S(10), y + S(4)}, kWhite, text);
      bx += ts.x + S(30);
    }
    y += S(46);

    if (figures.size() == 2) {
      for (int i = 0; i < 2; ++i) {
        const SkylanderInfo* half = figures[i];
        const float bh = S(66);
        dl->AddRectFilled({a.x + S(22), y}, {b.x - S(22), y + bh},
                          ElementColor(portal::FigureElement(*half), 0.62f, 240), S(12));
        Label(dl, BoldFont(), S(17), {a.x + S(36), y + S(7)}, Rgba(232, 232, 245),
              T(i == 0 ? Text::kTop : Text::kBottom));
        Label(dl, BoldFont(), S(23), {a.x + S(36), y + S(30)}, kWhite,
              portal::DisplayName(half->name), wrap - S(28));
        y += bh + S(10);
      }
      y += S(6);
    }

    const OwnedFigure* owned = library_.Owned(main);
    if (owned) {
      Label(dl, BoldFont(), S(22), {a.x + S(22), y}, kGold, T(Text::kYourFigure));
      y += S(32);
      if (owned->stats) {
        std::string stats = "Level " + std::to_string(owned->stats->level) + "   " +
                            std::to_string(owned->stats->gold) + " Gold";
        Label(dl, BoldFont(), S(24), {a.x + S(22), y}, kWhite, stats);
        y += S(34);
        if (!owned->stats->nickname.empty()) {
          Label(dl, BoldFont(), S(22), {a.x + S(22), y}, kDim,
                "\"" + owned->stats->nickname + "\"", wrap);
        }
      }
    } else {
      Label(dl, BoldFont(), S(20), {a.x + S(22), y}, kDim, T(Text::kNewFigure), wrap);
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
      if (MouseSelects(r0, r1)) choice_ = i;
      if (ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseClicked(0)) {
        Confirm(choice.figures);
        return;
      }
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      Label(dl, BoldFont(), S(23), {r0.x + S(18), r0.y + S(8)}, kWhite, choice.label);
      std::string sub = BadgeLabel(choice.badge);
      if (const OwnedFigure* owned = library_.Owned(choice.figures[0]); owned && owned->stats) {
        sub += "   Lv " + std::to_string(owned->stats->level);
      }
      if (!choice.detail.empty()) sub += "   " + choice.detail;
      Label(dl, BoldFont(), S(17), {r0.x + S(18), r0.y + S(39)}, kDim, sub);
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
      DrawElementIcon(dl, {a.x + S(52), y + S(24)}, S(22), element, ElementColor(element));
      Label(dl, BoldFont(), S(26), {a.x + S(88), y + S(6)}, kWhite, portal::DisplayName(sky->name));
      y += S(48);
      std::string line = BadgeLabel(portal::VersionBadge(*sky)) + "   " + std::string(sky->game) +
                         "   " + std::string(ElementLabel(element));
      Label(dl, BoldFont(), S(19), {a.x + S(88), y}, kDim, line);
      y += S(32);
      if (const OwnedFigure* owned = library_.Owned(sky)) {
        if (owned->stats) {
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
  bool software_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  std::optional<ImVec2> mouse_start_;
  bool mouse_moved_ = false;
  State state_ = State::kBrowsing;
  State previous_state_ = State::kBrowsing;
  Library library_;
  std::vector<LiftedFigure> lifted_;
  Clock::time_point lift_time_ = Clock::now();
  std::vector<std::filesystem::path> pending_paths_;
  bool restore_ = false;
  size_t tab_ = 0;
  size_t element_ = 0;
  size_t selected_ = 0;
  size_t scroll_row_ = 0;
  size_t columns_ = 1;
  std::vector<Choice> choices_;
  size_t choice_ = 0;
  size_t choice_scroll_ = 0;
  PadReader pad_reader_;
};

// UI thread only.
std::unique_ptr<PortalMenuDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

void OpenMenu(DeviceId device, uint32_t user, bool keyboard) {
  if (g_menu || !g_drawer) return;
  g_menu = std::make_unique<PortalMenuDialog>(
      g_drawer,
      [] {
        // The dialog finishes inside its own OnDraw; destroy it afterwards.
        g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
      },
      device, user, keyboard);
}

}  // namespace

void AddPortalMenuFonts(ImFontAtlas* atlas) {
#ifdef _WIN32
  g_font_title = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguibl.ttf", kTitleBakeSize);
  g_font_bold = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", kBoldBakeSize);
#else
  (void)atlas;
#endif
}

void RegisterPortalMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context) {
  g_drawer = drawer;
  g_app_context = app_context;
  menu_input::SetOpenHandler([](DeviceId device, uint32_t user) {
    if (!g_app_context) return;
    g_app_context->CallInUIThread([device, user] { OpenMenu(device, user, false); });
  });
  rex::ui::RegisterBind("bind_portal_menu", "F6",
                        "Open or close the portal menu (player 1, keyboard)", [] {
                          if (g_menu) {
                            g_menu->Cancel();
                          } else if (auto keyboard = menu_input::KeyboardDevice()) {
                            OpenMenu(*keyboard, 0, true);
                          } else {
                            OpenMenu(DeviceId::kInvalid, 0, true);
                          }
                        });
}

}  // namespace skylanders
