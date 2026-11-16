#include "portal/menu_favourites.h"

#include <algorithm>
#include <fstream>

namespace skylanders::portal {

std::vector<std::string> LoadFavourites(const std::filesystem::path& file) {
  std::vector<std::string> names;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) names.push_back(std::move(line));
  }
  return names;
}

void SaveFavourites(const std::filesystem::path& file, const std::vector<std::string>& names) {
  std::ofstream out(file, std::ios::trunc);
  for (const std::string& name : names) out << name << "\n";
}

bool HasFavourite(const std::vector<std::string>& names, std::string_view name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool ToggleFavourite(std::vector<std::string>& names, std::string_view name) {
  const auto it = std::find(names.begin(), names.end(), name);
  if (it != names.end()) {
    names.erase(it);
    return false;
  }
  names.emplace_back(name);
  return true;
}

std::vector<size_t> FavouriteCharacters(const std::vector<std::string>& characters,
                                        const std::vector<std::string>& favourites) {
  std::vector<size_t> indices;
  for (size_t i = 0; i < characters.size(); ++i) {
    if (HasFavourite(favourites, characters[i])) indices.push_back(i);
  }
  return indices;
}

}  // namespace skylanders::portal
