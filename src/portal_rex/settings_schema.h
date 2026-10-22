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
#include <string_view>
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
// The graphics defaults every game uses where the owner hasn't set his own: 16x texture
// filtering (the SDK defaults to 4x; ground and floor textures seen at a shallow angle stay sharp
// for about 1 % of the frame rate). Call from ReXApp::OnPostSetup, once the GPU plugin's settings
// exist; WriteSettingsSchemaIfRequested applies it too, so the launcher shows the values the game
// starts with.
void ApplyGraphicsDefaults();

// An HD texture pack for the game: <Documents>\Skylanders\Textures\<game> (beside the figure
// pictures, Pictures\<game>), used when that folder exists and texture_replace_dir isn't set
// otherwise. A pack is the rexglue-sdk's <hash>.dds files: the textures texture_dump_dir writes,
// made bigger (e.g. 4x by an AI upscaler) in the same block format. Call from OnPostSetup.
void UseTexturePack(const std::filesystem::path& user_data_root, std::string_view game);

void WriteSettingsSchemaIfRequested(rex::RuntimeConfig& config,
                                    const std::function<std::vector<SettingsPage>()>& pages,
                                    const std::filesystem::path& config_file);

}  // namespace skylanders
