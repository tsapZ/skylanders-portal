#pragma once

// The settings menu's pages as a file for the launcher (Skylanders: Core of Light), so it can show
// every game's own settings, exactly as its F4 menu has them, and write them into the game's
// settings file without knowing the game. Started with --settings_schema=<file>, the game writes
// its pages there (JSON, below) and quits before it opens a window or reads the disc.
//
// {"version": 1, "config_file": "<the game's settings file>",
//  "pages": [{"en": "Graphics", "de": "Grafik", "settings": [
//    {"cvar": "resolution_scale", "en": ..., "de": ..., "help_en": ..., "help_de": ...,
//     "type": "bool" | "int" | "float" | "string", "category": "Graphics",
//     "default": "<compiled-in>", "value": "<now: the file, the command line or the game's own
//     default>", "restart": true, "buttons": false,
//     "options": [{"value": "1", "en": "1x (720p)", "de": "1x (720p)"}, ...],
//     "presets": {"<option>": [["<cvar>", "<value>"], ...]}, "current": "<option or empty>"}]}]}
//
// A string setting is written to the settings file quoted, every other one as it is. Rows without
// a setting (actions, the controls overview) are left out.

#include <filesystem>
#include <functional>
#include <vector>

#include "portal_rex/settings_menu.h"

namespace rex {
struct RuntimeConfig;
}  // namespace rex

namespace skylanders {

// Whether the game was started with --settings_schema (it then skips its disc check, which would
// stop it with a message box when the disc is missing).
bool SettingsSchemaRequested();

// With --settings_schema: loads the GPU plugin into `config` if it isn't yet (its settings, such
// as the internal resolution, exist only then), writes `pages()` with every setting's value now and
// exits the process. Call at the end of ReXApp::OnPreSetup, after the game's own defaults. Without
// the flag it does nothing.
void WriteSettingsSchemaIfRequested(rex::RuntimeConfig& config,
                                    const std::function<std::vector<SettingsPage>()>& pages,
                                    const std::filesystem::path& config_file);

}  // namespace skylanders
