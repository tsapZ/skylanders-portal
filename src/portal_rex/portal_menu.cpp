#include "portal_rex/portal_menu.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cfloat>
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
REXCVAR_DEFINE_STRING(portal_menu_view, "cards", "Portal",
                      "Portal menu view: cards, or names for a compact list")
    .allowed({"cards", "names"});

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
  kHalf,
  kGame,
  kView,
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
  kChooseHalf,
  kKeeps,
  kCount,
};

constexpr const char* kEnglish[] = {
    "PORTAL OF POWER",
    "Player %u",
    "Recent",
    "Place",
    "Back",
    "Half",
    "Game",
    "View",
    "Whole Skylander",
    "Top half only",
    "Bottom half only",
    "Top",
    "Bottom",
    "New figure, created when placed",
    "Your figure",
    "The real USB portal is active. Put your figures on it.",
    "No portal is active (portal_mode = none).",
    "Nothing used yet.",
    "What goes on the portal?",
    "keeps",
};
constexpr const char* kGerman[] = {
    "PORTAL OF POWER",
    "Spieler %u",
    "Zuletzt",
    "Auflegen",
    "Zur\xC3\xBC"
    "ck",
    "H\xC3\xA4lfte",
    "Spiel",
    "Ansicht",
    "Ganzer Skylander",
    "Nur obere H\xC3\xA4lfte",
    "Nur untere H\xC3\xA4lfte",
    "Oben",
    "Unten",
    "Neue Figur, wird beim Auflegen angelegt",
    "Deine Figur",
    "Das echte USB-Portal ist aktiv. Stell deine Figuren direkt aufs Portal.",
    "Kein Portal aktiv (portal_mode = none).",
    "Noch nichts benutzt.",
    "Was soll aufs Portal?",
    "bleibt",
};
static_assert(std::size(kEnglish) == size_t(Text::kCount));
static_assert(std::size(kGerman) == size_t(Text::kCount));

bool g_german = false;

const char* T(Text text) { return (g_german ? kGerman : kEnglish)[size_t(text)]; }

// Element names as the German game shows them; English from the catalog otherwise.
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
      return {0.56f, 0.27f, 0.78f, 1};
    case Element::kEarth:
      return {0.69f, 0.48f, 0.24f, 1};
    case Element::kWater:
      return {0.18f, 0.53f, 0.87f, 1};
    case Element::kFire:
      return {0.91f, 0.29f, 0.17f, 1};
    case Element::kTech:
      return {0.95f, 0.64f, 0.11f, 1};
    case Element::kUndead:
      return {0.45f, 0.42f, 0.58f, 1};
    case Element::kLife:
      return {0.30f, 0.69f, 0.31f, 1};
    case Element::kAir:
      return {0.43f, 0.78f, 0.94f, 1};
    case Element::kLight:
      return {0.97f, 0.89f, 0.49f, 1};
    case Element::kDark:
      return {0.27f, 0.20f, 0.43f, 1};
    case Element::kNone:
      break;
  }
  return {0.47f, 0.47f, 0.51f, 1};
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

const ImU32 kGold = Rgba(240, 192, 80);
const ImU32 kWhite = Rgba(255, 255, 255);
const ImU32 kDim = Rgba(200, 210, 235);

std::string_view GameShortName(std::string_view game) {
  if (game == "Spyro's Adventure") return "SSA";
  if (game == "Swap Force") return "SWAP Force";
  return game;
}

// --- Figure library ---------------------------------------------------------------------------

uint32_t Key(uint16_t id, uint16_t variant) { return (uint32_t(id) << 16) | variant; }
uint32_t Key(const SkylanderInfo& sky) { return Key(sky.id, sky.variant); }

std::filesystem::path Utf8Path(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::filesystem::path FiguresDir() { return Utf8Path(REXCVAR_GET(portal_figures_dir)); }

struct OwnedFigure {
  std::filesystem::path path;
  std::optional<portal::FigureStats> stats;
  std::filesystem::file_time_type mtime{};
};

struct Card {
  const SkylanderInfo* sky = nullptr;     // the figure, or the top half of a swapper
  const SkylanderInfo* bottom = nullptr;  // the bottom half of a swapper
  std::string name;
  Element element = Element::kNone;
  const OwnedFigure* owned = nullptr;
  const OwnedFigure* owned_bottom = nullptr;
  bool swapper() const { return bottom != nullptr; }
  bool any_owned() const { return owned || owned_bottom; }
};

struct Tab {
  std::string title;
  std::vector<Card> cards;
};

// Recently placed figures: one line per placement, "id:variant" or "top+bottom" for a swapper.
std::filesystem::path RecentFile() { return FiguresDir() / "portal_menu_recent.txt"; }

std::vector<std::vector<uint32_t>> LoadRecent() {
  std::vector<std::vector<uint32_t>> recent;
  std::ifstream in(RecentFile());
  std::string line;
  while (std::getline(in, line)) {
    std::vector<uint32_t> keys;
    std::stringstream parts(line);
    std::string part;
    while (std::getline(parts, part, '+')) {
      const size_t colon = part.find(':');
      uint16_t id = 0, variant = 0;
      if (colon != std::string::npos &&
          std::from_chars(part.data(), part.data() + colon, id).ec == std::errc() &&
          std::from_chars(part.data() + colon + 1, part.data() + part.size(), variant).ec ==
              std::errc()) {
        keys.push_back(Key(id, variant));
      }
    }
    if (!keys.empty()) {
      recent.push_back(keys);
    }
  }
  return recent;
}

void SaveRecent(const std::vector<std::vector<uint32_t>>& recent) {
  std::ofstream out(RecentFile(), std::ios::trunc);
  for (const auto& keys : recent) {
    for (size_t i = 0; i < keys.size(); ++i) {
      out << (i ? "+" : "") << (keys[i] >> 16) << ":" << (keys[i] & 0xFFFF);
    }
    out << "\n";
  }
}

void AddRecent(std::vector<uint32_t> keys) {
  auto recent = LoadRecent();
  recent.erase(std::remove(recent.begin(), recent.end(), keys), recent.end());
  recent.insert(recent.begin(), std::move(keys));
  if (recent.size() > 12) {
    recent.resize(12);
  }
  SaveRecent(recent);
}

std::vector<std::string> AllowedGames() {
  std::vector<std::string> games;
  std::stringstream list(REXCVAR_GET(portal_menu_games));
  std::string game;
  while (std::getline(list, game, ';')) {
    if (!game.empty()) {
      games.push_back(game);
    }
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

class Library {
 public:
  void Build() {
    owned_.clear();
    tabs_.clear();
    ScanOwned();
    const std::vector<std::string> games = AllowedGames();
    // Recent first.
    Tab recent{T(Text::kRecent), {}};
    for (const auto& keys : LoadRecent()) {
      if (auto card = CardForKeys(keys); card && IsAllowed(games, card->sky->game)) {
        recent.cards.push_back(*card);
      }
    }
    tabs_.push_back(std::move(recent));
    for (const std::string& game : games) {
      Tab tab{game, {}};
      std::vector<Card> rest;
      for (const SkylanderInfo& sky : portal::AllSkylanders()) {
        if (sky.game != game || !IsPlayableCategory(sky.category) ||
            portal::GetSwapperHalf(sky) == SwapperHalf::kBottom) {
          continue;
        }
        Card card = MakeCard(&sky);
        (card.any_owned() ? tab.cards : rest).push_back(std::move(card));
      }
      tab.cards.insert(tab.cards.end(), rest.begin(), rest.end());
      tabs_.push_back(std::move(tab));
    }
  }

  const std::vector<Tab>& tabs() const { return tabs_; }

  const OwnedFigure* Owned(const SkylanderInfo* sky) const {
    if (!sky) return nullptr;
    auto it = owned_.find(Key(*sky));
    return it == owned_.end() ? nullptr : &it->second;
  }

 private:
  static bool IsAllowed(const std::vector<std::string>& games, std::string_view game) {
    return std::find(games.begin(), games.end(), game) != games.end();
  }

  void ScanOwned() {
    std::error_code ec;
    for (const portal::FigureCatalogEntry& entry : portal::ScanFigureCatalog(FiguresDir())) {
      std::optional<portal::FigureData> data = portal::LoadFigureFile(entry.path);
      if (!data) continue;
      const uint32_t key = Key(portal::ReadFigureId(*data), portal::ReadFigureVariant(*data));
      OwnedFigure figure{entry.path, portal::ParseFigureStats(*data),
                         std::filesystem::last_write_time(entry.path, ec)};
      auto it = owned_.find(key);
      // With several files for one figure, use the one played last.
      if (it == owned_.end() || figure.mtime > it->second.mtime) {
        owned_[key] = std::move(figure);
      }
    }
  }

  Card MakeCard(const SkylanderInfo* sky) const {
    Card card;
    card.sky = sky;
    card.element = portal::FigureElement(*sky);
    card.owned = Owned(sky);
    if (portal::GetSwapperHalf(*sky) == SwapperHalf::kTop) {
      card.bottom = portal::SwapperPartner(*sky);
      card.owned_bottom = Owned(card.bottom);
      card.name = portal::DisplayName(portal::SwapperBaseName(*sky));
    } else {
      card.name = portal::DisplayName(sky->name);
    }
    return card;
  }

  std::optional<Card> CardForKeys(const std::vector<uint32_t>& keys) const {
    std::vector<const SkylanderInfo*> figures;
    for (uint32_t key : keys) {
      if (const SkylanderInfo* sky = portal::FindSkylander(uint16_t(key >> 16), uint16_t(key))) {
        figures.push_back(sky);
      }
    }
    if (figures.empty()) return std::nullopt;
    if (figures.size() == 1 || portal::GetSwapperHalf(*figures[0]) == SwapperHalf::kNone) {
      return MakeCard(figures[0]);
    }
    // A swapper combination: show it with the halves that were placed.
    Card card = MakeCard(figures[0]);
    card.bottom = figures[1];
    card.owned_bottom = Owned(figures[1]);
    if (portal::SwapperPartner(*figures[0]) != figures[1]) {
      card.name = portal::DisplayName(portal::SwapperBaseName(*figures[0])) + " / " +
                  portal::DisplayName(portal::SwapperBaseName(*figures[1]));
    }
    return card;
  }

  std::unordered_map<uint32_t, OwnedFigure> owned_;
  std::vector<Tab> tabs_;
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
  // Figures placed some other way (start-up figure, the developer overlay) belong to player 1.
  if (slots.empty() && user == 0) {
    for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
      if (software->HasFigure(slot) && !OwnedByOther(slot, user)) {
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
    if (PlaceFigureFromFile(slot, path)) {
      slots.push_back(slot);
    }
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
    if (placed) {
      slots.push_back(figure.slot);
    }
  }
}

// --- Menu input ---------------------------------------------------------------------------------

struct Actions {
  bool up = false, down = false, left = false, right = false;
  bool accept = false, back = false, lb = false, rb = false, lt = false, rt = false, view = false;
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
    a.view = pressed(rex::input::X_INPUT_GAMEPAD_Y);
    a.lb = pressed(rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    a.rb = pressed(rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    const bool lt = pad.left_trigger > 128, rt = pad.right_trigger > 128;
    a.lt = lt && !prev_lt_;
    a.rt = rt && !prev_rt_;
    prev_lt_ = lt;
    prev_rt_ = rt;
    // The open button again closes the menu.
    if (menu_input::IsOpenChord(buttons) && !menu_input::IsOpenChord(prev_)) {
      a.back = true;
    }
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
  // P = Y, 1/3 = LB/RB, Q/E = LT/RT.
  a.accept = once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
  a.back = once(ImGuiKey_Escape) || once(ImGuiKey_Backspace);
  a.view = once(ImGuiKey_P);
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
  into.lb |= from.lb;
  into.rb |= from.rb;
  into.lt |= from.lt;
  into.rt |= from.rt;
  into.view |= from.view;
}

// --- Drawing helpers ----------------------------------------------------------------------------

float g_s = 1.0f;  // pixels per 1080p unit
float S(float v) { return v * g_s; }

void Label(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, std::string_view text,
           float wrap = 0.0f) {
  dl->AddText(font, size, pos, col, text.data(), text.data() + text.size(), wrap);
}

ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap = 0.0f) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
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

// --- The dialog ---------------------------------------------------------------------------------

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
    names_view_ = REXCVAR_GET(portal_menu_view) == "names";
    software_ = GetSoftwarePortal() != nullptr;
    if (software_) {
      library_.Build();
      lifted_ = LiftPlayer(user_);
      lift_time_ = Clock::now();
      // Start on the game of the lifted figure, else on the recent list (or the first game).
      tab_ = library_.tabs().size() > 1 && library_.tabs()[0].cards.empty() ? 1 : 0;
      for (const LiftedFigure& figure : lifted_) {
        if (!figure.sky) continue;
        for (size_t t = 1; t < library_.tabs().size(); ++t) {
          if (library_.tabs()[t].title == figure.sky->game) {
            tab_ = t;
            SelectFigure(figure.sky);
          }
        }
        break;
      }
    }
    REXLOG_INFO("Portal menu: opened for player {}", user_ + 1);
  }

  ~PortalMenuDialog() override {
    if (state_ != State::kDone) {
      menu_input::SetMenuOpen(false);
    }
  }

  // Closes like the Back button: the lifted figure goes back on the portal.
  void Cancel() {
    if (state_ == State::kBrowsing || state_ == State::kChoosingHalf ||
        state_ == State::kMessage) {
      StartCancel();
    }
  }

  uint32_t user() const { return user_; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    g_s = io.DisplaySize.y / 1080.0f;
    if (!mouse_start_) {
      mouse_start_ = io.MousePos;
    } else if (!mouse_moved_ && ImGui::IsMousePosValid(&io.MousePos) &&
               ImGui::IsMousePosValid(&*mouse_start_)) {
      const float dx = io.MousePos.x - mouse_start_->x, dy = io.MousePos.y - mouse_start_->y;
      mouse_moved_ = dx * dx + dy * dy > S(12) * S(12);
    }
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
    if (state_ == State::kDone) {
      return;
    }

    Actions actions = ReadActions();
    if (!software_) {
      state_ = State::kMessage;
    }

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
    // The game stays visible, dimmed, around the panel.
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 28, 150));
    const ImVec2 p0{W * 0.06f, H * 0.07f}, p1{W * 0.94f, H * 0.93f};
    dl->AddRectFilled({p0.x + S(6), p0.y + S(8)}, {p1.x + S(6), p1.y + S(8)}, Rgba(0, 0, 0, 90),
                      S(22));
    dl->AddRectFilled(p0, p1, Rgba(18, 26, 62, 238), S(22));
    dl->AddRect(p0, p1, kGold, S(22), 0, S(3));

    DrawHeader(dl, p0, p1);
    if (state_ == State::kMessage) {
      DrawMessage(dl, p0, p1, actions);
    } else {
      HandleBrowse(actions);
      DrawBody(dl, io, p0, p1);
    }
    DrawFooter(dl, p0, p1);
    ImGui::End();
  }

 private:
  enum class State { kBrowsing, kChoosingHalf, kMessage, kWaiting, kDone };
  enum class HalfChoice { kWhole, kTopOnly, kBottomOnly };

  Actions ReadActions() {
    Actions actions;
    std::optional<rex::input::X_INPUT_GAMEPAD> pad;
    if (!keyboard_) {
      pad = menu_input::ReadPad(device_);
    }
    if (waiting_for_release_) {
      const bool pad_released = !pad || pad_reader_.Released(*pad);
      const bool keys_released =
          !ImGui::IsKeyDown(ImGuiKey_F6) && !ImGui::IsKeyDown(ImGuiKey_Enter);
      if (pad_released && keys_released) {
        waiting_for_release_ = false;
      }
      if (pad) pad_reader_.Read(*pad);
      return actions;
    }
    if (pad) {
      Merge(actions, pad_reader_.Read(*pad));
    }
    Merge(actions, ReadKeyboard());
    return actions;
  }

  const Tab& CurrentTab() const { return library_.tabs()[tab_]; }

  const Card* SelectedCard() const {
    const Tab& tab = CurrentTab();
    if (tab.cards.empty()) return nullptr;
    return &tab.cards[std::min(selected_, tab.cards.size() - 1)];
  }

  void SelectFigure(const SkylanderInfo* sky) {
    const Tab& tab = CurrentTab();
    for (size_t i = 0; i < tab.cards.size(); ++i) {
      if (tab.cards[i].sky == sky || tab.cards[i].bottom == sky) {
        selected_ = i;
        return;
      }
    }
  }

  const SkylanderInfo* LiftedHalf(SwapperHalf half) const {
    for (const LiftedFigure& f : lifted_) {
      if (f.sky && portal::GetSwapperHalf(*f.sky) == half) return f.sky;
    }
    return nullptr;
  }

  // The figures a choice puts on the portal: one, or a swapper's top and bottom.
  std::vector<const SkylanderInfo*> Composition(const Card& card, HalfChoice choice) const {
    if (!card.swapper()) return {card.sky};
    const SkylanderInfo* top = card.sky;
    const SkylanderInfo* bottom = card.bottom;
    if (choice == HalfChoice::kTopOnly) {
      if (const SkylanderInfo* kept = LiftedHalf(SwapperHalf::kBottom)) bottom = kept;
    } else if (choice == HalfChoice::kBottomOnly) {
      if (const SkylanderInfo* kept = LiftedHalf(SwapperHalf::kTop)) top = kept;
    }
    return {top, bottom};
  }

  void HandleBrowse(const Actions& a) {
    const size_t tab_count = library_.tabs().size();
    if (state_ == State::kChoosingHalf) {
      if (a.up) half_choice_ = (half_choice_ + 2) % 3;
      if (a.down) half_choice_ = (half_choice_ + 1) % 3;
      if (a.back) state_ = State::kBrowsing;
      if (a.accept) {
        if (const Card* card = SelectedCard()) {
          Confirm(Composition(*card, HalfChoice(half_choice_)));
        }
      }
      return;
    }
    if (a.back) {
      StartCancel();
      return;
    }
    if (a.lt && tab_count) {
      tab_ = (tab_ + tab_count - 1) % tab_count;
      selected_ = 0;
      scroll_row_ = 0;
    }
    if (a.rt && tab_count) {
      tab_ = (tab_ + 1) % tab_count;
      selected_ = 0;
      scroll_row_ = 0;
    }
    if (a.view) {
      names_view_ = !names_view_;
      rex::cvar::SetFlagByName("portal_menu_view", names_view_ ? "names" : "cards");
    }
    const size_t count = CurrentTab().cards.size();
    if (count) {
      const size_t cols = std::max<size_t>(1, columns_);
      if (a.left && selected_ > 0) --selected_;
      if (a.right && selected_ + 1 < count) ++selected_;
      if (a.up && selected_ >= cols) selected_ -= cols;
      if (a.down && selected_ + cols < count) selected_ += cols;
      if (a.down && selected_ + cols >= count && selected_ / cols < (count - 1) / cols) {
        selected_ = count - 1;
      }
      const Card* card = SelectedCard();
      if (card && card->swapper()) {
        if (a.lb) Confirm(Composition(*card, HalfChoice::kTopOnly));
        else if (a.rb) Confirm(Composition(*card, HalfChoice::kBottomOnly));
        else if (a.accept) {
          state_ = State::kChoosingHalf;
          half_choice_ = 0;
        }
      } else if (card && a.accept) {
        Confirm(Composition(*card, HalfChoice::kWhole));
      }
    }
  }

  void Confirm(const std::vector<const SkylanderInfo*>& figures) {
    std::vector<std::filesystem::path> paths;
    std::vector<uint32_t> keys;
    for (const SkylanderInfo* sky : figures) {
      if (!sky) continue;
      if (const OwnedFigure* owned = library_.Owned(sky)) {
        paths.push_back(owned->path);
      } else if (auto created = CreateFigureFile(*sky)) {
        paths.push_back(*created);
      } else {
        REXLOG_WARN("Portal menu: could not create a figure file for {}", sky->name);
        continue;
      }
      keys.push_back(Key(*sky));
    }
    if (paths.empty()) return;
    AddRecent(keys);
    pending_paths_ = std::move(paths);
    restore_ = false;
    state_ = State::kWaiting;
    REXLOG_INFO("Portal menu: player {} places {}", user_ + 1, figures.front()->name);
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
    Label(dl, TitleFont(), S(44), {p0.x + S(34), p0.y + S(18)}, kGold, T(Text::kTitle));
    char player[32];
    std::snprintf(player, sizeof(player), T(Text::kPlayer), user_ + 1);
    const ImVec2 ts = TextSize(BoldFont(), S(24), player);
    const ImVec2 c1{p1.x - S(34), p0.y + S(24)};
    const ImVec2 c0{c1.x - ts.x - S(36), c1.y};
    dl->AddRectFilled(c0, {c1.x, c1.y + S(42)}, PlayerColor(user_), S(21));
    Label(dl, BoldFont(), S(24), {c0.x + S(18), c0.y + S(7)}, kWhite, player);
  }

  void DrawMessage(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const Actions& a) {
    const char* text = GetUsbPortal() || REXCVAR_GET(portal_mode) == "usb" ? T(Text::kUsbActive)
                                                                           : T(Text::kNoPortal);
    Label(dl, BoldFont(), S(30), {p0.x + S(60), (p0.y + p1.y) / 2 - S(30)}, kWhite, text,
         p1.x - p0.x - S(120));
    if (a.back || a.accept) {
      Finish();
    }
  }

  void DrawFooter(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const bool kb = keyboard_;
    float x = p0.x + S(34);
    const float y = p1.y - S(52);
    if (state_ == State::kMessage) {
      Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kBack), !kb);
      return;
    }
    x += Hint(dl, {x, y}, kb ? "Enter" : "A", Rgba(76, 175, 80), T(Text::kPlace), !kb);
    x += Hint(dl, {x, y}, kb ? "Esc" : "B", Rgba(232, 73, 43), T(Text::kBack), !kb);
    x += Hint(dl, {x, y}, kb ? "1 / 3" : "LB / RB", Rgba(90, 96, 120), T(Text::kHalf), false);
    x += Hint(dl, {x, y}, kb ? "Q / E" : "LT / RT", Rgba(90, 96, 120), T(Text::kGame), false);
    Hint(dl, {x, y}, kb ? "P" : "Y", Rgba(230, 180, 40), T(Text::kView), !kb);
  }

  void DrawBody(ImDrawList* dl, ImGuiIO& io, ImVec2 p0, ImVec2 p1) {
    const float preview_w = (p1.x - p0.x) * 0.27f;
    const ImVec2 prev0{p0.x + S(34), p0.y + S(96)};
    const ImVec2 prev1{prev0.x + preview_w, p1.y - S(82)};
    const ImVec2 grid0{prev1.x + S(36), p0.y + S(96)};
    const ImVec2 grid1{p1.x - S(34), p1.y - S(82)};

    // Tabs.
    float tx = grid0.x;
    const float tab_h = S(44);
    for (size_t t = 0; t < library_.tabs().size(); ++t) {
      const std::string& title = library_.tabs()[t].title;
      const ImVec2 ts = TextSize(BoldFont(), S(22), title);
      const ImVec2 a{tx, grid0.y}, b{tx + ts.x + S(36), grid0.y + tab_h};
      const bool active = t == tab_;
      dl->AddRectFilled(a, b, active ? kGold : Rgba(255, 255, 255, 28), S(22));
      Label(dl, BoldFont(), S(22), {a.x + S(18), a.y + S(9)}, active ? Rgba(30, 24, 10) : kDim,
           title);
      if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsMouseClicked(0)) {
        tab_ = t;
        selected_ = 0;
        scroll_row_ = 0;
      }
      tx = b.x + S(12);
    }

    const Card* selected = SelectedCard();
    DrawPreview(dl, prev0, prev1, selected);

    const ImVec2 area0{grid0.x, grid0.y + tab_h + S(22)};
    const ImVec2 area1 = grid1;
    const Tab& tab = CurrentTab();
    if (tab.cards.empty()) {
      Label(dl, BoldFont(), S(26), {area0.x, area0.y + S(10)}, kDim, T(Text::kNothingRecent));
      columns_ = 1;
      return;
    }
    const float gap = S(14);
    const float card_w = names_view_ ? S(300) : S(214);
    const float card_h = names_view_ ? S(48) : S(118);
    columns_ = std::max<size_t>(1, size_t((area1.x - area0.x + gap) / (card_w + gap)));
    const size_t rows_visible =
        std::max<size_t>(1, size_t((area1.y - area0.y + gap) / (card_h + gap)));
    const size_t sel_row = selected_ / columns_;
    if (sel_row < scroll_row_) scroll_row_ = sel_row;
    if (sel_row >= scroll_row_ + rows_visible) scroll_row_ = sel_row - rows_visible + 1;

    dl->PushClipRect(area0, area1, true);
    for (size_t i = scroll_row_ * columns_;
         i < tab.cards.size() && i < (scroll_row_ + rows_visible) * columns_; ++i) {
      const size_t row = i / columns_ - scroll_row_, col = i % columns_;
      const ImVec2 a{area0.x + col * (card_w + gap), area0.y + row * (card_h + gap)};
      const ImVec2 b{a.x + card_w, a.y + card_h};
      // The cursor may rest anywhere when the menu opens; it only selects once it was moved.
      if (mouse_moved_ && ImGui::IsMouseHoveringRect(a, b) &&
          (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        selected_ = i;
      }
      if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsMouseClicked(0)) {
        selected_ = i;
        Actions click;
        click.accept = true;
        HandleBrowse(click);
      }
      if (names_view_) {
        DrawNameRow(dl, a, b, tab.cards[i], i == selected_);
      } else {
        DrawCard(dl, a, b, tab.cards[i], i == selected_);
      }
    }
    dl->PopClipRect();

    // Scroll indicator.
    const size_t total_rows = (tab.cards.size() + columns_ - 1) / columns_;
    if (total_rows > rows_visible) {
      const float track_h = area1.y - area0.y;
      const float thumb_h = track_h * float(rows_visible) / float(total_rows);
      const float thumb_y = area0.y + track_h * float(scroll_row_) / float(total_rows);
      dl->AddRectFilled({area1.x + S(10), thumb_y}, {area1.x + S(16), thumb_y + thumb_h},
                        Rgba(255, 255, 255, 90), S(3));
    }

    if (state_ == State::kChoosingHalf && selected) {
      DrawHalfChoice(dl, area0, area1, *selected);
    }
  }

  void DrawCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const Card& card, bool selected) {
    if (selected) {
      dl->AddRectFilled({a.x - S(5), a.y - S(5)}, {b.x + S(5), b.y + S(5)}, Rgba(240, 192, 80, 90),
                        S(16));
    }
    dl->AddRectFilled(a, b, ElementColor(card.element, 0.45f, 245), S(12));
    const float band = S(28);
    if (card.swapper() && portal::FigureElement(*card.bottom) != card.element) {
      const Element bottom = portal::FigureElement(*card.bottom);
      const float mid = (a.x + b.x) / 2;
      dl->AddRectFilled(a, {mid, a.y + band}, ElementColor(card.element), S(12),
                        ImDrawFlags_RoundCornersTopLeft);
      dl->AddRectFilled({mid, a.y}, {b.x, a.y + band}, ElementColor(bottom), S(12),
                        ImDrawFlags_RoundCornersTopRight);
    } else {
      dl->AddRectFilled(a, {b.x, a.y + band}, ElementColor(card.element), S(12),
                        ImDrawFlags_RoundCornersTop);
    }
    Label(dl, BoldFont(), S(17), {a.x + S(10), a.y + S(4)}, Rgba(20, 20, 30),
          ElementLabel(card.element));
    if (card.swapper()) {
      const ImVec2 ts = TextSize(BoldFont(), S(15), "SWAP");
      Label(dl, BoldFont(), S(15), {b.x - ts.x - S(10), a.y + S(5)}, Rgba(20, 20, 30), "SWAP");
    }
    Label(dl, BoldFont(), S(22), {a.x + S(10), a.y + band + S(6)}, kWhite, card.name,
         b.x - a.x - S(20));
    std::string info(GameShortName(card.sky->game));
    if (card.owned && card.owned->stats) {
      info += "   Lv " + std::to_string(card.owned->stats->level);
    }
    Label(dl, BoldFont(), S(16), {a.x + S(10), b.y - S(26)},
         card.any_owned() ? kGold : Rgba(200, 205, 225, 170), info);
    dl->AddRect(a, b, selected ? kGold : Rgba(255, 255, 255, 40), S(12), 0,
                selected ? S(4) : S(1.5f));
  }

  void DrawNameRow(ImDrawList* dl, ImVec2 a, ImVec2 b, const Card& card, bool selected) {
    dl->AddRectFilled(a, b, selected ? Rgba(255, 255, 255, 46) : Rgba(255, 255, 255, 14), S(24));
    dl->AddCircleFilled({a.x + S(24), (a.y + b.y) / 2}, S(9), ElementColor(card.element));
    Label(dl, BoldFont(), S(22), {a.x + S(44), a.y + S(11)}, card.any_owned() ? kGold : kWhite,
         card.name);
    if (selected) dl->AddRect(a, b, kGold, S(24), 0, S(3));
  }

  void DrawPreview(ImDrawList* dl, ImVec2 a, ImVec2 b, const Card* card) {
    if (!card) {
      dl->AddRectFilled(a, b, Rgba(255, 255, 255, 12), S(18));
      return;
    }
    dl->AddRectFilled(a, b, ElementColor(card->element, 0.35f, 250), S(18));
    const float band = S(54);
    dl->AddRectFilled(a, {b.x, a.y + band}, ElementColor(card->element), S(18),
                      ImDrawFlags_RoundCornersTop);
    Label(dl, BoldFont(), S(28), {a.x + S(20), a.y + S(11)}, Rgba(20, 20, 30),
          ElementLabel(card->element));
    float y = a.y + band + S(22);
    const float wrap = b.x - a.x - S(40);
    Label(dl, TitleFont(), S(40), {a.x + S(20), y}, kWhite, card->name, wrap);
    y += TextSize(TitleFont(), S(40), card->name, wrap).y + S(10);
    std::string game = std::string(card->sky->game) + "  -  " + std::string(card->sky->category);
    Label(dl, BoldFont(), S(20), {a.x + S(20), y}, kDim, game, wrap);
    y += S(44);

    if (card->swapper()) {
      const auto parts = Composition(*card, HalfChoice::kWhole);
      for (int i = 0; i < 2; ++i) {
        const SkylanderInfo* half = parts[i];
        const float bh = S(70);
        dl->AddRectFilled({a.x + S(20), y}, {b.x - S(20), y + bh},
                          ElementColor(portal::FigureElement(*half), 0.6f, 255), S(12));
        Label(dl, BoldFont(), S(18), {a.x + S(34), y + S(8)}, Rgba(230, 230, 240),
             T(i == 0 ? Text::kTop : Text::kBottom));
        Label(dl, BoldFont(), S(24), {a.x + S(34), y + S(32)}, kWhite,
             portal::DisplayName(half->name), wrap - S(28));
        y += bh + S(12);
      }
      y += S(8);
    }

    const OwnedFigure* owned = card->owned ? card->owned : card->owned_bottom;
    if (owned) {
      Label(dl, BoldFont(), S(22), {a.x + S(20), y}, kGold, T(Text::kYourFigure));
      y += S(32);
      if (owned->stats) {
        std::string stats = "Level " + std::to_string(owned->stats->level) + "   " +
                            std::to_string(owned->stats->gold) + " Gold";
        Label(dl, BoldFont(), S(24), {a.x + S(20), y}, kWhite, stats);
        y += S(34);
        if (!owned->stats->nickname.empty()) {
          Label(dl, BoldFont(), S(22), {a.x + S(20), y}, kDim, "\"" + owned->stats->nickname + "\"",
               wrap);
        }
      }
    } else {
      Label(dl, BoldFont(), S(20), {a.x + S(20), y}, kDim, T(Text::kNewFigure), wrap);
    }
    dl->AddRect(a, b, Rgba(255, 255, 255, 60), S(18), 0, S(2));
  }

  void DrawHalfChoice(ImDrawList* dl, ImVec2 area0, ImVec2 area1, const Card& card) {
    const float w = S(560), h = S(330);
    const ImVec2 c{(area0.x + area1.x) / 2, (area0.y + area1.y) / 2};
    const ImVec2 a{c.x - w / 2, c.y - h / 2}, b{c.x + w / 2, c.y + h / 2};
    dl->AddRectFilled({area0.x - S(10), area0.y - S(10)}, {area1.x + S(10), area1.y + S(10)},
                      Rgba(6, 10, 28, 150), S(16));
    dl->AddRectFilled(a, b, Rgba(24, 34, 78, 252), S(18));
    dl->AddRect(a, b, kGold, S(18), 0, S(3));
    Label(dl, BoldFont(), S(28), {a.x + S(28), a.y + S(22)}, kGold, T(Text::kChooseHalf));
    const Text options[3] = {Text::kWhole, Text::kTopOnly, Text::kBottomOnly};
    for (int i = 0; i < 3; ++i) {
      const ImVec2 r0{a.x + S(24), a.y + S(80) + i * S(76)};
      const ImVec2 r1{b.x - S(24), r0.y + S(64)};
      const bool active = i == half_choice_;
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 50) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      Label(dl, BoldFont(), S(24), {r0.x + S(18), r0.y + S(8)}, kWhite, T(options[i]));
      // What stays on the portal with a half-only choice.
      std::string detail;
      const auto parts = Composition(card, HalfChoice(i));
      if (i == 1 && parts[1] != card.bottom) {
        detail = std::string(T(Text::kBottom)) + " " + T(Text::kKeeps) + ": " +
                 portal::DisplayName(parts[1]->name);
      } else if (i == 2 && parts[0] != card.sky) {
        detail = std::string(T(Text::kTop)) + " " + T(Text::kKeeps) + ": " +
                 portal::DisplayName(parts[0]->name);
      }
      if (!detail.empty()) {
        Label(dl, BoldFont(), S(17), {r0.x + S(18), r0.y + S(38)}, kDim, detail);
      }
    }
  }

  std::function<void()> on_done_;
  DeviceId device_;
  uint32_t user_;
  bool keyboard_;
  bool software_ = false;
  bool names_view_ = false;
  bool first_frame_ = true;
  std::optional<ImVec2> mouse_start_;
  bool mouse_moved_ = false;
  bool waiting_for_release_ = true;
  State state_ = State::kBrowsing;
  Library library_;
  std::vector<LiftedFigure> lifted_;
  Clock::time_point lift_time_ = Clock::now();
  std::vector<std::filesystem::path> pending_paths_;
  bool restore_ = false;
  size_t tab_ = 0;
  size_t selected_ = 0;
  size_t scroll_row_ = 0;
  size_t columns_ = 1;
  int half_choice_ = 0;
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
