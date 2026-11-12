#pragma once

// PlayStation or Nintendo button prompts in place of a game's Xbox 360 ones (the small ideas from SkylandersPC, the
// owner's go 2026-11-12). The icons are our own, drawn by portal/prompt_icons.h; where each game keeps its prompts is
// in portal/prompt_tables.h. Each goes in through an SDK texture patch (rex/graphics/texture_patch.h) when the game
// loads it: a prompt texture of its own (Spyro's Adventure) becomes our icon, a button atlas (Swap Force,
// SuperChargers, Imaginators) is redrawn from the game's own texels with only the buttons' cells changed. Nothing is
// written to disk. The SDK replaces textures with Direct3D 12 only; with Vulkan the game keeps its own prompts.

#include <string_view>

#include "portal_rex/settings_menu.h"

namespace skylanders {

// Whether the game's prompts are known (`game` as UseTexturePack gets it: "Spyro's Adventure").
bool HasButtonPrompts(std::string_view game);

// With button_prompts set to playstation or nintendo: registers the patches for the game's prompts. Called by
// UseTexturePack (settings_schema.h), before the GPU loads any texture.
void UseButtonPrompts(std::string_view game);

// F4 > Steuerung's "Button prompts: Xbox / PlayStation / Nintendo" (from the next start), for a game whose prompts are
// known; nothing for another.
void AddButtonPromptsSetting(SettingsPage& page, std::string_view game);

}  // namespace skylanders
