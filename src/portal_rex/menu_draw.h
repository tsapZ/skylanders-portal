#pragma once

// Drawing helpers shared by the Skylanders-style menus (portal menu, player menu): fonts, colours,
// a 1080p-based scale, outlined text, gradients, the gold-framed panel and button hints. Everything
// draws straight into an ImDrawList; UI thread only.

#include <cstdint>
#include <string>
#include <string_view>

#include <imgui.h>

namespace skylanders::menu_draw {

// Adds the fonts the menus draw with (Segoe UI Black/Bold where available). Call from
// ReXApp::OnConfigureFonts; without it the menus fall back to the default font.
void AddFonts(ImFontAtlas* atlas);
ImFont* TitleFont();
ImFont* BoldFont();

// Sizes are given in 1080p units; SetScale(display height / 1080) once per frame.
void SetScale(float pixels_per_unit);
float S(float v);

inline ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline const ImU32 kGold = IM_COL32(244, 196, 84, 255);
inline const ImU32 kGoldDark = IM_COL32(150, 104, 30, 255);
inline const ImU32 kWhite = IM_COL32(255, 255, 255, 255);
inline const ImU32 kDim = IM_COL32(205, 214, 238, 255);
inline const ImU32 kInk = IM_COL32(22, 20, 32, 255);
inline const ImU32 kAcceptGreen = IM_COL32(76, 175, 80, 255);
inline const ImU32 kBackRed = IM_COL32(232, 73, 43, 255);
inline const ImU32 kDetailsBlue = IM_COL32(46, 134, 222, 255);
inline const ImU32 kYellow = IM_COL32(230, 180, 40, 255);
inline const ImU32 kHintGrey = IM_COL32(78, 86, 120, 255);

// Player 1 blue, 2 red, 3 green, 4 yellow (guest user index 0-3).
ImU32 PlayerColor(uint32_t user);

void Label(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, std::string_view text,
           float wrap = 0.0f);
// Text with a dark outline, for titles over busy backgrounds.
void OutlinedLabel(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
                   std::string_view text, float outline);
ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap = 0.0f);
// The top to draw a one-line label of `size` at so its capitals are centred on `centre_y`, for
// labels in a box (tabs, chips, buttons, hints): the menus' fonts have Segoe UI's proportions (the
// game fonts are fitted to them), so the capitals' middle is 0.548 of the size below the top.
float CapCentredY(float centre_y, float size);

// A rounded rectangle with a vertical gradient.
void GradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding,
                  ImDrawFlags flags = 0);

// The menus' panel: slightly see-through dark blue, a magic glow from the top, a gold frame.
void DrawPanel(ImDrawList* dl, ImVec2 p0, ImVec2 p1);

// A controller button or key hint: a coloured round glyph for face buttons, a pill otherwise.
// Returns the width it took, including the gap to the next hint.
float Hint(ImDrawList* dl, ImVec2 pos, std::string_view glyph, ImU32 glyph_col,
           std::string_view label, bool round);
float HintWidth(std::string_view glyph, std::string_view label, bool round);

// The game's picture for an art key (portal_menu.h SetArtImageProvider) as a texture, kept once
// made; a null id while there is none (yet). Lives in portal_menu.cpp, which owns the textures.
ImTextureID GameArtTexture(const std::string& key);

// Multiplies the alpha of every vertex drawn into `dl` since `first_vertex` (a fade-in).
void FadeVertices(ImDrawList* dl, int first_vertex, float alpha);

// True when the menus should speak German: the game runs in German, by the Xbox 360 language the
// app gave it (user_language 3), which also covers a `language` of "auto" or a raw id.
bool GermanText();

}  // namespace skylanders::menu_draw
