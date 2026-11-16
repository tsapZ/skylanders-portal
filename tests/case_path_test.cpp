#include "alchemy/case_path.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_util.h"

namespace fs = std::filesystem;

namespace {

void TestResolveCasePath() {
  const auto unique =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path base = fs::temp_directory_path() / ("skylanders_case_path_test_" + unique);
  fs::create_directories(base / "Level");
  {
    std::ofstream out(base / "Level" / "Title.arc", std::ios::binary);
    out << "ok";
  }

  const fs::path exact = skylanders::alchemy::ResolveCasePath(base / "Level" / "Title.arc");
  CHECK(fs::exists(exact));

  const fs::path resolved = skylanders::alchemy::ResolveCasePath(base / "level" / "title.arc");
  CHECK(fs::exists(resolved));
#ifndef _WIN32
  // ResolveCasePath is a no-op on Windows because the filesystem is already case-insensitive.
  CHECK(resolved.filename() == "Title.arc");
#endif

  const fs::path missing = skylanders::alchemy::ResolveCasePath(base / "level" / "missing.arc");
  CHECK(!fs::exists(missing));

  fs::remove_all(base);
}

}  // namespace

int main() {
  TestResolveCasePath();
  return g_failures == 0 ? 0 : 1;
}
