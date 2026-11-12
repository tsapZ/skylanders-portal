#pragma once
// Our own button prompt icons (the small ideas from SkylandersPC, owner 2026-11-12): PlayStation style (cross, circle,
// square, triangle, Options, Share) and Nintendo style (letters by position: the bottom button is B), drawn here from
// shapes (no font, no one else's art) and written as DXT5 DDS files with all mips, so a game's own Xbox 360 prompt
// textures can be replaced by them (the SDK's texture_replace_dir, <hash>.dds). No ReXGlue: unit-tested
// (tests/prompt_icons_test.cpp).

#include <cstdint>
#include <vector>

namespace skylanders::portal {

enum class PromptStyle { kPlayStation, kNintendo };

// The Xbox 360 button a game's prompt texture shows.
enum class PromptButton { kA, kB, kX, kY, kStart, kBack };

// The icon of `button` in `style`, `size` x `size` (a power of two, 16 to 1024) RGBA, rows top first, upside down when
// `flip` (some games keep their prompt textures that way and turn them when drawing). Empty for a size below 4.
std::vector<uint8_t> DrawPromptIcon(PromptStyle style, PromptButton button, int size, bool flip);

// A DXT5 DDS file of an RGBA picture (`size` x `size`, a power of two) with its whole mip chain; empty when the size
// isn't a power of two or doesn't match the picture.
std::vector<uint8_t> EncodeDdsDxt5(const std::vector<uint8_t>& rgba, int size);

}  // namespace skylanders::portal
