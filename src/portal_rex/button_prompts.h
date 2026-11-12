#pragma once

// PlayStation or Nintendo button prompts in place of a game's Xbox 360 ones (the small ideas from SkylandersPC, the
// owner's go 2026-11-12). The icons are our own, drawn by portal/prompt_icons.h; at start they are written as
// <hash>.dds into <user data root>\prompts\<style> for the game's own prompt textures (each a texture of its own,
// found with the SDK's texture_dump_dir), and that folder goes last into texture_replace_dir, so it wins over an HD
// pack. The SDK replaces textures with Direct3D 12 only; with Vulkan the game keeps its own prompts.

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
