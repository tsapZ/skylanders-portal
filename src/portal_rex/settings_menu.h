#pragma once

// The settings menu in the portal menu's look, for controller and keyboard: pages of settings the
// app describes (SettingsPage), switched with LB / RB or LT / RT, each setting changed with left /
// right. Opens with settings_menu_buttons (default Back + RB) on any controller or
// bind_settings_menu (F4), also over the portal or the player menu (which then closes like with
// B). A change applies at once where it can, so it can be seen; Y saves the changes into the
// settings file, line by line (settings_file.h), and leaving with unsaved changes asks whether to
// save them (not saving puts the old values back). Settings that only apply at start-up are marked
// and the menu says so. The game sees no input while it is open.

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {

struct SettingOption {
  std::string value;  // as the cvar takes it ("true", "2", "cas")
  std::string en, de;
};

struct Setting {
  std::string cvar;  // empty for an action
  std::string en, de;
  std::string help_en, help_de;
  // The values to step through; empty for a bool cvar (off / on).
  std::vector<SettingOption> options;
  // Takes effect only after a restart. Settings whose cvar says so (start-up only or restart) are
  // marked anyway.
  bool restart = false;
  // A preset: `current` says which option matches now ("" for none: shown as custom), `preset`
  // gives the settings (cvar, value) an option stands for; the menu stores them.
  std::function<std::string()> current;
  std::function<std::vector<std::pair<std::string, std::string>>(const std::string& value)>
      preset;
  // An action row: A runs it (with the menu closed).
  std::function<void()> action;
};

struct SettingsPage {
  std::string en, de;
  std::vector<Setting> settings;
};

// `config_path`: the settings file changes go to. Call from ReXApp::OnCreateDialogs.
void RegisterSettingsMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                          std::vector<SettingsPage> pages, std::filesystem::path config_path);

// Closes the menu; call from ReXApp::OnShutdown.
void ShutdownSettingsMenu();

}  // namespace skylanders
