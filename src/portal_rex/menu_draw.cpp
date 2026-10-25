#include "portal_rex/menu_draw.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>

#include <imgui_internal.h>

#include "portal_rex/game_font.h"
#include "portal_rex/input_pictures.h"
#include "portal_rex/portal_menu_input.h"

namespace skylanders {
namespace {

GameFonts g_game_fonts;

}  // namespace

void SetGameFonts(GameFonts fonts) { g_game_fonts = std::move(fonts); }

}  // namespace skylanders

namespace skylanders::menu_draw {
namespace {

ImFont* g_font_title = nullptr;
ImFont* g_font_bold = nullptr;
// Baked large so they stay sharp when drawn smaller on 1080p and 1440p.
constexpr float kTitleBakeSize = 72.0f;
constexpr float kBoldBakeSize = 44.0f;

float g_s = 1.0f;  // pixels per 1080p unit

// A game's bitmap font as an ImGui font source (ImGui 1.92's font loader interface): for each
// size ImGui asks for, a glyph's cell is scaled from the game's atlas (averaged when smaller,
// interpolated when larger) and packed into ImGui's own atlas. AddFont wants FontLoaderData empty,
// so FontSrcInit finds the font by the source's name.
// The menus' sizes were chosen for Segoe UI, whose capitals are 0.526 of its size (cap height 1434
// over ascent + descent 2724, from its OS/2 and hhea tables). A game font's cells often have more
// room around the letters (SuperChargers' ProximaNova: 0.475, its small-caps title font 0.438), so
// each one is drawn larger until its H is as tall as Segoe's, and the menus look the same size.
// Its baseline goes where Segoe's is, 0.811 of the size from the line's top (ascent 2210 over
// 2724): the menus centre a label by the line's height, so a game font whose letters sit higher
// in their cells drew its labels too high in the tabs, buttons and hints (seen 2026-10-21 in every
// game with its font).
constexpr float kWindowsCapHeight = 0.526f;
constexpr float kWindowsAscent = 0.811f;

struct BitmapSource {
  std::shared_ptr<const GameFont> font;
  std::unordered_map<uint32_t, int> index;  // codepoint -> glyph
  float scale = 1.0f;                       // see kWindowsCapHeight
};

// The H's ink height in pixels, 0 if the font has no H.
float CapHeight(const GameFont& font) {
  for (const GameFontGlyph& g : font.glyphs) {
    if (g.codepoint != 'H' || g.page < 0 || size_t(g.page) >= font.pages.size()) continue;
    const GameFontPage& page = font.pages[g.page];
    int top = -1, bottom = -1;
    for (int row = 0; row < g.height; ++row) {
      for (int col = 0; col < g.width; ++col) {
        const int x = g.x + col, y = g.y + row;
        if (x < page.width && y < page.height && page.alpha[size_t(y) * page.width + x] >= 128) {
          if (top < 0) top = row;
          bottom = row;
          break;
        }
      }
    }
    return top < 0 ? 0.0f : float(bottom - top + 1);
  }
  return 0.0f;
}
std::unordered_map<std::string, BitmapSource> g_bitmap_sources;  // by ImFontConfig::Name

const BitmapSource* SourceOf(const ImFontConfig* src) {
  return static_cast<const BitmapSource*>(src->FontLoaderData);
}

bool BitmapSrcInit(ImFontAtlas*, ImFontConfig* src) {
  auto it = g_bitmap_sources.find(src->Name);
  if (it == g_bitmap_sources.end()) return false;
  src->FontLoaderData = &it->second;
  return true;
}

void BitmapSrcDestroy(ImFontAtlas*, ImFontConfig* src) { src->FontLoaderData = nullptr; }

bool BitmapContainsGlyph(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
  return SourceOf(src)->index.count(codepoint) != 0;
}

bool BitmapBakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked* baked, void*) {
  baked->Ascent = ImCeil(kWindowsAscent * baked->Size);
  baked->Descent = ImFloor((kWindowsAscent - 1.0f) * baked->Size);
  return true;
}

// The coverage of a w x h cell scaled to out_w x out_h: each output pixel averages the source area
// it covers (several pixels when shrinking, a bilinear sample when enlarging).
void ScaleCell(const GameFontPage& page, int x, int y, int w, int h, int out_w, int out_h,
               unsigned char* out) {
  const float sx = float(w) / out_w, sy = float(h) / out_h;
  auto at = [&](int px, int py) -> float {
    px = std::clamp(px, x, x + w - 1);
    py = std::clamp(py, y, y + h - 1);
    return page.alpha[size_t(py) * page.width + px];
  };
  for (int oy = 0; oy < out_h; ++oy) {
    for (int ox = 0; ox < out_w; ++ox) {
      float value;
      if (sx <= 1.0f && sy <= 1.0f) {
        const float fx = x + (ox + 0.5f) * sx - 0.5f, fy = y + (oy + 0.5f) * sy - 0.5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = fx - x0, ty = fy - y0;
        value = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) +
                (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
      } else {
        const float x_from = x + ox * sx, x_to = x_from + sx;
        const float y_from = y + oy * sy, y_to = y_from + sy;
        float sum = 0, weight = 0;
        for (int py = int(y_from); py < int(std::ceil(y_to)); ++py) {
          const float wy = std::min(y_to, py + 1.0f) - std::max(y_from, float(py));
          for (int px = int(x_from); px < int(std::ceil(x_to)); ++px) {
            const float wx = std::min(x_to, px + 1.0f) - std::max(x_from, float(px));
            sum += at(px, py) * wx * wy;
            weight += wx * wy;
          }
        }
        value = weight > 0 ? sum / weight : 0;
      }
      out[size_t(oy) * out_w + ox] = (unsigned char)std::clamp(int(value + 0.5f), 0, 255);
    }
  }
}

bool BitmapLoadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*,
                     ImWchar codepoint, ImFontGlyph* out_glyph, float* out_advance_x) {
  const BitmapSource& source = *SourceOf(src);
  const auto it = source.index.find(codepoint);
  if (it == source.index.end()) return false;
  const GameFontGlyph& g = source.font->glyphs[it->second];
  const float layout = baked->Size * source.scale / source.font->size;
  if (out_advance_x) {
    *out_advance_x = g.advance * layout;
    return true;
  }
  out_glyph->Codepoint = codepoint;
  out_glyph->AdvanceX = g.advance * layout;
  if (g.width <= 0 || g.height <= 0 || g.page < 0 || size_t(g.page) >= source.font->pages.size()) {
    return true;
  }
  const float density = src->RasterizerDensity * baked->RasterizerDensity;
  const int w = std::max(1, int(std::ceil(g.width * layout * density)));
  const int h = std::max(1, int(std::ceil(g.height * layout * density)));
  const ImFontAtlasRectId pack_id = ImFontAtlasPackAddRect(atlas, w, h);
  if (pack_id == ImFontAtlasRectId_Invalid) return false;
  ImTextureRect* r = ImFontAtlasPackGetRect(atlas, pack_id);
  ImFontAtlasBuilder* builder = atlas->Builder;
  builder->TempBuffer.resize(w * h);
  ScaleCell(source.font->pages[g.page], g.x, g.y, g.width, g.height, w, h, builder->TempBuffer.Data);
  out_glyph->X0 = g.x_offset * layout;
  out_glyph->Y0 = (g.y_offset - source.font->ascent) * layout + kWindowsAscent * baked->Size;
  out_glyph->X1 = out_glyph->X0 + w / density;
  out_glyph->Y1 = out_glyph->Y0 + h / density;
  out_glyph->Visible = true;
  out_glyph->PackId = pack_id;
  ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out_glyph, r, builder->TempBuffer.Data,
                                     ImTextureFormat_Alpha8, w);
  return true;
}

const ImFontLoader* BitmapLoader() {
  static ImFontLoader loader = [] {
    ImFontLoader l;
    l.Name = "skylanders game font";
    l.FontSrcInit = BitmapSrcInit;
    l.FontSrcDestroy = BitmapSrcDestroy;
    l.FontSrcContainsGlyph = BitmapContainsGlyph;
    l.FontBakedInit = BitmapBakedInit;
    l.FontBakedLoadGlyph = BitmapLoadGlyph;
    return l;
  }();
  return &loader;
}

// The game's font with the Windows font merged in for the glyphs it lacks, or just the Windows
// font without one.
ImFont* AddFont(ImFontAtlas* atlas, const std::shared_ptr<const GameFont>& game, const char* name,
                const char* windows_ttf, float size) {
  ImFont* font = nullptr;
  if (game && !game->glyphs.empty() && game->size > 0) {
    BitmapSource& source = g_bitmap_sources[name];
    source.font = game;
    source.index.clear();
    for (size_t i = 0; i < game->glyphs.size(); ++i) {
      source.index.emplace(game->glyphs[i].codepoint, int(i));
    }
    const float cap = CapHeight(*game);
    source.scale = cap > 0 ? std::clamp(kWindowsCapHeight * game->size / cap, 0.5f, 2.0f) : 1.0f;
    ImFontConfig cfg;
    std::snprintf(cfg.Name, sizeof(cfg.Name), "%s", name);
    cfg.FontLoader = BitmapLoader();
    cfg.FontDataOwnedByAtlas = true;
    cfg.SizePixels = size;
    font = atlas->AddFont(&cfg);
  }
#ifdef _WIN32
  if (font) {
    ImFontConfig merge;
    merge.MergeMode = true;
    atlas->AddFontFromFileTTF(windows_ttf, size, &merge);
  } else {
    font = atlas->AddFontFromFileTTF(windows_ttf, size);
  }
#else
  (void)windows_ttf;
#endif
  return font;
}

}  // namespace

void AddFonts(ImFontAtlas* atlas) {
  g_font_title = AddFont(atlas, g_game_fonts.title, "game font: title",
                         "C:\\Windows\\Fonts\\seguibl.ttf", kTitleBakeSize);
  g_font_bold = AddFont(atlas, g_game_fonts.body, "game font: body",
                        "C:\\Windows\\Fonts\\segoeuib.ttf", kBoldBakeSize);
}

ImFont* TitleFont() { return g_font_title ? g_font_title : ImGui::GetFont(); }
ImFont* BoldFont() { return g_font_bold ? g_font_bold : ImGui::GetFont(); }

void SetScale(float pixels_per_unit) { g_s = pixels_per_unit; }
float S(float v) { return v * g_s; }

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

void Label(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, std::string_view text,
           float wrap) {
  dl->AddText(font, size, pos, col, text.data(), text.data() + text.size(), wrap);
}

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

ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
}

// ImGui's multi-colour rectangles have square corners, so the rounded shape is drawn in white and
// its vertices recoloured.
void GradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding,
                  ImDrawFlags flags) {
  const int first = dl->VtxBuffer.Size;
  dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (top >> IM_COL32_A_SHIFT) & 0xFF), rounding,
                    flags);
  ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, dl->VtxBuffer.Size, a, {a.x, b.y}, top,
                                                bottom);
}

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

float CapCentredY(float centre_y, float size) {
  return centre_y - (kWindowsAscent - kWindowsCapHeight / 2) * size;
}

namespace {

// 0: the controller's buttons are shown, 1: the keyboard's keys; `shown` follows `wanted`, once
// a frame (the first hint drawn in it moves it on).
float g_keyboard_wanted = 0.0f;
float g_keyboard_shown = 0.0f;
int g_keyboard_frame = -1;

void StepKeyboardHints() {
  const int frame = ImGui::GetFrameCount();
  if (frame == g_keyboard_frame) return;
  g_keyboard_frame = frame;
  // A key pressed in any menu (the keyboard reaches the menus through ImGui); the controllers'
  // presses the menus report themselves (they read the pads).
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) {
      g_keyboard_wanted = 1.0f;
      break;
    }
  }
  const float step = ImGui::GetIO().DeltaTime / 0.25f;
  g_keyboard_shown = g_keyboard_wanted > g_keyboard_shown ? std::min(g_keyboard_wanted, g_keyboard_shown + step)
                                                          : std::max(g_keyboard_wanted, g_keyboard_shown - step);
}

ImU32 Faded(ImU32 col, float alpha) {
  const uint32_t a = uint32_t(float((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
  return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

// The kind of controller the hints draw (Core of Light #29): the one that pressed a button last,
// looked up once a frame; Xbox before any, and for a pad with an Xbox-like layout.
input_pictures::PadKind HintPadKind() {
  static int frame = -1;
  static std::string name;
  static input_pictures::PadKind kind = input_pictures::PadKind::kXbox;
  if (ImGui::GetFrameCount() != frame) {
    frame = ImGui::GetFrameCount();
    if (std::string now = menu_input::LastPadName(); now != name) {
      name = std::move(now);
      kind = name.empty() ? input_pictures::PadKind::kXbox : input_pictures::PadKindOf(name);
      if (kind == input_pictures::PadKind::kGeneric) kind = input_pictures::PadKind::kXbox;
    }
  }
  return kind;
}

// A face button's settings name for a round glyph ("A" -> "a"), empty for anything else.
std::string FaceButton(std::string_view glyph) {
  if (glyph == "A" || glyph == "B" || glyph == "X" || glyph == "Y") return std::string(1, char(glyph[0] - 'A' + 'a'));
  return {};
}

// A pill's text as the controller in use prints it: "LB / RB" -> "L1 / R1" or "L / R", "Start" ->
// "Options" or "+"; other words ("< / >", "Up / Down") stay.
std::string PadGlyph(std::string_view glyph) {
  const input_pictures::PadKind kind = HintPadKind();
  if (kind == input_pictures::PadKind::kXbox) return std::string(glyph);
  static const std::pair<std::string_view, std::string_view> kButtons[] = {
      {"LB", "lb"}, {"RB", "rb"}, {"LT", "lt"}, {"RT", "rt"}, {"L3", "ls"}, {"R3", "rs"},
      {"Back", "back"}, {"Start", "start"}, {"A", "a"}, {"B", "b"}, {"X", "x"}, {"Y", "y"}};
  std::string out;
  size_t start = 0;
  while (start <= glyph.size()) {
    size_t end = glyph.find(" / ", start);
    if (end == std::string_view::npos) end = glyph.size();
    std::string_view word = glyph.substr(start, end - start);
    std::string shown(word);
    for (const auto& [printed, button] : kButtons) {
      if (word != printed) continue;
      if (const char* name = input_pictures::PadButtonName(kind, button, GermanText())) shown = name;
    }
    out += shown;
    if (end == glyph.size()) break;
    out += " / ";
    start = end + 3;
  }
  return out;
}

float PadGlyphWidth(std::string_view glyph, bool round) {
  const float h = S(30);
  return round ? h : std::max(h, TextSize(BoldFont(), S(18), PadGlyph(glyph)).x + S(16));
}

float KeyCapWidth(std::string_view key) { return std::max(S(30), TextSize(BoldFont(), S(17), key).x + S(18)); }

// The glyph's width as drawn now: between the button's and the key's while they cross-fade.
float GlyphWidth(std::string_view glyph, bool round, std::string_view key) {
  const float pad = PadGlyphWidth(glyph, round);
  return key.empty() ? pad : pad + (KeyCapWidth(key) - pad) * g_keyboard_shown;
}

}  // namespace

void UseKeyboardHints(bool keyboard, bool now) {
  g_keyboard_wanted = keyboard ? 1.0f : 0.0f;
  if (now) g_keyboard_shown = g_keyboard_wanted;
}

float HintWidth(std::string_view glyph, std::string_view label, bool round, std::string_view key) {
  return GlyphWidth(glyph, round, key) + S(8) + TextSize(BoldFont(), S(20), label).x + S(28);
}

float Hint(ImDrawList* dl, ImVec2 pos, std::string_view glyph, ImU32 glyph_col,
           std::string_view label, bool round, std::string_view key) {
  StepKeyboardHints();
  const float h = S(30);
  const float t = key.empty() ? 0.0f : g_keyboard_shown;
  const float pad_w = PadGlyphWidth(glyph, round);
  const float glyph_w = GlyphWidth(glyph, round, key);
  const std::string face = round ? FaceButton(glyph) : std::string();
  if (t < 1.0f && !face.empty() && HintPadKind() != input_pictures::PadKind::kXbox) {
    // A PlayStation or Nintendo pad: its own face button (symbol or letter in its place).
    const int first = dl->VtxBuffer.Size;
    input_pictures::DrawPadFaceButton(dl, HintPadKind(), face, {pos.x + h / 2, pos.y + h / 2}, h / 2);
    FadeVertices(dl, first, 1.0f - t);
  } else if (t < 1.0f) {
    if (round) {
      dl->AddCircleFilled({pos.x + h / 2, pos.y + h / 2}, h / 2, Faded(glyph_col, 1.0f - t));
    } else {
      dl->AddRectFilled(pos, {pos.x + pad_w, pos.y + h}, Faded(glyph_col, 1.0f - t), S(8));
    }
    const std::string shown = round ? std::string(glyph) : PadGlyph(glyph);
    ImVec2 gs = TextSize(BoldFont(), S(18), shown);
    Label(dl, BoldFont(), S(18), {pos.x + (pad_w - gs.x) / 2, CapCentredY(pos.y + h / 2, S(18))},
          Faded(kWhite, 1.0f - t), shown);
  }
  if (t > 0.0f) {
    // The key cap: dark with a light edge, as Core of Light draws it.
    const float key_w = KeyCapWidth(key);
    dl->AddRectFilled(pos, {pos.x + key_w, pos.y + h}, Faded(IM_COL32(34, 40, 72, 255), t), S(6));
    dl->AddRect(pos, {pos.x + key_w, pos.y + h}, Faded(IM_COL32(200, 206, 228, 255), t), S(6), 0, S(2));
    ImVec2 ks = TextSize(BoldFont(), S(17), key);
    Label(dl, BoldFont(), S(17), {pos.x + (key_w - ks.x) / 2, CapCentredY(pos.y + h / 2, S(17))},
          Faded(kWhite, t), key);
  }
  Label(dl, BoldFont(), S(20), {pos.x + glyph_w + S(8), CapCentredY(pos.y + h / 2, S(20))}, kDim,
        label);
  return HintWidth(glyph, label, round, key);
}

void ScrollList(size_t& first, size_t& followed, float& wheel, size_t chosen, size_t visible,
                size_t total, bool wheel_on) {
  long long top = static_cast<long long>(first);
  if (wheel_on) {
    wheel += ImGui::GetIO().MouseWheel;
    const int notches = static_cast<int>(wheel);
    wheel -= static_cast<float>(notches);
    top -= notches;  // up (positive) shows earlier rows
  }
  const long long row = static_cast<long long>(chosen);
  const long long shown = static_cast<long long>(std::max<size_t>(1, visible));
  if (chosen != followed) {
    followed = chosen;
    if (row < top) top = row;
    if (row >= top + shown) top = row - shown + 1;
  }
  const long long last = total > visible ? static_cast<long long>(total - visible) : 0;
  first = static_cast<size_t>(std::clamp(top, 0LL, last));
}

void FadeVertices(ImDrawList* dl, int first_vertex, float alpha) {
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  if (alpha >= 1.0f) return;
  for (int i = first_vertex; i < dl->VtxBuffer.Size; ++i) {
    ImU32& col = dl->VtxBuffer[i].col;
    const uint32_t a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    col = (col & ~IM_COL32_A_MASK) | (uint32_t(float(a) * alpha) << IM_COL32_A_SHIFT);
  }
}

bool GermanText() {
  // user_language is the SDK's (XGetLanguage); the apps set it from their `language` setting at
  // startup. Before that, or in an app without that step, the setting itself.
  if (rex::cvar::GetFlagByName("user_language") == "3") return true;
  return rex::cvar::GetFlagByName("language") == "german";
}

}  // namespace skylanders::menu_draw
