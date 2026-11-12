#include "portal_rex/button_prompts.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <string>
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

// The SDK's texture patches (rex/graphics/texture_patch.h): `user` is the table entry. They run on a replacement
// worker.
bool PatchTexture(uint64_t, uint32_t width, uint32_t height, const uint8_t*, size_t, std::vector<uint8_t>& dds,
                  void* user) {
  const PromptTexture& t = *static_cast<const PromptTexture*>(user);
  if (width != t.width || height != t.height) return false;
  dds = portal::EncodeDdsDxt5(portal::DrawPromptIcon(g_style.load(), t.button, kIconSize, t.flipped), kIconSize);
  return !dds.empty();
}

bool PatchAtlas(uint64_t, uint32_t width, uint32_t height, const uint8_t* data, size_t size, std::vector<uint8_t>& dds,
                void* user) {
  const PromptAtlas& atlas = *static_cast<const PromptAtlas*>(user);
  if (width != atlas.width || height != atlas.height || size != size_t(width) * height * 4) return false;
  dds = portal::PatchButtonAtlas(data, int(width), int(height), atlas.cells, g_style.load(), kAtlasScale);
  return !dds.empty();
}

}  // namespace

bool HasButtonPrompts(std::string_view game) {
  return !portal::PromptTexturesOf(game).empty() || !portal::PromptAtlasesOf(game).empty();
}

void UseButtonPrompts(std::string_view game) {
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
  // The games' own setting (gpu_backend: any = Direct3D 12 where there is one, d3d12, vulkan): the SDK replaces
  // textures in its Direct3D 12 texture cache only.
  if (rex::cvar::GetFlagByName("gpu_backend") == "vulkan") {
    REXLOG_INFO("Button prompts: the Vulkan renderer can't replace textures, the game's own stay");
    return;
  }
  g_style.store(style);
  const std::span<const PromptTexture> textures = portal::PromptTexturesOf(game);
  for (const PromptTexture& t : textures) {
    rex::graphics::RegisterTexturePatch(t.hash, t.width, t.height, &PatchTexture, const_cast<PromptTexture*>(&t));
  }
  const std::span<const PromptAtlas> atlases = portal::PromptAtlasesOf(game);
  for (const PromptAtlas& atlas : atlases) {
    rex::graphics::RegisterTexturePatch(atlas.hash, atlas.width, atlas.height, &PatchAtlas,
                                        const_cast<PromptAtlas*>(&atlas));
  }
  REXLOG_INFO("Button prompts: {} ({} prompt textures, {} button atlases, redrawn as they load)", style_name,
              textures.size(), atlases.size());
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
