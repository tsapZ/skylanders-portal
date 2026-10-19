#include "portal_rex/settings_file.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace skylanders {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

// Where a line's comment starts (a '#' outside a quoted string after the '='), or npos.
size_t CommentStart(const std::string& line) {
  const size_t eq = line.find('=');
  if (eq == std::string::npos) return std::string::npos;
  bool quoted = false;
  for (size_t i = eq + 1; i < line.size(); ++i) {
    if (line[i] == '\\' && quoted) {
      ++i;
    } else if (line[i] == '"') {
      quoted = !quoted;
    } else if (line[i] == '#' && !quoted) {
      return i;
    }
  }
  return std::string::npos;
}

// The setting a line assigns ("portal_mode" for `portal_mode = "usb"   # comment`), or empty.
std::string KeyOf(const std::string& line) {
  const size_t start = line.find_first_not_of(" \t");
  if (start == std::string::npos || line[start] == '#' || line[start] == '[') return {};
  const size_t eq = line.find('=', start);
  if (eq == std::string::npos) return {};
  std::string key = line.substr(start, eq - start);
  key.erase(key.find_last_not_of(" \t") + 1);
  return key;
}

}  // namespace

void WriteSetting(const std::filesystem::path& path, std::string_view category,
                  std::string_view name, const std::string& toml_value) {
  std::vector<std::string> lines;
  {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
  }
  const bool crlf = !lines.empty() && !lines[0].empty() && lines[0].back() == '\r';
  for (std::string& line : lines) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
  }
  const std::string entry = std::string(name) + " = " + toml_value;
  bool done = false;
  for (std::string& line : lines) {
    if (KeyOf(line) == name) {
      // The comment after the value stays, in its column if the new value leaves room.
      const size_t comment = CommentStart(line);
      std::string replaced = entry;
      if (comment != std::string::npos) {
        replaced.append(comment > replaced.size() + 1 ? comment - replaced.size() : 2, ' ');
        replaced += line.substr(comment);
      }
      line = replaced;
      done = true;
      break;
    }
  }
  if (!done) {
    const std::string quoted = "[\"" + std::string(category) + "\"]";
    const std::string bare = "[" + std::string(category) + "]";
    for (size_t i = 0; i < lines.size() && !done; ++i) {
      std::string header = lines[i];
      header.erase(header.find_last_not_of(" \t") + 1);
      if (header == quoted || header == bare) {
        lines.insert(lines.begin() + ptrdiff_t(i) + 1, entry);
        done = true;
      }
    }
  }
  if (!done) {
    if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
    lines.push_back("[\"" + std::string(category) + "\"]");
    lines.push_back(entry);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    REXLOG_WARN("Settings: cannot write {}", PathUtf8(path));
    return;
  }
  for (const std::string& line : lines) out << line << (crlf ? "\r\n" : "\n");
}

bool StoreSetting(const std::filesystem::path& path, std::string_view name,
                  const std::string& value) {
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(name);
  if (!info) {
    REXLOG_WARN("Settings: no setting {}", name);
    return false;
  }
  // Copied out: the pointer is only valid until the registry changes.
  const std::string category = info->category;
  const rex::cvar::FlagType type = info->type;
  const rex::cvar::Lifecycle lifecycle = info->lifecycle;
  const std::vector<std::string> allowed = info->constraints.allowed_values;
  if (!allowed.empty() && std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
    REXLOG_WARN("Settings: {} does not take '{}'", name, value);
    return false;
  }
  // Start-up-only settings refuse a new value while running; they go into the file only.
  if (!rex::cvar::SetFlagByName(name, value) && lifecycle != rex::cvar::Lifecycle::kInitOnly) {
    REXLOG_WARN("Settings: {} rejected '{}'", name, value);
    return false;
  }
  REXLOG_INFO("Settings: {} = {}", name, value);
  if (path.empty()) return true;
  std::string toml = value;
  if (type == rex::cvar::FlagType::String) {
    toml.clear();
    for (char c : value) {
      if (c == '"' || c == '\\') toml += '\\';
      toml += c;
    }
    toml = "\"" + toml + "\"";
  }
  WriteSetting(path, category, name, toml);
  return true;
}

}  // namespace skylanders
