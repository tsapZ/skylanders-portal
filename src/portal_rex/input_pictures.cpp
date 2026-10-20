#include "portal_rex/input_pictures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "portal_rex/menu_draw.h"

namespace skylanders::input_pictures {
namespace {

using namespace menu_draw;

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool Has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

// ---- The controller ---------------------------------------------------------------------------
// Drawn in a 1000 x 640 box. Xbox and Nintendo pads have the left stick up and the d-pad below it,
// PlayStation and generic pads the other way round.

constexpr float kPadWidth = 1000, kPadHeight = 640;

struct Spot {
  const char* button;
  float x, y, r;
};

bool StickUp(PadKind kind) { return kind == PadKind::kXbox || kind == PadKind::kNintendo; }

std::vector<Spot> Spots(PadKind kind) {
  const bool stick_up = StickUp(kind);
  const float lsx = stick_up ? 300 : 395, lsy = stick_up ? 255 : 405;
  const float dx = stick_up ? 395 : 300, dy = stick_up ? 405 : 255;
  const float fx = 705, fy = 255;
  return {
      {"ls", lsx, lsy, 58},      {"rs", 605, 405, 58},       {"up", dx, dy - 44, 24},
      {"down", dx, dy + 44, 24}, {"left", dx - 44, dy, 24},  {"right", dx + 44, dy, 24},
      {"y", fx, fy - 62, 31},    {"a", fx, fy + 62, 31},     {"x", fx - 62, fy, 31},
      {"b", fx + 62, fy, 31},    {"back", 428, 255, 20},     {"start", 572, 255, 20},
      {"guide", 500, 168, 34},   {"lb", 285, 122, 46},       {"rb", 715, 122, 46},
      {"lt", 275, 62, 38},       {"rt", 725, 62, 38},
  };
}

const Spot* Find(const std::vector<Spot>& spots, std::string_view button) {
  for (const Spot& s : spots) {
    if (button == s.button) return &s;
  }
  return nullptr;
}

void CenteredText(ImDrawList* dl, float size, ImVec2 c, ImU32 col, std::string_view text) {
  const ImVec2 ts = TextSize(BoldFont(), size, text);
  Label(dl, BoldFont(), size, {c.x - ts.x / 2, c.y - ts.y / 2}, col, text);
}

// A PlayStation face button's symbol.
void PsSymbol(ImDrawList* dl, std::string_view button, ImVec2 c, float r) {
  const float t = std::max(1.5f, r * 0.16f), s = r * 0.48f;
  if (button == "a") {  // cross
    const ImU32 col = Rgba(124, 178, 232);
    dl->AddLine({c.x - s, c.y - s}, {c.x + s, c.y + s}, col, t);
    dl->AddLine({c.x - s, c.y + s}, {c.x + s, c.y - s}, col, t);
  } else if (button == "b") {  // circle
    dl->AddCircle(c, s, Rgba(232, 92, 92), 0, t);
  } else if (button == "x") {  // square
    dl->AddRect({c.x - s * 0.85f, c.y - s * 0.85f}, {c.x + s * 0.85f, c.y + s * 0.85f},
                Rgba(226, 140, 200), 0, 0, t);
  } else if (button == "y") {  // triangle
    dl->AddTriangle({c.x, c.y - s}, {c.x - s * 0.95f, c.y + s * 0.7f},
                    {c.x + s * 0.95f, c.y + s * 0.7f}, Rgba(90, 200, 160), t);
  }
}

void DrawFaceButton(ImDrawList* dl, PadKind kind, std::string_view button, ImVec2 c, float r) {
  if (kind == PadKind::kXbox) {
    ImU32 col = Rgba(76, 175, 80);
    if (button == "b") col = Rgba(232, 73, 43);
    if (button == "x") col = Rgba(46, 134, 222);
    if (button == "y") col = Rgba(230, 180, 40);
    dl->AddCircleFilled(c, r, Rgba(30, 34, 52));
    dl->AddCircleFilled(c, r * 0.86f, col);
    CenteredText(dl, r * 1.05f, c, kWhite, PadButtonName(kind, button, false));
    return;
  }
  dl->AddCircleFilled(c, r, Rgba(30, 34, 52));
  dl->AddCircleFilled(c, r * 0.86f, Rgba(64, 70, 98));
  if (kind == PadKind::kPlayStation) {
    PsSymbol(dl, button, c, r);
  } else {
    CenteredText(dl, r * 1.0f, c, kWhite, PadButtonName(kind, button, false));
  }
}

}  // namespace

PadKind PadKindOf(std::string_view device_name) {
  const std::string n = Lower(device_name);
  if (Has(n, "xbox") || Has(n, "xinput") || Has(n, "x-box")) return PadKind::kXbox;
  if (Has(n, "ps3") || Has(n, "ps4") || Has(n, "ps5") || Has(n, "playstation") ||
      Has(n, "dualshock") || Has(n, "dualsense") || n == "wireless controller") {
    return PadKind::kPlayStation;
  }
  if (Has(n, "nintendo") || Has(n, "switch") || Has(n, "pro controller") || Has(n, "joy-con")) {
    return PadKind::kNintendo;
  }
  return PadKind::kGeneric;
}

const char* PadButtonName(PadKind kind, std::string_view button, bool german) {
  struct Names {
    const char* button;
    const char* xbox;
    const char* ps_en;
    const char* ps_de;
    const char* nintendo;
    const char* generic;
  };
  static const Names kNames[] = {
      {"a", "A", "Cross", "Kreuz", "B", "A"},
      {"b", "B", "Circle", "Kreis", "A", "B"},
      {"x", "X", "Square", "Quadrat", "Y", "X"},
      {"y", "Y", "Triangle", "Dreieck", "X", "Y"},
      {"lb", "LB", "L1", "L1", "L", "LB"},
      {"rb", "RB", "R1", "R1", "R", "RB"},
      {"lt", "LT", "L2", "L2", "ZL", "LT"},
      {"rt", "RT", "R2", "R2", "ZR", "RT"},
      {"ls", "L3", "L3", "L3", "L3", "L3"},
      {"rs", "R3", "R3", "R3", "R3", "R3"},
      {"back", "Back", "Share", "Share", "-", "Back"},
      {"start", "Start", "Options", "Options", "+", "Start"},
      {"guide", "Xbox", "PS", "PS", "Home", "Guide"},
  };
  for (const Names& n : kNames) {
    if (button != n.button) continue;
    switch (kind) {
      case PadKind::kXbox: return n.xbox;
      case PadKind::kPlayStation: return german ? n.ps_de : n.ps_en;
      case PadKind::kNintendo: return n.nintendo;
      case PadKind::kGeneric: return n.generic;
    }
  }
  return nullptr;
}

void DrawController(ImDrawList* dl, ImVec2 a, ImVec2 b, PadKind kind,
                    const std::vector<PadMark>& marks) {
  const float scale = std::min((b.x - a.x) / kPadWidth, (b.y - a.y) / kPadHeight);
  if (scale <= 0.05f) return;
  const ImVec2 o{(a.x + b.x - kPadWidth * scale) / 2, a.y};
  auto P = [&](float x, float y) { return ImVec2{o.x + x * scale, o.y + y * scale}; };
  auto R = [&](float r) { return r * scale; };
  const std::vector<Spot> spots = Spots(kind);
  const ImU32 body = Rgba(66, 74, 116), shade = Rgba(8, 10, 26, 150);
  const ImU32 part = Rgba(44, 50, 80);

  // Triggers and bumpers behind the body.
  for (const char* t : {"lt", "rt"}) {
    const Spot* s = Find(spots, t);
    dl->AddRectFilled(P(s->x - 58, s->y - 34), P(s->x + 58, s->y + 44), part, R(22));
    CenteredText(dl, R(30), P(s->x, s->y - 4), kDim, PadButtonName(kind, t, false));
  }
  for (const char* t : {"lb", "rb"}) {
    const Spot* s = Find(spots, t);
    dl->AddRectFilled(P(s->x - 108, s->y - 26), P(s->x + 108, s->y + 30), Rgba(54, 60, 94), R(26));
    CenteredText(dl, R(28), P(s->x, s->y - 2), kDim, PadButtonName(kind, t, false));
  }
  // The body: two grips and the middle, with a shadow below.
  for (int pass = 0; pass < 2; ++pass) {
    const float d = pass == 0 ? 12.0f : 0.0f;
    const ImU32 col = pass == 0 ? shade : body;
    dl->AddCircleFilled(P(255, 425 + d), R(140), col, 48);
    dl->AddCircleFilled(P(745, 425 + d), R(140), col, 48);
    dl->AddRectFilled(P(150, 150 + d), P(850, 430 + d), col, R(140));
  }

  // Sticks, d-pad, the middle buttons, the face buttons.
  for (const char* t : {"ls", "rs"}) {
    const Spot* s = Find(spots, t);
    dl->AddCircleFilled(P(s->x, s->y), R(s->r), Rgba(26, 30, 48), 40);
    dl->AddCircleFilled(P(s->x, s->y), R(s->r * 0.72f), Rgba(70, 78, 112), 40);
    dl->AddCircle(P(s->x, s->y), R(s->r * 0.5f), Rgba(96, 106, 150), 40, R(3));
  }
  {
    const Spot* u = Find(spots, "up");
    const float cx = u->x, cy = u->y + 44;
    dl->AddRectFilled(P(cx - 22, cy - 68), P(cx + 22, cy + 68), Rgba(30, 34, 52), R(8));
    dl->AddRectFilled(P(cx - 68, cy - 22), P(cx + 68, cy + 22), Rgba(30, 34, 52), R(8));
    const ImU32 arrow = Rgba(120, 130, 170);
    dl->AddTriangleFilled(P(cx, cy - 58), P(cx - 11, cy - 42), P(cx + 11, cy - 42), arrow);
    dl->AddTriangleFilled(P(cx, cy + 58), P(cx - 11, cy + 42), P(cx + 11, cy + 42), arrow);
    dl->AddTriangleFilled(P(cx - 58, cy), P(cx - 42, cy - 11), P(cx - 42, cy + 11), arrow);
    dl->AddTriangleFilled(P(cx + 58, cy), P(cx + 42, cy - 11), P(cx + 42, cy + 11), arrow);
  }
  for (const char* t : {"back", "start"}) {
    const Spot* s = Find(spots, t);
    dl->AddRectFilled(P(s->x - 26, s->y - 14), P(s->x + 26, s->y + 14), Rgba(30, 34, 52), R(14));
    CenteredText(dl, R(22), P(s->x, s->y + 34), kDim, PadButtonName(kind, t, false));
  }
  {
    const Spot* g = Find(spots, "guide");
    const bool xbox = kind == PadKind::kXbox;
    dl->AddCircleFilled(P(g->x, g->y), R(g->r), xbox ? Rgba(60, 150, 70) : Rgba(30, 34, 52), 40);
    CenteredText(dl, R(xbox ? 24 : 20), P(g->x, g->y), kWhite, PadButtonName(kind, "guide", false));
  }
  for (const char* t : {"a", "b", "x", "y"}) {
    const Spot* s = Find(spots, t);
    DrawFaceButton(dl, kind, t, P(s->x, s->y), R(s->r));
  }

  // The marks: a pulsing ring per mark, further out for each earlier one on the same button.
  const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 4.0f);
  for (const PadMark& m : marks) {
    const Spot* s = Find(spots, m.button);
    if (!s) continue;
    const ImVec2 c = P(s->x, s->y);
    const float r = R(s->r) + S(5) + S(7) * float(m.ring);
    if (!m.ring) dl->AddCircleFilled(c, r, (m.colour & 0x00FFFFFF) | (ImU32(50 + 50 * pulse) << 24));
    dl->AddCircle(c, r, m.colour, 0, S(3.5f));
  }
}

// ---- The keyboard -----------------------------------------------------------------------------

namespace {

struct Key {
  const char* name;  // the SDK's key name; "" for a key nothing can be bound to, nullptr: a gap
  float width;
  const char* legend = nullptr;  // printed instead of KeyLegend(name)
};
using Row = std::vector<Key>;

const std::vector<Row>& Layout(bool german) {
  static const std::vector<Row> kFunctionRow = {
      Row{{"Escape", 1}, {nullptr, 1}, {"F1", 1}, {"F2", 1}, {"F3", 1}, {"F4", 1}, {nullptr, 0.5f},
          {"F5", 1}, {"F6", 1}, {"F7", 1}, {"F8", 1}, {nullptr, 0.5f}, {"F9", 1}, {"F10", 1},
          {"F11", 1}, {"F12", 1}}};
  static const std::vector<Row> kUs = [] {
    std::vector<Row> rows = kFunctionRow;
    rows.push_back({{"Backtick", 1}, {"1", 1}, {"2", 1}, {"3", 1}, {"4", 1}, {"5", 1}, {"6", 1},
                    {"7", 1}, {"8", 1}, {"9", 1}, {"0", 1}, {"Minus", 1}, {"Plus", 1},
                    {"Backspace", 2}});
    rows.push_back({{"Tab", 1.5f}, {"Q", 1}, {"W", 1}, {"E", 1}, {"R", 1}, {"T", 1}, {"Y", 1},
                    {"U", 1}, {"I", 1}, {"O", 1}, {"P", 1}, {"LBracket", 1}, {"RBracket", 1},
                    {"Backslash", 1.5f}});
    rows.push_back({{"CapsLock", 1.75f}, {"A", 1}, {"S", 1}, {"D", 1}, {"F", 1}, {"G", 1},
                    {"H", 1}, {"J", 1}, {"K", 1}, {"L", 1}, {"Semicolon", 1}, {"Quote", 1},
                    {"Return", 2.25f}});
    rows.push_back({{"Shift", 2.25f}, {"Z", 1}, {"X", 1}, {"C", 1}, {"V", 1}, {"B", 1}, {"N", 1},
                    {"M", 1}, {"Comma", 1}, {"Period", 1}, {"Slash", 1}, {"Shift", 2.75f}});
    rows.push_back({{"Control", 1.5f}, {"Alt", 1.5f}, {"Space", 9}, {"Alt", 1.5f},
                    {"Control", 1.5f}});
    return rows;
  }();
  // German: QWERTZ; the SDK's names follow the Windows virtual keys, which German keyboards put
  // elsewhere ("Semicolon" = VK_OEM_1 is the \xC3\x9C key, "Backtick" = VK_OEM_3 the \xC3\x96 key).
  static const std::vector<Row> kDe = [] {
    std::vector<Row> rows = kFunctionRow;
    rows.push_back({{"Backslash", 1}, {"1", 1}, {"2", 1}, {"3", 1}, {"4", 1}, {"5", 1}, {"6", 1},
                    {"7", 1}, {"8", 1}, {"9", 1}, {"0", 1}, {"LBracket", 1}, {"RBracket", 1},
                    {"Backspace", 2}});
    rows.push_back({{"Tab", 1.5f}, {"Q", 1}, {"W", 1}, {"E", 1}, {"R", 1}, {"T", 1}, {"Z", 1},
                    {"U", 1}, {"I", 1}, {"O", 1}, {"P", 1}, {"Semicolon", 1}, {"Plus", 1},
                    {"Return", 1.5f}});
    rows.push_back({{"CapsLock", 1.75f}, {"A", 1}, {"S", 1}, {"D", 1}, {"F", 1}, {"G", 1},
                    {"H", 1}, {"J", 1}, {"K", 1}, {"L", 1}, {"Backtick", 1}, {"Quote", 1},
                    {"Slash", 1}, {"Return", 1.25f, ""}});
    rows.push_back({{"Shift", 1.25f}, {"", 1, "<"}, {"Y", 1}, {"X", 1}, {"C", 1}, {"V", 1},
                    {"B", 1}, {"N", 1}, {"M", 1}, {"Comma", 1}, {"Period", 1}, {"Minus", 1},
                    {"Shift", 2.75f}});
    rows.push_back({{"Control", 1.5f}, {"Alt", 1.5f}, {"Space", 9}, {"Alt", 1.5f},
                    {"Control", 1.5f}});
    return rows;
  }();
  return german ? kDe : kUs;
}

// The arrow keys and the block above them, right of the main keys: (name, column, row).
struct BlockKey {
  const char* name;
  float x, y;
};
constexpr BlockKey kBlock[] = {
    {"Insert", 0, 1}, {"Home", 1, 1}, {"PageUp", 2, 1},  {"Delete", 0, 2}, {"End", 1, 2},
    {"PageDown", 2, 2}, {"Up", 1, 4}, {"Left", 0, 5},    {"Down", 1, 5},   {"Right", 2, 5},
};

bool IsArrow(std::string_view key) {
  return key == "Up" || key == "Down" || key == "Left" || key == "Right";
}

void Arrow(ImDrawList* dl, std::string_view key, ImVec2 c, float s, ImU32 col) {
  if (key == "Up") dl->AddTriangleFilled({c.x, c.y - s}, {c.x - s, c.y + s * 0.6f}, {c.x + s, c.y + s * 0.6f}, col);
  if (key == "Down") dl->AddTriangleFilled({c.x, c.y + s}, {c.x + s, c.y - s * 0.6f}, {c.x - s, c.y - s * 0.6f}, col);
  if (key == "Left") dl->AddTriangleFilled({c.x - s, c.y}, {c.x + s * 0.6f, c.y + s}, {c.x + s * 0.6f, c.y - s}, col);
  if (key == "Right") dl->AddTriangleFilled({c.x + s, c.y}, {c.x - s * 0.6f, c.y - s}, {c.x - s * 0.6f, c.y + s}, col);
}

}  // namespace

std::string MainKey(std::string_view binding) {
  const size_t plus = binding.rfind('+');
  // "Plus" itself has no '+' in its name, so the last '+' always ends a modifier.
  return std::string(plus == std::string_view::npos ? binding : binding.substr(plus + 1));
}

std::string ModifierKey(std::string_view binding) {
  const size_t plus = binding.find('+');
  return plus == std::string_view::npos ? std::string() : std::string(binding.substr(0, plus));
}

std::string KeyLegend(std::string_view key, bool german) {
  struct Legend {
    const char* key;
    const char* en;
    const char* de;
  };
  static const Legend kLegends[] = {
      {"Escape", "Esc", "Esc"},
      {"Backspace", "Bksp", "R\xC3\xBC" "ck"},
      {"Tab", "Tab", "Tab"},
      {"CapsLock", "Caps", "Caps"},
      {"Return", "Enter", "Enter"},
      {"Shift", "Shift", "Shift"},
      {"Control", "Ctrl", "Strg"},
      {"Alt", "Alt", "Alt"},
      {"Space", "Space", "Leertaste"},
      {"Insert", "Ins", "Einfg"},
      {"Delete", "Del", "Entf"},
      {"Home", "Home", "Pos1"},
      {"End", "End", "Ende"},
      {"PageUp", "PgUp", "PgUp"},
      {"PageDown", "PgDn", "PgDn"},
      {"Backtick", "`", "\xC3\x96"},
      {"Minus", "-", "-"},
      {"Plus", "=", "+"},
      {"LBracket", "[", "\xC3\x9F"},
      {"RBracket", "]", "\xC2\xB4"},
      {"Backslash", "\\", "^"},
      {"Semicolon", ";", "\xC3\x9C"},
      {"Quote", "'", "\xC3\x84"},
      {"Comma", ",", ","},
      {"Period", ".", "."},
      {"Slash", "/", "#"},
  };
  for (const Legend& l : kLegends) {
    if (key == l.key) return german ? l.de : l.en;
  }
  return std::string(key);
}

float DrawKeyboard(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::vector<KeyMark>& marks,
                   bool german) {
  constexpr float kWidthUnits = 18.6f, kHeightUnits = 6.45f, kGap = 0.1f;
  const float u = std::min((b.x - a.x) / kWidthUnits, (b.y - a.y) / kHeightUnits);
  if (u <= 2) return 0;
  const ImVec2 o{(a.x + b.x - kWidthUnits * u) / 2, a.y};
  auto draw_key = [&](const char* name, const char* legend_override, float x, float y, float w) {
    const ImVec2 k0{o.x + (x + kGap / 2) * u, o.y + (y + kGap / 2) * u};
    const ImVec2 k1{o.x + (x + w - kGap / 2) * u, o.y + (y + 1 - kGap / 2) * u};
    std::string labels;
    ImU32 colour = 0;
    for (const KeyMark& m : marks) {
      if (*name == '\0' || m.key != name) continue;
      if (!colour) colour = m.colour;
      if (!m.label.empty() && !Has(labels, m.label)) labels += (labels.empty() ? "" : "/") + m.label;
    }
    dl->AddRectFilled({k0.x, k0.y + u * 0.06f}, {k1.x, k1.y + u * 0.06f}, Rgba(8, 10, 26, 160),
                      u * 0.14f);
    dl->AddRectFilled(k0, k1, colour ? colour : Rgba(44, 50, 84), u * 0.14f);
    dl->AddRect(k0, k1, colour ? Rgba(255, 255, 255, 120) : Rgba(255, 255, 255, 28), u * 0.14f, 0,
                std::max(1.0f, u * 0.03f));
    const ImU32 ink = colour ? kInk : kDim;
    const ImVec2 c{(k0.x + k1.x) / 2, (k0.y + k1.y) / 2};
    const bool has_label = !labels.empty();
    if (IsArrow(name)) {
      Arrow(dl, name, {c.x, has_label ? c.y - u * 0.14f : c.y}, u * 0.16f, ink);
    } else {
      const std::string legend = legend_override ? legend_override : KeyLegend(name, german);
      float size = u * (legend.size() > 2 ? 0.27f : 0.38f);
      while (size > u * 0.18f && TextSize(BoldFont(), size, legend).x > (k1.x - k0.x) - u * 0.12f) {
        size -= 1;
      }
      const ImVec2 ts = TextSize(BoldFont(), size, legend);
      Label(dl, BoldFont(), size, {c.x - ts.x / 2, (has_label ? c.y - u * 0.16f : c.y) - ts.y / 2},
            ink, legend);
    }
    if (has_label) {
      float size = u * 0.24f;
      while (size > u * 0.14f && TextSize(BoldFont(), size, labels).x > (k1.x - k0.x) - u * 0.08f) {
        size -= 1;
      }
      const ImVec2 ls = TextSize(BoldFont(), size, labels);
      Label(dl, BoldFont(), size, {c.x - ls.x / 2, c.y + u * 0.3f - ls.y / 2}, kInk, labels);
    }
  };
  const std::vector<Row>& rows = Layout(german);
  for (size_t r = 0; r < rows.size(); ++r) {
    const float y = r == 0 ? 0.0f : 0.45f + float(r);
    float x = 0;
    for (const Key& k : rows[r]) {
      if (k.name) draw_key(k.name, k.legend, x, y, k.width);
      x += k.width;
    }
  }
  for (const BlockKey& k : kBlock) draw_key(k.name, nullptr, 15.6f + k.x, 0.45f + k.y, 1);
  return kHeightUnits * u;
}

}  // namespace skylanders::input_pictures
