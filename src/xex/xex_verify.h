#pragma once

// The disc check: default.xex's SHA-256 against the ones a port is made for (one per supported
// disc), or its code pin (xex_header.h) against theirs: another pressing with the same code runs
// too. Used by the game at start, by <game>_xexcheck (the installer's check, xex_check_cli.cpp)
// and by the launcher's disc setup through that tool.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace skylanders::xex {

enum class XexCheck {
  Match,        // file hashes to the expected value
  SameCode,     // another file, but its code pin is an expected one: another pressing, same code
  Mismatch,     // file readable but hash differs (includes empty and truncated files)
  Unreadable,   // file missing, a directory, or a read error
  BadExpected,  // expected hash is not 64 hex characters
};

struct XexCheckResult {
  XexCheck status;
  std::string actual_sha256;  // empty unless the file was readable
  std::string header;         // DescribeXexHeader of the file, empty when it has no XEX2 header
  std::string code_pin;       // its code pin, empty without a header or code pages
};

// Lowercase hex SHA-256 of the file at `path`, or nullopt if it cannot be read completely.
std::optional<std::string> Sha256File(const std::filesystem::path& path);

// Compares the file's SHA-256 with `expected_sha256`: one or more entries, comma separated (any
// case, surrounding whitespace ignored), each a 64-hex-character SHA-256 or a code pin
// ("82450000:" and 64 hex characters). A hash matching is a Match, else a code pin matching is
// SameCode.
XexCheckResult VerifyXex(const std::filesystem::path& xex, std::string_view expected_sha256);

// The code pin of the XEX2 file at `path` (xex_header.h), or nullopt when it has none.
std::optional<std::string> XexCodePin(const std::filesystem::path& path);

// UTF-8 form of a path. Unlike path::string(), never throws for characters outside the ANSI code page.
std::string Utf8(const std::filesystem::path& p);

// The first line of a game's log (build_info.cpp): "<title> <version> (build <commit>); Windows 11
// build 26100; <CPU>, <n> threads; <n> GB RAM". Nothing personal in it: no names, no paths.
std::string BuildAndSystemLine(std::string_view title);

// The game's release version (its installer script's MyAppVersion) and commit, from the source
// skylanders_game_xex generates for the game (cmake/game_xex.cmake).
const char* GameVersion();
const char* GameCommit();

// Human-readable explanation for anything other than XexCheck::Match; empty for a match. UTF-8, so
// a game folder with any Unicode characters is shown exactly as it was tried (see Utf8()).
// `game` names the disc to put in, e.g. "Skylanders: SuperChargers".
std::string DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                               std::string_view expected_sha256, std::string_view game);

// Files every copy of the disc has, checked at start: `required` is a comma separated list of paths
// relative to the rom folder ('/' between the parts), where a "*" part stands for any one folder
// ("text/*/global.lxb": some language folder in text has global.lxb). Returns the first entry the
// rom folder has no match for, empty when all are there. Some extract tools leave out folders
// below the first level (Spyro's Adventure's text\<language> and ui\<language>, CoL #64); the game
// then froze on its title screen with no word why.
std::string MissingRomFile(const std::filesystem::path& rom, std::string_view required);

// The message for MissingRomFile's result: which file, and how to get a complete copy.
std::string DescribeMissingRomFile(const std::filesystem::path& rom, std::string_view missing,
                                   std::string_view game);

}  // namespace skylanders::xex
