// menu_favourites: the favourites file and the characters it picks (SkylandersCoreOfLight #93).

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "portal/menu_favourites.h"
#include "test_util.h"

using namespace skylanders::portal;
namespace fs = std::filesystem;

int main() {
  const fs::path dir = fs::temp_directory_path() / L"skylanders_menu_favourites_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  const fs::path file = dir / L"portal_menu_favourites_p1.txt";

  // A missing file has no favourites.
  CHECK(LoadFavourites(file).empty());

  // One name per line, blank lines skipped, UTF-8 kept, a trailing CR dropped.
  {
    std::ofstream out(file, std::ios::binary);
    out << "Chop Chop\n\n\xC3\x9C" "bercharge\r\nSpyro\n";
  }
  std::vector<std::string> names = LoadFavourites(file);
  CHECK(names.size() == 3);
  CHECK(names.size() == 3 && names[0] == "Chop Chop");
  CHECK(names.size() == 3 && names[1] == "\xC3\x9C" "bercharge");
  CHECK(names.size() == 3 && names[2] == "Spyro");

  // Toggling adds at the end and removes the first match.
  CHECK(ToggleFavourite(names, "Eruptor"));
  CHECK(names.back() == "Eruptor");
  CHECK(!ToggleFavourite(names, "Spyro"));
  CHECK(!HasFavourite(names, "Spyro"));
  CHECK(HasFavourite(names, "Chop Chop"));

  // Writing keeps the order.
  SaveFavourites(file, names);
  CHECK(LoadFavourites(file) == names);

  // An empty list leaves an empty file.
  SaveFavourites(file, {});
  CHECK(LoadFavourites(file).empty());

  // The characters picked from the library, in its order and each once.
  const std::vector<std::string> characters = {"Spyro", "Chop Chop", "Bash", "Chop Chop"};
  const std::vector<std::string> favourites = {"Chop Chop", "Eruptor"};
  const std::vector<size_t> picked = FavouriteCharacters(characters, favourites);
  CHECK(picked.size() == 2);
  CHECK(picked.size() == 2 && picked[0] == 1 && picked[1] == 3);

  fs::remove_all(dir, ec);
  return Finish("menu_favourites");
}
