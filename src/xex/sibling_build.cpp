#include "xex/sibling_build.h"

#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <vector>
#endif

namespace skylanders::xex {

namespace {

std::filesystem::path SiblingPath(const std::filesystem::path& own, std::string_view exe_name) {
  const std::u8string name(reinterpret_cast<const char8_t*>(exe_name.data()), exe_name.size());
  return own.parent_path() / std::filesystem::path(name);
}

}  // namespace

#if defined(_WIN32)

std::optional<int> RunSiblingBuild(std::string_view exe_name) {
  std::wstring own(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, own.data(), static_cast<DWORD>(own.size()));
    if (n == 0) return std::nullopt;
    if (n < own.size()) {
      own.resize(n);
      break;
    }
    own.resize(own.size() * 2);
  }
  const std::filesystem::path sibling = SiblingPath(std::filesystem::path(own), exe_name);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(sibling, ec)) return std::nullopt;

  // This command line without its program name (quoted or not), after the sibling's quoted path.
  const std::wstring line = GetCommandLineW();
  size_t rest = 0;
  if (!line.empty() && line[0] == L'"') {
    const size_t close = line.find(L'"', 1);
    rest = close == std::wstring::npos ? line.size() : close + 1;
  } else {
    const size_t space = line.find_first_of(L" \t");
    rest = space == std::wstring::npos ? line.size() : space;
  }
  std::wstring command = L"\"" + sibling.wstring() + L"\"" + line.substr(rest);

  // A job that ends with this process: when Core of Light (or anything) ends this game, the started one ends too.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  }
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info = {};
  if (!CreateProcessW(sibling.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                      &startup, &info)) {
    if (job) CloseHandle(job);
    return std::nullopt;
  }
  // Inside another job without breakaway this fails; the game then just isn't ended along with this one.
  if (job) AssignProcessToJobObject(job, info.hProcess);
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(info.hProcess, &code);
  CloseHandle(info.hProcess);
  if (job) CloseHandle(job);
  return static_cast<int>(code);
}

#else

std::optional<int> RunSiblingBuild(std::string_view exe_name) {
  std::error_code ec;
  const std::filesystem::path own = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec || own.empty()) return std::nullopt;
  const std::filesystem::path sibling = SiblingPath(own, exe_name);
  if (!std::filesystem::is_regular_file(sibling, ec)) return std::nullopt;

  std::vector<std::string> args;
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (size_t pos = 0; pos < raw.size();) {
    const size_t nul = raw.find('\0', pos);
    args.push_back(raw.substr(pos, nul == std::string::npos ? std::string::npos : nul - pos));
    pos = nul == std::string::npos ? raw.size() : nul + 1;
  }
  const std::string path = sibling.string();
  if (args.empty()) {
    args.push_back(path);
  } else {
    args[0] = path;
  }
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  execv(path.c_str(), argv.data());
  return std::nullopt;  // execv returned: it failed
}

#endif

}  // namespace skylanders::xex
