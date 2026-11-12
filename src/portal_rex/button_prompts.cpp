#include "portal_rex/button_prompts.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "portal/prompt_icons.h"

REXCVAR_DEFINE_STRING(button_prompts, "xbox", "Input",
                      "The button prompts the game shows: xbox (its own), playstation or nintendo (our own "
                      "icons in their place; Direct3D 12 only); read at start");

namespace skylanders {
namespace {

using portal::PromptButton;
using portal::PromptStyle;

// One of a game's prompt textures: the SDK's hash of its top level (texture_dump_dir's file name), the button it
// shows, whether the game keeps it upside down (and turns it when drawing).
struct PromptTexture {
  uint64_t hash;
  PromptButton button;
  bool flipped;
};

// Spyro's Adventure (research\button-prompts-2026-11-12.md: dumps of the title, the hub, the pause menu's pages):
// 128x128 DXT5 each, upright. The game shows no X / Y / Back prompt on those screens.
constexpr PromptTexture kSpyrosAdventure[] = {
    {0xB9E607D0F791F494, PromptButton::kA, false},
    {0x6537E008ADF32F89, PromptButton::kB, false},
    {0x9D6716CD0FDB72B0, PromptButton::kStart, false},
};

std::span<const PromptTexture> TexturesOf(std::string_view game) {
  if (game == "Spyro's Adventure") return kSpyrosAdventure;
  return {};
}

// 256x256: the games' are 128x128, and the menus show them at up to about twice that at 1440p.
constexpr int kIconSize = 256;

bool SameFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  const std::vector<uint8_t> old{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  return old == data;
}

}  // namespace

bool HasButtonPrompts(std::string_view game) { return !TexturesOf(game).empty(); }

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
  const std::span<const PromptTexture> textures = TexturesOf(game);
  if (textures.empty()) {
    REXLOG_INFO("Button prompts: none known for {}, the game's own stay", game);
    return;
  }
  const std::filesystem::path dir = user_data_root / "prompts" / style_name;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  size_t written = 0;
  for (const PromptTexture& t : textures) {
    const std::vector<uint8_t> dds =
        portal::EncodeDdsDxt5(portal::DrawPromptIcon(style, t.button, kIconSize, t.flipped), kIconSize);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llX.dds", static_cast<unsigned long long>(t.hash));
    const std::filesystem::path path = dir / name;
    if (SameFile(path, dds)) continue;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
    if (!out) {
      const std::u8string u8 = path.u8string();
      REXLOG_WARN("Button prompts: could not write {}, the game's own stay", std::string(u8.begin(), u8.end()));
      return;
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
    return;
  }
  REXLOG_INFO("Button prompts: {} ({} textures, {} written) from {}", style_name, textures.size(), written, folder);
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
