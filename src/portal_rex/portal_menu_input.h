#pragma once

// Input plumbing for the portal menu (portal_menu.h). The game's input system is built like the
// SDK's default one, but every driver is wrapped, so the menu can
//   - notice its open button on any controller and know which player that controller is,
//   - hide all controller and keyboard input from the game while the menu is open,
//   - read each controller itself while the game sees nothing.
// The Guide button is always hidden from the game; the game has no use for it.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

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

// Called on a game input thread when a controller's open button fires while the menu is closed,
// with the device and the player (guest user index) it belongs to.
void SetOpenHandler(std::function<void(rex::input::DeviceId device, uint32_t user)> handler);

// While open, the game sees every wrapped device as a neutral controller. Closing keeps hiding a
// device until all its buttons are released, so the closing press doesn't reach the game.
void SetMenuOpen(bool open);
bool MenuOpen();

// The device's current state, read now from the UI thread; nullopt for devices that can't be
// polled there (keyboard/mouse emulation) or that are gone.
std::optional<rex::input::X_INPUT_GAMEPAD> ReadPad(rex::input::DeviceId device);

// The player a device plays as: its connection slot for controllers, 0 for the keyboard and
// other synthetic devices.
uint32_t UserOf(rex::input::DeviceId device);

// The keyboard/mouse emulation device, if there is one (it plays as player 0).
std::optional<rex::input::DeviceId> KeyboardDevice();

// True when this button set fires the open button (used by the menu to close again with the
// same button). Edge detection is the caller's job.
bool IsOpenChord(uint16_t buttons);

}  // namespace menu_input
}  // namespace skylanders
