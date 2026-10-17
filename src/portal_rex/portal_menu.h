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
//
// Needs the input system from CreatePortalMenuInputSystem and the emulated (software) portal.

#include <rex/cvar.h>

struct ImFontAtlas;
namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

REXCVAR_DECLARE(std::string, portal_menu_games);

namespace skylanders {

// Adds the fonts the menu draws with (Segoe UI Black/Bold where available). Call from
// ReXApp::OnConfigureFonts; without it the menu falls back to the default font.
void AddPortalMenuFonts(ImFontAtlas* atlas);

// Registers the menu's keyboard bind and the controller open handler.
void RegisterPortalMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context);

}  // namespace skylanders
