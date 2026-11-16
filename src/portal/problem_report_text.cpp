#include "portal_rex/problem_report.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace skylanders {
namespace fs = std::filesystem;
namespace {

// The report keeps the newest few logs and notes, and only the end of a log, so a long session
// still makes a report a player can attach.
constexpr size_t kMaxLogs = 4;
constexpr size_t kMaxNotes = 3;
constexpr std::uintmax_t kMaxLogBytes = 512 * 1024;

enum class FileKind { kToml, kLog, kNote };

struct DatedFile {
  fs::path path;
  fs::file_time_type time;
};

char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

bool SameIgnoringAsciiCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
  }
  return true;
}

bool StartsWithIgnoringAsciiCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && SameIgnoringAsciiCase(text.substr(0, prefix.size()), prefix);
}

bool EndsWithIgnoringAsciiCase(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         SameIgnoringAsciiCase(text.substr(text.size() - suffix.size()), suffix);
}

// UTF-8 form of a path. Unlike path::string(), never throws for characters outside the ANSI code
// page.
std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string Private(const std::string& text,
                    const std::vector<std::pair<std::string, std::string>>& words) {
  std::string out;
  out.reserve(text.size());
  size_t pos = 0;
  while (pos < text.size()) {
    const std::pair<std::string, std::string>* match = nullptr;
    for (const auto& word : words) {
      if (word.first.empty() || word.first.size() > text.size() - pos) continue;
      if (SameIgnoringAsciiCase(std::string_view(text).substr(pos, word.first.size()), word.first) &&
          (!match || word.first.size() > match->first.size())) {
        match = &word;
      }
    }
    if (match) {
      out += match->second;
      pos += match->first.size();
    } else {
      out += text[pos++];
    }
  }
  return out;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The last `max_bytes` of a file: a log's end says what just went wrong, its start usually doesn't.
std::string ReadTail(const fs::path& path, std::uintmax_t max_bytes) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  const std::streamoff size = in.tellg();
  if (size <= 0) return {};
  const std::streamoff start = size > std::streamoff(max_bytes) ? size - std::streamoff(max_bytes) : 0;
  in.seekg(start, std::ios::beg);
  if (!in) return {};
  std::string text(size_t(size - start), '\0');
  in.read(text.data(), std::streamsize(text.size()));
  text.resize(size_t(in.gcount()));
  return text;
}

// The logs folder in the working folder and in the game's folder, unless they are the same one.
std::vector<fs::path> LogFolders(const fs::path& work_dir, const fs::path& exe_dir) {
  std::vector<fs::path> folders{work_dir / "logs"};
  const fs::path exe_logs = exe_dir / "logs";
  std::error_code ec;
  const bool same = folders.front().lexically_normal() == exe_logs.lexically_normal() ||
                    (fs::exists(folders.front(), ec) && fs::exists(exe_logs, ec) &&
                     fs::equivalent(folders.front(), exe_logs, ec));
  if (!same) folders.push_back(exe_logs);
  return folders;
}

std::vector<DatedFile> FindFiles(const std::vector<fs::path>& folders, FileKind kind) {
  std::vector<DatedFile> files;
  for (const fs::path& folder : folders) {
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code file_ec;
      if (!it->is_regular_file(file_ec)) continue;
      const fs::path& path = it->path();
      const std::string name = Utf8(path.filename());
      const bool wanted =
          kind == FileKind::kToml
              ? EndsWithIgnoringAsciiCase(name, ".toml")
              : kind == FileKind::kLog
                    ? EndsWithIgnoringAsciiCase(name, ".log")
                    : StartsWithIgnoringAsciiCase(name, "problem-") &&
                          EndsWithIgnoringAsciiCase(name, ".txt");
      if (!wanted) continue;
      std::error_code time_ec;
      const fs::file_time_type time = fs::last_write_time(path, time_ec);
      files.push_back({path, time_ec ? fs::file_time_type::min() : time});
    }
  }
  return files;
}

void SortByName(std::vector<DatedFile>& files) {
  std::sort(files.begin(), files.end(), [](const DatedFile& a, const DatedFile& b) {
    return Utf8(a.path.filename()) < Utf8(b.path.filename());
  });
}

// Newest first, keeping at most `max` of them.
void KeepNewest(std::vector<DatedFile>& files, size_t max) {
  std::sort(files.begin(), files.end(), [](const DatedFile& a, const DatedFile& b) {
    if (a.time != b.time) return a.time > b.time;
    return Utf8(a.path.filename()) < Utf8(b.path.filename());
  });
  if (files.size() > max) files.resize(max);
}

void AddPart(std::string& report, const std::string& name, const std::string& text,
             const std::vector<std::pair<std::string, std::string>>& private_words) {
  report += "===== " + name + " =====\n";
  const std::string private_text = Private(text, private_words);
  report += private_text;
  if (!private_text.empty() && private_text.back() != '\n') report += '\n';
}

}  // namespace

std::string BuildGameReport(const fs::path& work_dir, const fs::path& exe_dir,
                            const std::string& program,
                            const std::vector<std::pair<std::string, std::string>>& private_words) {
  std::string report;
  AddPart(report, "info",
          "Game: " + program + "\nMade by the game's Report a problem (without Core of Light)",
          private_words);

  // The game's settings file sits next to the executable.
  std::vector<DatedFile> settings = FindFiles({exe_dir}, FileKind::kToml);
  SortByName(settings);
  for (const DatedFile& file : settings) {
    AddPart(report, program + "/" + Utf8(file.path.filename()), ReadFile(file.path), private_words);
  }

  const std::vector<fs::path> log_folders = LogFolders(work_dir, exe_dir);
  std::vector<DatedFile> logs = FindFiles(log_folders, FileKind::kLog);
  KeepNewest(logs, kMaxLogs);
  for (const DatedFile& file : logs) {
    AddPart(report, program + "/logs/" + Utf8(file.path.filename()),
            ReadTail(file.path, kMaxLogBytes), private_words);
  }

  std::vector<DatedFile> notes = FindFiles(log_folders, FileKind::kNote);
  KeepNewest(notes, kMaxNotes);
  for (const DatedFile& file : notes) {
    AddPart(report, program + "/notes/" + Utf8(file.path.filename()), ReadFile(file.path),
            private_words);
  }
  return report;
}

}  // namespace skylanders
