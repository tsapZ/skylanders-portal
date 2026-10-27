#pragma once

// The disc check: default.xex's SHA-256 against the ones a port is made for (one per supported
// disc). Used by the game at start, by <game>_xexcheck (the installer's check, xex_check_cli.cpp)
// and by the launcher's disc setup through that tool.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace skylanders::xex {

enum class XexCheck {
  Match,        // file hashes to the expected value
  Mismatch,     // file readable but hash differs (includes empty and truncated files)
  Unreadable,   // file missing, a directory, or a read error
  BadExpected,  // expected hash is not 64 hex characters
};

struct XexCheckResult {
  XexCheck status;
  std::string actual_sha256;  // empty unless the file was readable
};

// Lowercase hex SHA-256 of the file at `path`, or nullopt if it cannot be read completely.
std::optional<std::string> Sha256File(const std::filesystem::path& path);

// Compares the file's SHA-256 with `expected_sha256`: one or more 64-hex-character hashes, comma
// separated (any case, surrounding whitespace ignored); a match with any of them is a Match.
XexCheckResult VerifyXex(const std::filesystem::path& xex, std::string_view expected_sha256);

// UTF-8 form of a path. Unlike path::string(), never throws for characters outside the ANSI code page.
std::string Utf8(const std::filesystem::path& p);

// Human-readable explanation for anything other than XexCheck::Match; empty for a match. UTF-8, so
// a game folder with any Unicode characters is shown exactly as it was tried (see Utf8()).
// `game` names the disc to put in, e.g. "Skylanders: SuperChargers".
std::string DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                               std::string_view expected_sha256, std::string_view game);

}  // namespace skylanders::xex
