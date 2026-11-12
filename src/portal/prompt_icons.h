#pragma once
// Our own button prompt icons (the small ideas from SkylandersPC, owner 2026-11-12): PlayStation style (cross, circle,
// square, triangle, Options, Share, L1/R1, L2/R2, L3/R3, the d-pad) and Nintendo style (letters by position: the bottom
// button is B; +, -, L/R, ZL/ZR), drawn here from shapes (no font, no one else's art). Two ways into a game:
// - a prompt texture of its own: the icon as a DXT5 DDS with all mips, for the SDK's texture_replace_dir (<hash>.dds);
// - a button atlas (an 8888 texture with every prompt, sometimes with other art): the atlas redrawn from the game's own
//   texels, only the buttons' cells replaced, for the SDK's texture patches (rex/graphics/texture_patch.h).
// No ReXGlue: unit-tested (tests/prompt_icons_test.cpp).

#include <cstdint>
#include <span>
#include <vector>

namespace skylanders::portal {

enum class PromptStyle { kPlayStation, kNintendo };

// The Xbox 360 button a game's prompt shows.
enum class PromptButton { kA, kB, kX, kY, kStart, kBack, kLB, kRB, kLT, kRT, kLS, kRS, kDpad };

// The icon of `button` in `style`, `size` x `size` (a power of two, 16 to 1024) RGBA, rows top first, upside down when
// `flip` (some games keep their prompt textures that way and turn them when drawing). Empty for a size below 4.
std::vector<uint8_t> DrawPromptIcon(PromptStyle style, PromptButton button, int size, bool flip);

// A DXT5 DDS file of an RGBA picture (`size` x `size`, a power of two) with its whole mip chain; empty when the size
// isn't a power of two or doesn't match the picture.
std::vector<uint8_t> EncodeDdsDxt5(const std::vector<uint8_t>& rgba, int size);

// One button in a game's atlas: its box in the atlas's texels.
struct AtlasCell {
  PromptButton button;
  uint16_t x, y, w, h;
};

// The atlas `bgra` (`width` x `height` texels as the SDK hands them over: B, G, R, A bytes) at `scale` times its size
// (1 to 8): every cell cleared (with two texels around it) and our icon drawn in it, the rest scaled up as it is
// (bilinear, as the GPU would); written as an 8888 DDS (A8R8G8B8 masks, the same byte order) with all mips. Empty on
// a bad size.
std::vector<uint8_t> PatchButtonAtlas(const uint8_t* bgra, int width, int height, std::span<const AtlasCell> cells,
                                      PromptStyle style, int scale);

}  // namespace skylanders::portal
