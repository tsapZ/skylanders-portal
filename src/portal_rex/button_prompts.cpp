#include "portal_rex/button_prompts.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/texture_patch.h>
#include <rex/logging.h>

#include "portal/prompt_icons.h"
#include "portal/prompt_tables.h"

REXCVAR_DEFINE_STRING(button_prompts, "xbox", "Input",
                      "The button prompts the game shows: xbox (its own), playstation or nintendo (our own "
                      "icons in their place; Direct3D 12 only); read at start");

namespace skylanders {
namespace {

using portal::PromptAtlas;
using portal::PromptStyle;
using portal::PromptTexture;

// 256x256: the games' own are 128x128, and the menus show them at up to about twice that at 1440p.
constexpr int kIconSize = 256;
// The atlases at 4x their size: their buttons are 38-53 texels, about 20-40 pixels on a 1080p screen.
constexpr int kAtlasScale = 4;

std::atomic<PromptStyle> g_style{PromptStyle::kPlayStation};

bool SameFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  const std::vector<uint8_t> old{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  return old == data;
}

// The SDK's texture patch for a button atlas (rex/graphics/texture_patch.h): `user` is its table entry. Runs on a
// replacement worker, on the game's own texels.
bool PatchAtlas(uint64_t, uint32_t width, uint32_t height, const uint8_t* data, size_t size, std::vector<uint8_t>& dds,
                void* user) {
  const PromptAtlas& atlas = *static_cast<const PromptAtlas*>(user);
  if (width != atlas.width || height != atlas.height || size != size_t(width) * height * 4) return false;
  dds = portal::PatchButtonAtlas(data, int(width), int(height), atlas.cells, g_style.load(), kAtlasScale);
  return !dds.empty();
}

// Spyro's Adventure's way: each prompt a file in <user data>\prompts\<style>, that folder last in
// texture_replace_dir. False when that didn't work.
bool WritePromptFiles(const std::filesystem::path& user_data_root, const std::string& style_name, PromptStyle style,
                      std::span<const PromptTexture> textures) {
  const std::filesystem::path dir = user_data_root / "prompts" / style_name;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::vector<std::string> names;
  for (const PromptTexture& t : textures) {
    char name[32];
    std::snprintf(name, sizeof(name), "%016llX.dds", static_cast<unsigned long long>(t.hash));
    names.push_back(name);
  }
  // Files of an older table out, so a texture taken off the table goes back to the game's own.
  std::vector<std::filesystem::path> stale;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::filesystem::path& old = entry.path();
    if (old.extension() != ".dds" || old.stem().string().size() != 16) continue;
    if (std::find(names.begin(), names.end(), old.filename().string()) == names.end()) stale.push_back(old);
  }
  for (const auto& old : stale) {
    std::error_code remove_ec;
    std::filesystem::remove(old, remove_ec);
  }
  size_t written = 0;
  for (size_t i = 0; i < textures.size(); ++i) {
    const PromptTexture& t = textures[i];
    const std::vector<uint8_t> dds =
        portal::EncodeDdsDxt5(portal::DrawPromptIcon(style, t.button, kIconSize, t.flipped), kIconSize);
    const std::filesystem::path path = dir / names[i];
    if (SameFile(path, dds)) continue;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
    if (!out) {
      const std::u8string u8 = path.u8string();
      REXLOG_WARN("Button prompts: could not write {}, the game's own stay", std::string(u8.begin(), u8.end()));
      return false;
    }
    ++written;
  }
  // Last: a later folder's file wins over an earlier one's (an HD pack's) of the same hash.
  const std::u8string u8 = dir.u8string();
  const std::string folder(u8.begin(), u8.end());
  std::string dirs = rex::cvar::GetFlagByName("texture_replace_dir");
  dirs += (dirs.empty() ? "" : ";") + folder;
  if (!rex::cvar::SetFlagByName("texture_replace_dir", dirs)) {
    REXLOG_WARN("Button prompts: could not set texture_replace_dir, the game's own stay");
    return false;
  }
  REXLOG_INFO("Button prompts: {} ({} textures, {} written) from {}", style_name, textures.size(), written, folder);
  return true;
}

}  // namespace

bool HasButtonPrompts(std::string_view game) {
  return !portal::PromptTexturesOf(game).empty() || !portal::PromptAtlasesOf(game).empty();
}

void UseButtonPrompts(const std::filesystem::path& user_data_root, std::string_view game) {
  const std::string style_name = REXCVAR_GET(button_prompts);
  PromptStyle style;
  if (style_name == "playstation") {
    style = PromptStyle::kPlayStation;
  } else if (style_name == "nintendo") {
    style = PromptStyle::kNintendo;
  } else {
    return;  // xbox: the game's own
  }
  if (!HasButtonPrompts(game)) {
    REXLOG_INFO("Button prompts: none known for {}, the game's own stay", game);
    return;
  }
  // The replacement exists with the Direct3D 12 renderer only (the GPU plugin defines its setting).
  if (!rex::cvar::GetFlagInfo("texture_replace_dir")) {
    REXLOG_INFO("Button prompts: this renderer can't replace textures (Direct3D 12 only), the game's own stay");
    return;
  }
  g_style.store(style);
  const std::span<const PromptTexture> textures = portal::PromptTexturesOf(game);
  if (!textures.empty() && !WritePromptFiles(user_data_root, style_name, style, textures)) return;
  const std::span<const PromptAtlas> atlases = portal::PromptAtlasesOf(game);
  for (const PromptAtlas& atlas : atlases) {
    rex::graphics::RegisterTexturePatch(atlas.hash, &PatchAtlas, const_cast<PromptAtlas*>(&atlas));
  }
  if (!atlases.empty()) {
    REXLOG_INFO("Button prompts: {} ({} button atlases redrawn as they load)", style_name, atlases.size());
  }
}

void AddButtonPromptsSetting(SettingsPage& page, std::string_view game) {
  if (!HasButtonPrompts(game)) return;
  Setting s{"button_prompts", "Button prompts", "Tastensymbole",
            "Which buttons the game's prompts show: the Xbox ones, as on the console, or PlayStation's or "
            "Nintendo's (by position: the bottom button is B). The icons are this project's own. Takes effect when "
            "the game starts again; with the Vulkan renderer the game keeps its own.",
            "Welche Tasten die Hinweise des Spiels zeigen: die der Xbox wie auf der Konsole, oder die von "
            "PlayStation oder Nintendo (nach Lage: die untere Taste ist B). Die Symbole sind eigene dieses Projekts. "
            "Wirkt beim n\xC3\xA4" "chsten Spielstart; mit dem Vulkan-Renderer bleiben die des Spiels.",
            {{"xbox", "Xbox", "Xbox"}, {"playstation", "PlayStation", "PlayStation"}, {"nintendo", "Nintendo", "Nintendo"}}};
  s.restart = true;
  page.settings.push_back(std::move(s));
}

}  // namespace skylanders
