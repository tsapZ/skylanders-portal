#include "portal_rex/menu_draw.h"

#include <algorithm>
#include <cfloat>

#include <imgui_internal.h>

namespace skylanders::menu_draw {
namespace {

ImFont* g_font_title = nullptr;
ImFont* g_font_bold = nullptr;
// Baked large so they stay sharp when drawn smaller on 1080p and 1440p.
constexpr float kTitleBakeSize = 72.0f;
constexpr float kBoldBakeSize = 44.0f;

float g_s = 1.0f;  // pixels per 1080p unit

}  // namespace

void AddFonts(ImFontAtlas* atlas) {
#ifdef _WIN32
  g_font_title = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguibl.ttf", kTitleBakeSize);
  g_font_bold = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", kBoldBakeSize);
#else
  (void)atlas;
#endif
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

float HintWidth(std::string_view glyph, std::string_view label, bool round) {
  const float h = S(30);
  const float glyph_w = round ? h : std::max(h, TextSize(BoldFont(), S(18), glyph).x + S(16));
  return glyph_w + S(8) + TextSize(BoldFont(), S(20), label).x + S(28);
}

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
  return HintWidth(glyph, label, round);
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

}  // namespace skylanders::menu_draw
