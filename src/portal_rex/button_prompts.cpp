#include "portal_rex/button_prompts.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/texture_patch.h>
#include <rex/logging.h>

#include "portal/prompt_icons.h"
#include "portal/prompt_tables.h"
#include "portal_rex/input_pictures.h"
#include "portal_rex/portal_menu_input.h"

REXCVAR_DEFINE_STRING(button_prompts, "auto", "Input",
                      "The button prompts the game shows: auto (those of the controller connected at start: "
                      "PlayStation or Nintendo, else the game's own), xbox (the game's own), playstation or "
                      "nintendo (our own icons in their place; Direct3D 12 only); read at start");

namespace skylanders {
namespace {

using portal::PromptAtlas;
using portal::PromptStyle;
using portal::PromptTexture;

// 256x256: the games' own are 128x128, and the menus show them at up to about twice that at 1440p.
constexpr int kIconSize = 256;
// The atlases at 4x their size: their buttons are 38-53 texels, about 20-40 pixels on a 1080p screen.
constexpr int kAtlasScale = 4;

std::atomic<bool> g_auto{false};
std::atomic<PromptStyle> g_style{PromptStyle::kPlayStation};

// The style to draw: the setting's, or with auto the one of the controller that came first, chosen once, when the
// first prompt loads (the controllers are known by then; one plugged in later changes nothing until the next start).
// None: the game's own Xbox prompts.
std::optional<PromptStyle> StyleNow() {
  if (!g_auto.load()) return g_style.load();
  static std::once_flag once;
  static std::optional<PromptStyle> chosen;
  std::call_once(once, [] {
    // The first prompts load a few seconds after start, maybe before a pad has announced itself: up to 2 s for one,
    // on a replacement worker only (the game shows its own texture meanwhile), never on the GPU thread.
    const std::string async = rex::cvar::GetFlagByName("texture_replace_async");
    const bool may_wait = async != "false" && async != "0";
    std::string name;
    for (int tries = 0;; ++tries) {
      uint32_t first = UINT32_MAX;
      for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
        if (device.keyboard || device.slot >= first) continue;
        first = device.slot;
        name = device.name;
      }
      if (!name.empty() || !may_wait || tries >= 20) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // An XInput pad (Xbox, Steam Input, DS4Windows) is named as one and keeps the game's own.
    switch (input_pictures::PadKindOf(name)) {
      case input_pictures::PadKind::kPlayStation:
        chosen = PromptStyle::kPlayStation;
        break;
      case input_pictures::PadKind::kNintendo:
        chosen = PromptStyle::kNintendo;
        break;
      default:
        break;
    }
    REXLOG_INFO("Button prompts: automatic, {} -> {}", name.empty() ? std::string("no controller") : name,
                !chosen ? "the game's own" : *chosen == PromptStyle::kPlayStation ? "playstation" : "nintendo");
  });
  return chosen;
}

// The SDK's texture patches (rex/graphics/texture_patch.h): `user` is the table entry. They run on a replacement
// worker.
bool PatchTexture(uint64_t, uint32_t width, uint32_t height, const uint8_t*, size_t, std::vector<uint8_t>& dds,
                  void* user) {
  const PromptTexture& t = *static_cast<const PromptTexture*>(user);
  if (width != t.width || height != t.height) return false;
  const std::optional<PromptStyle> style = StyleNow();
  if (!style) return false;
  dds = portal::EncodeDdsDxt5(portal::DrawPromptIcon(*style, t.button, kIconSize, t.flipped), kIconSize);
  return !dds.empty();
}

bool PatchAtlas(uint64_t, uint32_t width, uint32_t height, const uint8_t* data, size_t size, std::vector<uint8_t>& dds,
                void* user) {
  const PromptAtlas& atlas = *static_cast<const PromptAtlas*>(user);
  if (width != atlas.width || height != atlas.height || size != size_t(width) * height * 4) return false;
  const std::optional<PromptStyle> style = StyleNow();
  if (!style) return false;
  dds = portal::PatchButtonAtlas(data, int(width), int(height), atlas.cells, *style, kAtlasScale);
  return !dds.empty();
}

}  // namespace

bool HasButtonPrompts(std::string_view game) {
  return !portal::PromptTexturesOf(game).empty() || !portal::PromptAtlasesOf(game).empty();
}

void UseButtonPrompts(std::string_view game) {
  const std::string setting = REXCVAR_GET(button_prompts);
  if (setting == "playstation") {
    g_style.store(PromptStyle::kPlayStation);
  } else if (setting == "nintendo") {
    g_style.store(PromptStyle::kNintendo);
  } else if (setting == "auto") {
    g_auto.store(true);
  } else {
    return;  // xbox: the game's own
  }
  if (!HasButtonPrompts(game)) {
    if (!g_auto.load()) REXLOG_INFO("Button prompts: none known for {}, the game's own stay", game);
    return;
  }
  // The games' own setting (gpu_backend: any = Direct3D 12 where there is one, d3d12, vulkan): the SDK replaces
  // textures in its Direct3D 12 texture cache only.
  if (rex::cvar::GetFlagByName("gpu_backend") == "vulkan") {
    REXLOG_INFO("Button prompts: the Vulkan renderer can't replace textures, the game's own stay");
    return;
  }
  const std::span<const PromptTexture> textures = portal::PromptTexturesOf(game);
  for (const PromptTexture& t : textures) {
    rex::graphics::RegisterTexturePatch(t.hash, t.width, t.height, &PatchTexture, const_cast<PromptTexture*>(&t));
  }
  const std::span<const PromptAtlas> atlases = portal::PromptAtlasesOf(game);
  for (const PromptAtlas& atlas : atlases) {
    rex::graphics::RegisterTexturePatch(atlas.hash, atlas.width, atlas.height, &PatchAtlas,
                                        const_cast<PromptAtlas*>(&atlas));
  }
  REXLOG_INFO("Button prompts: {} ({} prompt textures, {} button atlases, redrawn as they load)", setting,
              textures.size(), atlases.size());
}

void AddButtonPromptsSetting(SettingsPage& page, std::string_view game) {
  if (!HasButtonPrompts(game)) return;
  Setting s{"button_prompts", "Button prompts", "Tastensymbole",
            "Which buttons the game's prompts show. Automatic: those of the controller connected when the game "
            "starts (a PlayStation or Nintendo one; others, and pads through Steam Input, show Xbox). Nintendo's "
            "go by position: the bottom button is B. The icons are this project's own. Takes effect when the game "
            "starts again; with the Vulkan renderer the game keeps its own.",
            "Welche Tasten die Hinweise des Spiels zeigen. Automatisch: die des Controllers, der beim Spielstart "
            "angeschlossen ist (PlayStation oder Nintendo; andere und Controller \xC3\xBC" "ber Steam Input zeigen "
            "Xbox). Nintendo nach Lage: die untere Taste ist B. Die Symbole sind eigene dieses Projekts. Wirkt beim "
            "n\xC3\xA4" "chsten Spielstart; mit dem Vulkan-Renderer bleiben die des Spiels.",
            {{"auto", "Automatic", "Automatisch"},
             {"xbox", "Xbox", "Xbox"},
             {"playstation", "PlayStation", "PlayStation"},
             {"nintendo", "Nintendo", "Nintendo"}}};
  s.restart = true;
  page.settings.push_back(std::move(s));
}

}  // namespace skylanders
