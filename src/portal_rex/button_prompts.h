#pragma once

// PlayStation or Nintendo button prompts in place of a game's Xbox 360 ones (the small ideas from SkylandersPC, the
// owner's go 2026-11-12). The icons are our own, drawn by portal/prompt_icons.h; where each game keeps its prompts is
// in portal/prompt_tables.h. Two ways in:
// - prompts that are textures of their own (Spyro's Adventure): written at start as <hash>.dds into
//   <user data root>\prompts\<style>, that folder last in texture_replace_dir, so it wins over an HD pack;
// - button atlases (Swap Force, SuperChargers, Imaginators): a texture patch (rex/graphics/texture_patch.h) redraws
//   each from the game's own texels when it loads, only the buttons' cells changed.
// The SDK replaces textures with Direct3D 12 only; with Vulkan the game keeps its own prompts.

#include <filesystem>
#include <string_view>

#include "portal_rex/settings_menu.h"

namespace skylanders {

// Whether the game's prompt textures are known (`game` as UseTexturePack gets it: "Spyro's Adventure").
bool HasButtonPrompts(std::string_view game);

// With button_prompts set to playstation or nintendo: writes the icons for the game's prompt textures and adds their
// folder to texture_replace_dir. Called by UseTexturePack (settings_schema.h), after the HD pack.
void UseButtonPrompts(const std::filesystem::path& user_data_root, std::string_view game);

// F4 > Steuerung's "Button prompts: Xbox / PlayStation / Nintendo" (from the next start), for a game whose prompt
// textures are known; nothing for another.
void AddButtonPromptsSetting(SettingsPage& page, std::string_view game);

}  // namespace skylanders
