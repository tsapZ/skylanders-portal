#include "portal_rex/problem_report.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "portal/data_folder.h"
#include "portal_rex/utf8_path.h"

namespace skylanders {
namespace fs = std::filesystem;
namespace {

#if defined(_WIN32)
std::string FromWide(const std::wstring& text) {
  if (text.empty()) return {};
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

std::string Environment(const wchar_t* name) {
  const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) return {};
  std::wstring value(size, L'\0');
  const DWORD length = GetEnvironmentVariableW(name, value.data(), size);
  if (length == 0 || length >= size) return {};
  value.resize(length);
  return FromWide(value);
}

std::string ComputerName() {
  wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD size = DWORD(std::size(name));
  if (!GetComputerNameW(name, &size)) return {};
  return FromWide(std::wstring(name, size));
}

fs::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  fs::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) result = path;
  CoTaskMemFree(path);
  return result;
}

fs::path ExeFolder() {
  std::wstring exe(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    if (length == 0) return {};
    if (length < exe.size()) {
      exe.resize(length);
      return fs::path(exe).parent_path();
    }
    exe.resize(exe.size() * 2);
  }
}

// The desktop, which follows OneDrive when the shell moved it; Documents and then the working
// folder when that isn't there.
fs::path ReportFolder() {
  fs::path folder = KnownFolder(FOLDERID_Desktop);
  if (folder.empty()) folder = KnownFolder(FOLDERID_Documents);
  if (!folder.empty()) return folder;
  std::error_code ec;
  return fs::current_path(ec);
}
#else
// The game's folder from /proc/self/exe (Linux; elsewhere it isn't there and the folder stays empty).
// Without it a report made on Linux had no settings file, which sits next to the program, and its
// logs only from the working folder (Core of Light #135: a validation line in the toml couldn't be
// checked).
fs::path ExeFolder() {
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : exe.parent_path();
}

fs::path ReportFolder() {
  const fs::path folder = DocumentsFolder();
  if (!folder.empty()) return folder;
  std::error_code ec;
  return fs::current_path(ec);
}
#endif

}  // namespace

std::vector<std::pair<std::string, std::string>> PrivateWords() {
  std::vector<std::pair<std::string, std::string>> words;
#if defined(_WIN32)
  std::string profile = Environment(L"USERPROFILE");
  while (!profile.empty() && (profile.back() == '\\' || profile.back() == '/')) profile.pop_back();
  if (!profile.empty()) {
    words.emplace_back(profile, "%USERPROFILE%");
    std::string other = profile;
    for (char& c : other) {
      if (c == '\\') c = '/';
      else if (c == '/') c = '\\';
    }
    if (other != profile) words.emplace_back(std::move(other), "%USERPROFILE%");
  }
  const std::string user = Environment(L"USERNAME");
  if (user.size() >= 3) words.emplace_back(user, "%USERNAME%");
  const std::string computer = ComputerName();
  if (computer.size() >= 3) words.emplace_back(computer, "%COMPUTERNAME%");
#else
  // Linux (CoL #135: a report from Linux kept /home/<name>): the same three words, from HOME,
  // USER or LOGNAME and the host name.
  const char* home = std::getenv("HOME");
  const char* user = std::getenv("USER");
  if (user == nullptr || *user == '\0') user = std::getenv("LOGNAME");
  char host[256] = {};
  if (gethostname(host, sizeof(host) - 1) != 0) host[0] = '\0';
  words = PosixPrivateWords(home ? home : "", user ? user : "", host);
#endif
  return words;
}

fs::path WriteGameProblemReport(const std::string& program, std::string& error) {
  error.clear();
  // Scripted test runs set SKYLANDERS_REPORT_DIR, so a test's report lands in its scratch folder and
  // not on the owner's desktop (RULES.md: tests never touch the owner's data).
  fs::path folder;
  if (const char* test_dir = std::getenv("SKYLANDERS_REPORT_DIR"); test_dir && *test_dir) {
    folder = fs::path(test_dir);  // a scratch folder, ASCII
  } else {
    folder = ReportFolder();
  }
  if (folder.empty()) {
    error = "there is no folder to write the report to";
    return {};
  }
  const std::time_t now = std::time(nullptr);
  std::tm when{};
#if defined(_WIN32)
  localtime_s(&when, &now);
#else
  localtime_r(&now, &when);
#endif
  char stamp[32] = {};
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M", &when);
  const fs::path path = folder / (std::string("Skylanders-problem-report-") + stamp + ".txt");
  std::error_code ec;
  const fs::path work_dir = fs::current_path(ec);
  const std::string report = BuildGameReport(ec ? fs::path() : work_dir, ExeFolder(), program, PrivateWords());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "cannot write " + Utf8Path(path);
    return {};
  }
  out << report;
  out.close();
  if (!out) {
    error = "cannot write " + Utf8Path(path);
    return {};
  }
  return path;
}

}  // namespace skylanders
