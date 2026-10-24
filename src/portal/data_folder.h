#pragma once

// The one folder the Skylanders games keep their data in (the owner's call, 2026-10-24): by default
// Documents\Skylanders, or the folder picked in Core of Light, with
//
//   Figures\  Pictures\  Textures\     shared by every game
//   Spyro's Adventure\  Giants\  ...   each game's saves, achievements and shader cache
//
// Before, each game had a folder of its own in Documents (Documents\ssarecomp, ...). A game or the
// launcher copies that over once; the old folder stays, with a note in it, until the launcher has
// asked whether it may go. No ReXGlue here: the launcher uses it as well as the games.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace skylanders {

// Documents (the shell's known folder, so a moved Documents folder counts).
std::filesystem::path DocumentsFolder();
// Documents\Skylanders.
std::filesystem::path DefaultDataFolder();
// Where a picked data folder is written down for every game: %APPDATA%\Skylanders\data-folder.txt,
// one line, UTF-8. Not there: the default.
std::filesystem::path DataFolderPointer();
// The data folder: the pointer's, else Documents\Skylanders.
std::filesystem::path DataFolder();
// Writes the pointer (or removes it for the default folder); false if it couldn't.
bool SetDataFolder(const std::filesystem::path& folder);

// A game's folder in the data folder, by its name ("Spyro's Adventure", "Giants", "Swap Force",
// "Trap Team", "SuperChargers", "Imaginators"), and whether a folder name is one of them.
bool IsGameFolderName(const std::filesystem::path& name);
// The game folder's name for a game's executable name ("ssarecomp" -> "Spyro's Adventure"); empty
// for one it doesn't know.
std::string_view GameFolderForExe(std::string_view exe_name);

// The folder with Figures, Pictures and Textures for a game's user data: the data folder for a game
// folder in it (Documents\Skylanders\Giants -> Documents\Skylanders, also a profile's
// ...\Profiles\<id>\Giants -> ...\Profiles\<id>); else, as before, "Skylanders" next to it
// (Documents\giantsrecomp -> Documents\Skylanders; a test copy keeps its own).
std::filesystem::path SharedBase(const std::filesystem::path& user_data_root);

// The note a copy leaves in the old folder (its first line is the new folder).
inline constexpr const char* kMovedNote = "MOVED_TO.txt";

struct CopyResult {
  bool ok = false;
  size_t files = 0;
  uintmax_t bytes = 0;
  std::string error;  // UTF-8, empty when ok
};
// Copies every file under `from` to `to`, which must not exist yet: first into `to` + ".partial",
// renamed to `to` once each file is there with its size, so `to` is either complete or absent.
// `on_file` (optional) hears each file before it is copied, for a progress display.
CopyResult CopyFolderVerified(const std::filesystem::path& from, const std::filesystem::path& to,
                              const std::function<void(const std::filesystem::path&)>& on_file = {});
// Whether each file under `old_dir` is under `new_dir` too (before the old one may be deleted).
bool AllFilesThere(const std::filesystem::path& old_dir, const std::filesystem::path& new_dir);
// Leaves the note in `old_dir` that it was copied to `new_dir`.
void WriteMovedNote(const std::filesystem::path& old_dir, const std::filesystem::path& new_dir);

}  // namespace skylanders
