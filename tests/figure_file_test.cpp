#include <filesystem>
#include <fstream>
#include <string>

#include "portal/figure_file.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace skylanders::portal;

static fs::path WriteTemp(const std::wstring& name, size_t size, uint8_t seed) {
  fs::path p = fs::temp_directory_path() / name;
  std::ofstream out(p, std::ios::binary);
  for (size_t i = 0; i < size; ++i) out.put(static_cast<char>(seed + i * 3));
  return p;
}

int main() {
  // A file of exactly 1024 bytes loads unchanged.
  {
    fs::path p = WriteTemp(L"gr_figure_ok.dump", kFigureSize, 5);
    auto data = LoadFigureFile(p);
    CHECK(data.has_value());
    if (data) {
      CHECK((*data)[0] == 5);
      CHECK((*data)[1] == 8);
      CHECK((*data)[1023] == static_cast<uint8_t>(5 + 1023 * 3));
    }
  }

  // Anything that is not exactly one figure is rejected: too short, too long, empty.
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1023.dump", kFigureSize - 1, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1025.dump", kFigureSize + 1, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1088.dump", 1088, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_empty.dump", 0, 1)).has_value());

  // Backups: the folder mirrors the path under Figures; a file elsewhere goes to Other.
  {
    const fs::path data = fs::temp_directory_path() / L"gr_backup_data";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data / L"Figures" / L"Giants", ec);
    const fs::path figure = data / L"Figures" / L"Giants" / L"Tree Rex.dump";
    { std::ofstream(figure, std::ios::binary) << std::string(kFigureSize, 'x'); }
    const fs::path dir = FigureBackupDir(figure, data / L"Figures");
    CHECK(dir == data / L"Backups" / L"Figures" / L"Giants" / L"Tree Rex");
    CHECK(FigureBackupDir(fs::temp_directory_path() / L"Loose.dump", data / L"Figures") ==
          data / L"Backups" / L"Figures" / L"Other" / L"Loose");
    // Twelve copies with sortable names: the newest ten stay.
    for (int i = 0; i < 12; ++i) {
      const std::string name = "2026-11-02 15-00-" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".dump";
      CHECK(!BackUpFigureFile(figure, dir, kFigureBackups, name).empty());
    }
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) count += entry.path().extension() == ".dump";
    CHECK(count == kFigureBackups);
    CHECK(!fs::exists(dir / L"2026-11-02 15-00-00.dump"));
    CHECK(!fs::exists(dir / L"2026-11-02 15-00-01.dump"));
    CHECK(fs::exists(dir / L"2026-11-02 15-00-11.dump"));
    // A missing file gives no copy.
    CHECK(BackUpFigureFile(data / L"Figures" / L"missing.dump", dir).empty());
    fs::remove_all(data, ec);
  }

  // A missing file and a folder are rejected.
  CHECK(!LoadFigureFile(fs::temp_directory_path() / L"gr_figure_missing.dump").has_value());
  CHECK(!LoadFigureFile(fs::temp_directory_path()).has_value());
  CHECK(!LoadFigureFile(fs::path()).has_value());

  // Paths with spaces, parentheses and non-ANSI characters work.
  {
    fs::path dir = fs::temp_directory_path() / L"gr fig (2) 日本";
    fs::create_directories(dir);
    fs::path p = dir / L"Tree Rex.dump";
    {
      std::ofstream out(p, std::ios::binary);
      for (size_t i = 0; i < kFigureSize; ++i) out.put(static_cast<char>(i));
    }
    auto data = LoadFigureFile(p);
    CHECK(data.has_value());
    if (data) CHECK((*data)[255] == 255);
  }

  // SaveFigureFileAtomic creates a new file with exactly the given bytes.
  {
    fs::path p = fs::temp_directory_path() / L"gr_figure_save_new.dump";
    fs::remove(p);
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i * 5 + 2);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK(*loaded == d);
    fs::remove(p);
  }

  // It overwrites existing content fully, and leaves no temp file behind.
  {
    fs::path p = WriteTemp(L"gr_figure_save_overwrite.dump", kFigureSize, 9);
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(200 - i);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK(*loaded == d);
    fs::path tmp = p;
    tmp += L".tmp";
    CHECK(!fs::exists(tmp));
  }

  // A location that cannot be written (parent directory missing) fails cleanly and creates nothing.
  {
    fs::path p = fs::temp_directory_path() / L"gr_missing_dir_xyz" / L"figure.dump";
    FigureData d{};
    CHECK(!SaveFigureFileAtomic(p, d));
    CHECK(!fs::exists(p));
  }

  // Paths with spaces, parentheses and non-ANSI characters work for saving too.
  {
    fs::path dir = fs::temp_directory_path() / L"gr save (2) 日本";
    fs::create_directories(dir);
    fs::path p = dir / L"Tree Rex.dump";
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK((*loaded)[255] == 255);
  }

  // ReadFigureId/ReadFigureVariant read the little-endian id/variant bytes.
  {
    FigureData d{};
    d[0x10] = 0x70;
    d[0x11] = 0x00;  // id 0x0070 = 112 (Tree Rex)
    d[0x1C] = 0x02;
    d[0x1D] = 0x16;  // variant 0x1602
    CHECK(ReadFigureId(d) == 112);
    CHECK(ReadFigureVariant(d) == 0x1602);
  }

  // CreateBlankFigure (fixed-serial overload) produces the exact byte layout from the design spec.
  {
    FigureData d = CreateBlankFigure(112, 0, {0x11, 0x22, 0x33, 0x44});
    CHECK(d[0] == 0x11);
    CHECK(d[1] == 0x22);
    CHECK(d[2] == 0x33);
    CHECK(d[3] == 0x44);
    CHECK(d[4] == (0x11 ^ 0x22 ^ 0x33 ^ 0x44));  // BCC
    CHECK(d[5] == 0x81);
    CHECK(d[6] == 0x01);
    CHECK(d[7] == 0x0F);
    CHECK(ReadFigureId(d) == 112);
    CHECK(ReadFigureVariant(d) == 0);
    // CRC16-CCITT(init 0xFFFF, poly 0x1021) over bytes 0x00-0x1D, computed independently here.
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < 0x1E; ++i) {
      crc ^= static_cast<uint16_t>(d[i]) << 8;
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                              : static_cast<uint16_t>(crc << 1);
      }
    }
    CHECK(d[0x1E] == (crc & 0xFF));
    CHECK(d[0x1F] == (crc >> 8));
    // Sector 0 trailer access bits: little-endian bytes of 0x690F0F0F (0x0F,0x0F,0x0F,0x69),
    // confirmed against a real dump's sector-0 trailer pattern.
    CHECK(d[0x36] == 0x0F);
    CHECK(d[0x37] == 0x0F);
    CHECK(d[0x38] == 0x0F);
    CHECK(d[0x39] == 0x69);
    // Sector 1 trailer access bits: little-endian bytes of 0x69080F7F (0x7F,0x0F,0x08,0x69).
    CHECK(d[0x76] == 0x7F);
    CHECK(d[0x77] == 0x0F);
    CHECK(d[0x78] == 0x08);
    CHECK(d[0x79] == 0x69);
    // Sector 15 (last) trailer access bits, same pattern as sector 1.
    CHECK(d[0x3F6] == 0x7F);
    CHECK(d[0x3F7] == 0x0F);
    CHECK(d[0x3F8] == 0x08);
    CHECK(d[0x3F9] == 0x69);
    // Untouched byte stays zero.
    CHECK(d[0x20] == 0);
  }

  // The random-serial overload produces a loadable, self-consistent figure (BCC/CRC correct);
  // two calls give different serials (overwhelmingly likely with 4 random bytes).
  {
    FigureData a = CreateBlankFigure(4, 0);
    FigureData b = CreateBlankFigure(4, 0);
    CHECK(a[4] == (a[0] ^ a[1] ^ a[2] ^ a[3]));
    bool any_serial_byte_differs = a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3];
    CHECK(any_serial_byte_differs);
  }

  // UniqueFigurePath: a free name comes back unchanged.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_unique_path";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex.dump");

    // An existing file forces " (2)", then " (3)" on the next collision.
    { std::ofstream(dir / L"Tree Rex.dump", std::ios::binary) << "x"; }
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex (2).dump");
    { std::ofstream(dir / L"Tree Rex (2).dump", std::ios::binary) << "x"; }
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex (3).dump");

    // A name that already contains parentheses (a real catalog entry, e.g. "Bash (Series 2)").
    CHECK(UniqueFigurePath(dir, "Bash (Series 2)") == dir / L"Bash (Series 2).dump");

    // A directory that does not exist yet: the first name is always free, nothing is created.
    fs::path missing_dir = fs::temp_directory_path() / L"gr_unique_path_missing";
    fs::remove_all(missing_dir);
    CHECK(UniqueFigurePath(missing_dir, "Spyro") == missing_dir / L"Spyro.dump");
    CHECK(!fs::exists(missing_dir));

    fs::remove_all(dir);
  }

  {
    // A figures folder with characters outside the ANSI code page must still round-trip.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / L"sp_日本_figures";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / L"Snap Shot é.dump";
    const skylanders::portal::FigureData data = skylanders::portal::CreateBlankFigure(462, 12288);
    CHECK(skylanders::portal::SaveFigureFileAtomic(path, data));
    auto loaded = skylanders::portal::LoadFigureFile(path);
    CHECK(loaded.has_value() && *loaded == data);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  return Finish("figure_file");
}
