#include "portal/soul_gem_names.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "portal/figure_catalog.h"
#include "portal/run_logic.h"

namespace skylanders::portal {

bool SameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

std::string_view FileStem(std::string_view path, std::string_view extension) {
  const size_t cut = path.find_last_of("\\/:");
  const std::string_view file = cut == std::string_view::npos ? path : path.substr(cut + 1);
  if (file.size() <= extension.size() || !SameName(file.substr(file.size() - extension.size()), extension)) {
    return {};
  }
  return file.substr(0, file.size() - extension.size());
}

bool InFolder(std::string_view path, std::string_view folder) {
  const size_t file = path.find_last_of("\\/");
  if (file == std::string_view::npos) return false;
  const std::string_view dir = path.substr(0, file);
  const size_t cut = dir.find_last_of("\\/:");
  return SameName(cut == std::string_view::npos ? dir : dir.substr(cut + 1), folder);
}

std::string_view AfterPrefix(std::string_view text, std::string_view prefix) {
  if (text.size() <= prefix.size() || !SameName(text.substr(0, prefix.size()), prefix)) return {};
  return text.substr(prefix.size());
}

std::string SkylanderNamed(std::string_view name) {
  // The catalog is in release order, so the first entry of a name stays (Giants' Bash is Spyro's
  // Adventure's).
  static const std::unordered_map<std::string, std::string_view> by_key = [] {
    std::unordered_map<std::string, std::string_view> map;
    for (const SkylanderInfo& s : AllSkylanders()) map.emplace(run::NameKey(s.name), s.name);
    return map;
  }();
  const std::string key = run::NameKey(name);
  if (key.empty()) return {};
  const auto it = by_key.find(key);
  return it == by_key.end() ? std::string() : std::string(it->second);
}

std::string SkylanderWithId(uint16_t id) {
  for (const SkylanderInfo& s : AllSkylanders()) {
    if (s.id == id) return std::string(s.name);
  }
  return {};
}

}  // namespace skylanders::portal
