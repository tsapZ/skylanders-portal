#pragma once

// Input plumbing for the portal menu (portal_menu.h). The game's input system is built like the
// SDK's default one, but every driver is wrapped, so the menu can
//   - notice its open button on any controller and know which player that controller is,
//   - hide all controller and keyboard input from the game while the menu is open,
//   - read each controller itself while the game sees nothing.
// The Guide button is always hidden from the game; the game has no use for it. Buttons that may
// start an open entry (Back for back+start or hold_back) reach the game only once it's clear
// they don't open the menu, so Back doesn't open the game's pause menu on the way.
// keyboard_player picks which player the keyboard plays as.
//
// Besides the portal menu, the same kind of button entries take the item off the portal
// (portal_item_off_buttons), open the player menu (player_menu_buttons) and the settings menu
// (settings_menu_buttons).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/device.h>
#include <rex/input/input.h>

namespace rex::input {
class InputDriver;
}  // namespace rex::input
namespace rex::system {
class IInputSystem;
}  // namespace rex::system

REXCVAR_DECLARE(std::string, portal_menu_open_buttons);
REXCVAR_DECLARE(std::string, portal_item_off_buttons);
REXCVAR_DECLARE(std::string, player_menu_buttons);
REXCVAR_DECLARE(std::string, settings_menu_buttons);

namespace skylanders {

// The SDK's default input system (SDL or XInput by input_backend, keyboard/mouse emulation, the
// fallback device) with every driver wrapped for the portal menu. Pass it as
// RuntimeConfig::input_factory in OnPreSetup.
std::unique_ptr<rex::system::IInputSystem> CreatePortalMenuInputSystem(bool tool_mode);

// Wraps an extra driver the app adds later (e.g. scripted input for tests) the same way.
// `pollable`: its GetDeviceState may also be called from the UI thread.
std::unique_ptr<rex::input::InputDriver> WrapForPortalMenu(
    std::unique_ptr<rex::input::InputDriver> driver, bool pollable);

namespace menu_input {

// What a button entry does.
enum class ButtonAction {
  kPortalMenu,  // portal_menu_open_buttons
  kItemOff,     // portal_item_off_buttons
  kPlayerMenu,  // player_menu_buttons
  kSettingsMenu,  // settings_menu_buttons
};

// Called on a game input thread when a controller's button entry for `action` fires while no menu
// is open, with the device and the player (guest user index) it belongs to.
void SetButtonHandler(ButtonAction action,
                      std::function<void(rex::input::DeviceId device, uint32_t user)> handler);
// Same as SetButtonHandler(ButtonAction::kPortalMenu, handler).
void SetOpenHandler(std::function<void(rex::input::DeviceId device, uint32_t user)> handler);

// While open, the game sees every wrapped device as a neutral controller. Closing keeps hiding a
// device until all its buttons are released, so the closing press doesn't reach the game.
void SetMenuOpen(bool open);
bool MenuOpen();

// The open menu (the portal or the player menu) can be asked to close like with its Back button,
// so that the settings menu can come up instead. Set right after SetMenuOpen(true); SetMenuOpen
// (false) clears it. While one is set, the settings menu's button entry fires even though a menu
// is open. UI thread.
void SetMenuCloser(std::function<void()> closer);
// Asks the open menu to close; false if no menu is open or it can't be closed this way.
bool CloseOpenMenu();

// The device's current state, read now from the UI thread; nullopt for devices that can't be
// polled there (keyboard/mouse emulation) or that are gone.
std::optional<rex::input::X_INPUT_GAMEPAD> ReadPad(rex::input::DeviceId device);

// The player a device plays as: its connection slot for controllers, keyboard_player - 1 for the
// keyboard, 0 for other synthetic devices.
uint32_t UserOf(rex::input::DeviceId device);

// The keyboard/mouse emulation device, if there is one.
std::optional<rex::input::DeviceId> KeyboardDevice();

// A device that plays as a given player: a controller the menu can poll if there is one, else
// the keyboard when it plays as that player.
struct PlayerDevice {
  rex::input::DeviceId id;
  bool keyboard = false;
};
std::optional<PlayerDevice> DeviceForUser(uint32_t user);

// All controllers the menu can poll that play as this player.
std::vector<rex::input::DeviceId> PadsOfUser(uint32_t user);

// Makes a device play as `user` (nullopt: back to its slot). For test scripts that act as a
// second player.
void SetDevicePlayer(rex::input::DeviceId device, std::optional<uint32_t> user);

// Shows the game a short press of `button` (X_INPUT_GAMEPAD_*) on every device of `user`, a moment
// from now and even while the device is still hidden after a menu: the menu presses the game's own
// buttons with it (B "leave the game" on the portal screen).
void TapButton(uint32_t user, uint16_t button);

// How many times a device's own button went down so far (taps from TapButton not counted): a
// change tells that the player pressed something.
uint64_t RealButtonPresses();

// The X_INPUT_GAMEPAD_* bit of a button name as the *_buttons settings write it ("b", "rs",
// "start", ...); 0 for an unknown name.
uint16_t ButtonByName(const std::string& name);

// True when this button set fires the open button (used by the menu to close again with the
// same button). Edge detection is the caller's job.
bool IsOpenChord(uint16_t buttons);
// The same for any action's entries.
bool IsActionChord(ButtonAction action, uint16_t buttons);

// A device the input system has now, for the player menu.
struct ConnectedDevice {
  rex::input::DeviceId id;
  std::string name;
  uint32_t slot = 0;       // connection order
  uint32_t user = 0;       // the player it plays as now
  uint32_t natural_user = 0;  // the player it plays as when nobody moved it
  bool keyboard = false;   // the keyboard/mouse emulation
  bool pollable = false;   // ReadPad works for it
  bool reassigned = false;  // moved with SetDevicePlayer
};
// Controllers, the keyboard and other wrapped devices (a test script), in connection order.
std::vector<ConnectedDevice> ConnectedDevices();

}  // namespace menu_input
}  // namespace skylanders
