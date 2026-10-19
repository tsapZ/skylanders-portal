#pragma once

// Writing single settings into the app's TOML settings file without touching the rest of it.
// rex::cvar::SaveConfig rewrites the whole file from every changed setting: it drops the user's
// comments and would also store command-line settings (--user_data_root, --input_script). The
// menus write only the settings they change, line by line.

#include <filesystem>
#include <string>
#include <string_view>

namespace skylanders {

// Replaces the line of `name` in the file, or adds it under its [category] header (["Portal"] or
// [Portal]), or adds the header at the end. `toml_value` is already a TOML value ("true", "2",
// "\"text\""). UI thread only.
void WriteSetting(const std::filesystem::path& path, std::string_view category,
                  std::string_view name, const std::string& toml_value);

// Sets the cvar now (settings that only apply at start-up keep their running value) and writes it
// into the settings file under the cvar's own category; nothing is written when `path` is empty.
// Returns false when the cvar doesn't exist or rejects the value. UI thread only.
bool StoreSetting(const std::filesystem::path& path, std::string_view name,
                  const std::string& value);

}  // namespace skylanders
