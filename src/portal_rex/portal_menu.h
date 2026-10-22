#pragma once

// The portal menu: swap Skylanders with a controller or the keyboard, like taking a figure off a
// real portal and putting another one on.
//
// - Opens for the controller whose open button fired (portal_menu_open_buttons, see
//   portal_menu_input.h) or from the keyboard (bind_portal_menu, F6; player 1).
// - Opening lifts that player's figure off the emulated portal, so the game reacts on its own:
//   alone it pauses and asks for a Skylander, in co-op only that player drops out. Each player
//   can only change their own figure.
// - Choosing places the new figure (both halves for a swapper); closing puts the old one back.
// - Opened with a button, the menu waits for the game's own portal screen (the camera zooming out
//   to the portal) before it fades in; opened by the game's prompt, it comes up at once.
// - Start: options (which games are listed, only my figures) and, for a second player, leaving
//   the portal.
// - portal_item_off_buttons / bind_portal_item_off (F5) take the item off without the menu.
//
// Needs the input system from CreatePortalMenuInputSystem and the emulated (software) portal.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>

struct ImFontAtlas;
namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
class WindowedAppContext;
}  // namespace rex::ui

REXCVAR_DECLARE(std::string, portal_menu_games);
REXCVAR_DECLARE(bool, portal_menu_auto_open);
REXCVAR_DECLARE(std::string, portal_menu_hidden_games);
REXCVAR_DECLARE(bool, portal_menu_owned_only);
REXCVAR_DECLARE(int32_t, portal_menu_open_delay_ms);

namespace skylanders {

// Adds the fonts the menu draws with (Segoe UI Black/Bold where available). Call from
// ReXApp::OnConfigureFonts; without it the menu falls back to the default font.
void AddPortalMenuFonts(ImFontAtlas* atlas);

// Registers the menu's keyboard bind and the controller open handler. With an immediate drawer
// the menu can show figure pictures (SetFigureImageProvider).
void RegisterPortalMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                        rex::ui::ImmediateDrawer* immediate_drawer = nullptr);

// The settings file the menu writes its options to (ReXApp's config path). Without it the options
// last for this session only.
void SetPortalMenuConfigPath(std::filesystem::path config_path);

// Frees the menu's textures; call before the immediate drawer goes away (ReXApp::OnShutdown).
void ShutdownPortalMenu();

// A figure's picture.
struct FigureImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4
};

// Pictures for the menu, e.g. from the game's own data. Called on the UI thread for what is on
// screen, every frame, so it must not block: return nullptr while a picture is still loading and
// the menu asks again. Without a provider (or a picture), the menu draws the element symbol.
using FigureImageProvider =
    std::function<std::shared_ptr<const FigureImage>(uint16_t id, uint16_t variant)>;
void SetFigureImageProvider(FigureImageProvider provider);

// The pictures for the Core of Light launcher's figure library, which reads no disc itself: once
// the game is running, every figure picture it has (from SetFigureImageProvider) goes into `dir`
// as "<id>-<variant>.png", once; pictures already there are kept. The pictures are asked for on
// the UI thread, a few at a time, and written on a thread of its own; a picture the game is still
// loading is asked for again for about two minutes. The hats go there too, once: "hats.txt" (one
// line per hat the game knows: its number, a tab, its name in the menu's language, a tab, its
// bonuses joined by " / ") and "hat-<number>.png" for those with a picture (SetHatInfoProvider,
// SetArtImageProvider). And the upgrade trees of the figures in the figure folder next to the
// Pictures folder (SetFigureUpgradesProvider): "upgrades-<id>-<variant>.txt", one line per path
// ("P", its kind 0-4 as FigureUpgradePath::Kind, its number among the two paths or -1, name,
// description) and per upgrade after it ("U", name, cost, bit, alternate bit, its picture's file
// or nothing, description), tab-separated, with the pictures as "up-<id>-<variant>-<n>.png".
// Call after RegisterPortalMenu. `dir` is typically
// SharedPicturesDir(user_data_root, "<the game's name>").
void StartFigurePictureCache(std::filesystem::path dir);
// <user data root's parent>\Skylanders\Pictures\<game>, next to the shared figure folder.
std::filesystem::path SharedPicturesDir(const std::filesystem::path& user_data_root, std::string_view game);

// The name the game shows for a figure in its current language (magic items and adventure packs
// have translated names), or nullopt to keep the catalog name. Same rules as above.
using FigureNameProvider = std::function<std::optional<std::string>(uint16_t id, uint16_t variant)>;
void SetFigureNameProvider(FigureNameProvider provider);

// What the game calls an adventure pack (the toys 300-399, each opening a level of its own) in its
// current language, e.g. Spyro's Adventure's "Ortselement" / "Location piece"; empty keeps the
// menu's "Abenteuer-Paket" / "Adventure pack". Shown in the details view and the portal check.
void SetAdventurePackName(std::string name);

// A figure's trait the game shows with a symbol of its own, e.g. Swap Force's SWAP skill of a
// swapper's bottom half ("Hüpfen" with the HUD's bounce symbol). The menu puts the symbol on the
// figure's card and the name next to it in the preview and the details view; for a swapper it asks
// for the bottom half. `image` is an art key for the art image provider; `name` in the menu's
// language. Same rules as the other providers (UI thread, must not block).
struct FigureTrait {
  std::string image;
  std::string name;
};
using FigureTraitProvider = std::function<std::optional<FigureTrait>(uint16_t id, uint16_t variant)>;
void SetFigureTraitProvider(FigureTraitProvider provider);

// Pictures from the game's own files by key, for the menu's symbols: "element:magic" (the
// element names in lower case, "light" and "dark" from Trap Team on), "ui:gold" (the game's
// gold symbol, for the gold amount and the upgrade prices), and the keys the upgrade and hat
// data below name. Same rules as the figure pictures: return nullptr while loading; without one
// the menu draws its own symbols.
using ArtImageProvider = std::function<std::shared_ptr<const FigureImage>(const std::string& key)>;
void SetArtImageProvider(ArtImageProvider provider);

// A figure's upgrades as the game's upgrade screen lists them, for the details view.
struct FigureUpgrade {
  std::string name;
  std::string description;
  uint32_t cost = 0;
  int bit = -1;       // its bit in FigureStats::upgrades; -1: always there (the starting attacks)
  int alternate_bit = -1;  // a path's upgrade: its bit while the other path is the chosen one
  std::string image;  // key for the art image provider
};
struct FigureUpgradePath {
  enum class Kind { kStart, kBasic, kPath, kSoulGem, kWowPow };
  Kind kind = Kind::kBasic;
  std::string name;
  std::string description;
  std::vector<FigureUpgrade> upgrades;
};
struct FigureUpgrades {
  std::vector<FigureUpgradePath> paths;  // in the game's order: start, basic, two paths, ...
};
// The upgrades for exactly this figure (the Wow Pow follows the variant's year); nullptr while
// loading or if the game has none (items, sidekicks). Called on the UI thread, must not block.
using FigureUpgradesProvider =
    std::function<std::shared_ptr<const FigureUpgrades>(uint16_t id, uint16_t variant)>;
void SetFigureUpgradesProvider(FigureUpgradesProvider provider);

// A hat (its kTfbSpyroTag_Hat_* number, as a figure stores it): the game's name for it in its
// language, its picture and what it adds to the wearer's stats.
struct HatStat {
  std::string name;   // "Kritische Trefferwahrscheinlichkeit"
  float value = 0;
  std::string image;  // the stat's symbol, a key for the art image provider (may be empty)
};
struct HatInfo {
  std::string name;
  std::string image;
  std::vector<HatStat> stats;
};
using HatInfoProvider = std::function<std::shared_ptr<const HatInfo>(uint16_t hat)>;
void SetHatInfoProvider(HatInfoProvider provider);

// How long a magic item's effect lasts in the game, in seconds, for the details view; nullopt
// while unknown and for an item that acts at once. Same rules as the figure pictures.
using ItemDurationProvider = std::function<std::optional<int>(uint16_t id)>;
void SetItemDurationProvider(ItemDurationProvider provider);

// True while the game is busy with a figure change (its "place your Skylander" sequence). Putting
// an item on or taking it off waits until this is false, so the player's Skylander is back in
// the game first. Called from a background thread. Without it, item changes wait 0.7 s.
void SetGameBusyProvider(std::function<bool()> busy);

// For a game integration that knows when the game waits for a player's figure (its "place your
// Skylander" screen): opens the menu for that player (guest user index), steered by their
// controller, unless portal_menu_auto_open is off, a menu is already open or the portal isn't
// the emulated one. Callable from any thread.
void AutoOpenPortalMenu(uint32_t user);

// The same for a game that says it waits for a Skylander but not whose (Trap Team's portal script
// only has "Player State - Need Toy"): opens the menu for the first player without a Skylander on
// the emulated portal, the order in which the game itself hands figures to players. So player 2's
// prompt while player 1 plays opens player 2's menu. Callable from any thread.
void AutoOpenPortalMenuForPlayerWithoutFigure();

// For a game integration that knows when the game shows its portal error screen ("too many
// Skylanders / items on the portal", a figure it can't read; `reason` is the game's own reason
// code, see magic_moment_watch.cpp): opens the portal check. It lists everything on the emulated
// portal, marks what there is too much of, and takes off what the player picks, so a stuck error
// screen can always be left. Only with the emulated portal and while no other menu is open.
void ShowPortalCheck(uint32_t reason);
// The game left its error screen: closes the portal check if it is still open.
void ClosePortalCheck();

// The game stopped waiting: closes the menu again if AutoOpenPortalMenu opened it and nothing was
// chosen yet. Callable from any thread.
void CloseAutoOpenedPortalMenu();

// The game started its portal sequence for this player (the camera zooms out to the portal): a
// menu opened with the button and waiting for the portal screen comes up 0.6 s later, just before
// the game's "place your Skylander" text. Callable from any thread.
void PortalScreenComing(uint32_t user);

}  // namespace skylanders
