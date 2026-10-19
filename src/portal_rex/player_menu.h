#pragma once

// The player menu: which controller plays as which player. Opens with player_menu_buttons (default
// L3 + R3 together) on any controller or bind_player_menu (F9) on the keyboard. Every controller
// moves itself with left / right, all at the same time, so a Bluetooth controller that dropped out
// and came back as another player is put back in a moment; Y puts every controller back in
// connection order. The game sees no input while it is open.
//
// The assignment lasts for this session (controllers get new ids when they reconnect).
// Needs the input system from CreatePortalMenuInputSystem (portal_menu_input.h).

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {

void RegisterPlayerMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context);

// Closes the menu; call from ReXApp::OnShutdown.
void ShutdownPlayerMenu();

}  // namespace skylanders
