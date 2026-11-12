#pragma once
// Where each game keeps its button prompts (research 2026-11-12, the SDK's texture dumps and a census of every
// texture the games load): only numbers (hashes, sizes, boxes), no pictures. All go in through the SDK's texture
// patches (rex/graphics/texture_patch.h):
// - Spyro's Adventure: each prompt a DXT5 texture of its own, replaced by our icon as a whole.
// - Swap Force, SuperChargers, Imaginators: 8888 atlases with every button (one also holds three other symbols, a
//   keyhole, a badge and an exclamation mark, which stay the game's): only the buttons' cells redrawn.
// - Giants and Trap Team: their menu bars draw a tinted circle with a letter from the game's font; none here.

#include <cstdint>
#include <span>
#include <string_view>

#include "portal/prompt_icons.h"

namespace skylanders::portal {

// A prompt texture of its own (DXT5): the SDK's hash of it (texture_dump_dir's file name), its size, the button it
// shows, whether the game keeps it upside down.
struct PromptTexture {
  uint64_t hash;
  uint16_t width, height;
  PromptButton button;
  bool flipped;
};

// A button atlas: the SDK's hash of it, its size, the buttons' cells.
struct PromptAtlas {
  uint64_t hash;
  uint16_t width, height;
  std::span<const AtlasCell> cells;
};

// `game` as the games name themselves to the library ("Spyro's Adventure", "Swap Force", ...).
std::span<const PromptTexture> PromptTexturesOf(std::string_view game);
std::span<const PromptAtlas> PromptAtlasesOf(std::string_view game);

}  // namespace skylanders::portal
