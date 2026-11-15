#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace skylanders::alchemy {

inline bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// On case-sensitive filesystems, extracted Xbox 360 discs keep mixed/lower case
// differently across pressings (for example level/Title.arc vs level/title.arc).
// Resolve missing components case-insensitively against existing directories.
inline std::filesystem::path ResolveCasePath(const std::filesystem::path& path) {
#ifdef _WIN32
  return path;
#else
  std::error_code ec;
  if (path.empty() || std::filesystem::exists(path, ec)) return path;

  std::filesystem::path cur = path.lexically_normal();
  if (std::filesystem::exists(cur, ec)) return cur;

  std::vector<std::filesystem::path> missing_parts;
  while (!cur.empty() && !std::filesystem::exists(cur, ec)) {
    const std::filesystem::path name = cur.filename();
    const std::filesystem::path parent = cur.parent_path();
    if (!name.empty()) missing_parts.push_back(name);
    if (parent == cur) break;
    cur = parent;
  }
  if (missing_parts.empty()) return path;
  if (cur.empty()) cur = ".";
  if (!std::filesystem::is_directory(cur, ec)) return path;

  for (auto it = missing_parts.rbegin(); it != missing_parts.rend(); ++it) {
    const std::u8string target_u8 = it->u8string();
    const std::string target(target_u8.begin(), target_u8.end());
    std::filesystem::path next = cur / *it;
    if (!std::filesystem::exists(next, ec)) {
      std::filesystem::path matched;
      for (const auto& entry : std::filesystem::directory_iterator(
               cur, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const std::u8string entry_u8 = entry.path().filename().u8string();
        const std::string entry_name(entry_u8.begin(), entry_u8.end());
        if (EqualsIgnoreAsciiCase(entry_name, target)) {
          matched = entry.path();
          break;
        }
      }
      if (matched.empty()) return path;
      next = std::move(matched);
    }
    cur = std::move(next);
  }
  return std::filesystem::exists(cur, ec) ? cur : path;
#endif
}

}  // namespace skylanders::alchemy
