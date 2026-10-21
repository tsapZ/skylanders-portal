#pragma once

// The game's own fonts for the menus (portal, player, settings and achievement menus): bitmap
// fonts a game integration reads from the owner's game files at start-up, like the pictures.
// Nothing of them is in this library. Glyphs a font lacks come from the Windows font as before.

#include <cstdint>
#include <memory>
#include <vector>

namespace skylanders {

struct GameFontGlyph {
  uint32_t codepoint = 0;
  float advance = 0;                 // pen movement, in pixels at the font's size
  float x_offset = 0, y_offset = 0;  // the cell's top-left from the pen position and the line top
  int page = 0;                      // index into GameFont::pages
  int x = 0, y = 0, width = 0, height = 0;  // the cell in its page, in pixels
};

struct GameFontPage {
  int width = 0, height = 0;
  std::vector<uint8_t> alpha;  // width * height coverage, 0-255
};

struct GameFont {
  float size = 0;    // the line height the glyphs are drawn for, in pixels
  float ascent = 0;  // from a line's top to its baseline, in pixels
  std::vector<GameFontPage> pages;
  std::vector<GameFontGlyph> glyphs;
};

struct GameFonts {
  std::shared_ptr<const GameFont> title;  // headings and names (instead of Segoe UI Black)
  std::shared_ptr<const GameFont> body;   // everything else (instead of Segoe UI Bold)
};

// Before AddPortalMenuFonts (the app's OnConfigureFonts): the fonts the menus draw with. A font
// left empty keeps the Windows one.
void SetGameFonts(GameFonts fonts);

}  // namespace skylanders
