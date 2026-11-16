#include <cstdint>
#include <filesystem>
#include <vector>

#include "portal_rex/png_file.h"
#include "test_util.h"

namespace fs = std::filesystem;

int main() {
  const fs::path dir = fs::temp_directory_path() / "skylanders_png_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);

  const fs::path png = dir / "sample.png";
  const int w = 3, h = 2;
  const std::vector<uint8_t> rgba = {
      255, 0,   0,   255,  0,   255, 0,   200,  0,   0,   255, 128,
      10,  20,  30,  255,  40,  50,  60,  0,    200, 210, 220, 255,
  };

  CHECK(skylanders::WritePng(png, w, h, rgba.data()));
  CHECK(fs::is_regular_file(png, ec));
  CHECK(!fs::exists(png.string() + ".part", ec));

  int out_w = 0, out_h = 0;
  std::vector<uint8_t> decoded;
  CHECK(skylanders::ReadPng(png, out_w, out_h, decoded));
  CHECK(out_w == w);
  CHECK(out_h == h);
  CHECK(decoded == rgba);

  // Invalid inputs fail cleanly without leaving stray state.
  CHECK(!skylanders::WritePng(dir / "bad.png", 0, 2, rgba.data()));
  CHECK(!skylanders::ReadPng(dir / "missing.png", out_w, out_h, decoded));
  CHECK(out_w == 0 && out_h == 0 && decoded.empty());

  fs::remove_all(dir, ec);
  return Finish("png_file");
}
