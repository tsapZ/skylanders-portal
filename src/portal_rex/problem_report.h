#pragma once

// The report a game writes by itself for a GitHub issue (the dialog's Save in problem_note.cpp), so
// Report a problem works without Core of Light. BuildGameReport is plain C++ and lives in
// src/portal/problem_report_text.cpp, which the core unit tests can build without ReXGlue (so does
// PosixPrivateWords); the desktop file and PrivateWords are here because they ask the system.

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

// The words to keep private: the Windows user's folder, user name and computer name; on Linux
// the home folder, user and host name (PosixPrivateWords).
std::vector<std::pair<std::string, std::string>> PrivateWords();

// PrivateWords for Linux and other POSIX systems, from HOME, USER (or LOGNAME) and the host name:
// the home folder becomes $HOME, the user $USER, the host $HOSTNAME (and its first label, when
// the name has dots). Like the Windows words, a name shorter than 3 characters stays, and so does
// a home of "/": replacing them would blank ordinary text all over the report.
std::vector<std::pair<std::string, std::string>> PosixPrivateWords(std::string home,
                                                                   const std::string& user,
                                                                   const std::string& host);

// Writes BuildGameReport to the desktop as Skylanders-problem-report-<date>.txt; an empty path
// with `error` set when it can't be written.
std::filesystem::path WriteGameProblemReport(const std::string& program, std::string& error);

}  // namespace skylanders
