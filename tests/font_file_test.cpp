#include "portal_rex/font_file.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_util.h"

namespace fs = std::filesystem;
using skylanders::menu_draw::FindFallbackFontPaths;
using skylanders::menu_draw::IsLoadableFontFile;

namespace {

void WriteBytes(const fs::path& path, const std::vector<unsigned char>& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

// A file with the given sfnt tag and size; only the header and length matter to the check.
void WriteSfnt(const fs::path& path, std::uint32_t tag, size_t size) {
  std::vector<unsigned char> bytes(size, 0);
  if (size >= 4) {
    bytes[0] = static_cast<unsigned char>((tag >> 24) & 0xFF);
    bytes[1] = static_cast<unsigned char>((tag >> 16) & 0xFF);
    bytes[2] = static_cast<unsigned char>((tag >> 8) & 0xFF);
    bytes[3] = static_cast<unsigned char>(tag & 0xFF);
  }
  WriteBytes(path, bytes);
}

void TestIsLoadableFontFile(const fs::path& dir) {
  const fs::path ttf = dir / "valid.ttf";
  const fs::path otf = dir / "valid.otf";
  const fs::path ttc = dir / "valid.ttc";
  const fs::path apple_true = dir / "valid-true.ttf";
  const fs::path too_short = dir / "short.ttf";
  const fs::path not_font = dir / "notes.txt";
  const fs::path empty = dir / "empty.ttf";

  WriteSfnt(ttf, 0x00010000u, 200);
  WriteSfnt(otf, 0x4F54544Fu /* 'OTTO' */, 200);
  WriteSfnt(ttc, 0x74746366u /* 'ttcf' */, 200);
  WriteSfnt(apple_true, 0x74727565u /* 'true' */, 200);
  // Exactly the 100-byte minimum ImGui rejects (it asserts on data_size <= 100).
  WriteSfnt(too_short, 0x00010000u, 100);
  WriteBytes(not_font, std::vector<unsigned char>(200, 'A'));
  WriteBytes(empty, {});

  CHECK(IsLoadableFontFile(ttf));
  CHECK(IsLoadableFontFile(otf));
  CHECK(IsLoadableFontFile(ttc));
  CHECK(IsLoadableFontFile(apple_true));
  CHECK(!IsLoadableFontFile(too_short));
  CHECK(!IsLoadableFontFile(not_font));
  CHECK(!IsLoadableFontFile(empty));
  CHECK(!IsLoadableFontFile(dir / "missing.ttf"));
}

void TestFindFallbackOrder(const fs::path& dir) {
  const fs::path first = dir / "first.ttf";
  const fs::path short_file = dir / "short.ttf";
  const fs::path second = dir / "second.ttf";
  const fs::path not_font = dir / "notes.txt";
  const fs::path missing = dir / "missing.ttf";
  WriteSfnt(first, 0x00010000u, 200);
  WriteSfnt(short_file, 0x00010000u, 100);
  WriteSfnt(second, 0x00010000u, 250);
  WriteBytes(not_font, std::vector<unsigned char>(200, 'A'));

  // Invalid, blank and missing candidates are dropped; the valid ones keep their order.
  const std::vector<std::string> paths = FindFallbackFontPaths(
      {not_font.string().c_str(), "", first.string().c_str(), short_file.string().c_str(),
       second.string().c_str(), missing.string().c_str(), nullptr});
  CHECK(paths.size() == 2);
  if (paths.size() == 2) {
    CHECK(paths[0] == first.string());
    CHECK(paths[1] == second.string());
  }

  // Nothing usable -> empty, so the caller keeps the game font alone.
  const std::vector<std::string> none =
      FindFallbackFontPaths({not_font.string().c_str(), missing.string().c_str()});
  CHECK(none.empty());
}

}  // namespace

int main() {
  const auto unique =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path dir = fs::temp_directory_path() / ("skylanders_font_file_test_" + unique);
  std::error_code ec;
  fs::create_directories(dir, ec);

  TestIsLoadableFontFile(dir);
  TestFindFallbackOrder(dir);

  fs::remove_all(dir, ec);
  return Finish("font_file");
}
