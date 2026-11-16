#pragma once

// The report a game writes by itself for a GitHub issue (the dialog's Save in problem_note.cpp), so
// Report a problem works without Core of Light. BuildGameReport is plain C++ and lives in
// src/portal/problem_report_text.cpp, which the core unit tests can build without ReXGlue; the
// desktop file and the private words are here because they need Windows.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace skylanders {

// The report text: the game, its settings (*.toml), the newest logs and notes, with private words
// replaced. Each part starts with a line "===== <part> =====".
std::string BuildGameReport(const std::filesystem::path& work_dir,
                            const std::filesystem::path& exe_dir, const std::string& program,
                            const std::vector<std::pair<std::string, std::string>>& private_words);

// The words to keep private: the Windows user's folder, user name and computer name.
std::vector<std::pair<std::string, std::string>> PrivateWords();

// Writes BuildGameReport to the desktop as Skylanders-problem-report-<date>.txt; an empty path
// with `error` set when it can't be written.
std::filesystem::path WriteGameProblemReport(const std::string& program, std::string& error);

}  // namespace skylanders
