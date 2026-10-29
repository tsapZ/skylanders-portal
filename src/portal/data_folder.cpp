#include "portal/data_folder.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace skylanders {
namespace fs = std::filesystem;
namespace {

// Each game's executable name (ReXApp's name, its old folder in Documents) and its folder now.
constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kGames{{
    {"ssarecomp", "Spyro's Adventure"},
    {"giantsrecomp", "Giants"},
    {"swapforcerecomp", "Swap Force"},
    {"trapteamrecomp", "Trap Team"},
    {"superchargersrecomp", "SuperChargers"},
    {"imaginatorsrecomp", "Imaginators"},
}};

#ifdef _WIN32
fs::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  fs::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) result = path;
  CoTaskMemFree(path);
  return result;
}
#endif

std::string Utf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

}  // namespace

fs::path DocumentsFolder() {
#ifdef _WIN32
  if (fs::path documents = KnownFolder(FOLDERID_Documents); !documents.empty()) return documents;
#endif
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / "Documents";
}

fs::path DefaultDataFolder() { return DocumentsFolder() / "Skylanders"; }

fs::path DataFolderPointer() {
#ifdef _WIN32
  fs::path appdata = KnownFolder(FOLDERID_RoamingAppData);
#else
  const char* config = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  fs::path appdata = config ? fs::path(config) : fs::path(home ? home : ".") / ".config";
#endif
  if (appdata.empty()) return {};
  return appdata / "Skylanders" / "data-folder.txt";
}

fs::path PortableFolder() {
#ifdef _WIN32
  std::wstring exe(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    if (n == 0) return {};
    if (n < exe.size()) {
      exe.resize(n);
      break;
    }
    exe.resize(exe.size() * 2);
  }
  fs::path dir = fs::path(exe).parent_path();
  std::error_code ec;
  for (int up = 0; up <= 3 && !dir.empty(); ++up) {
    if (fs::is_regular_file(dir / kPortableMarker, ec)) return dir;
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
#endif
  return {};
}

fs::path DataFolder() {
  if (const fs::path portable = PortableFolder(); !portable.empty()) return portable / "Skylanders";
  const fs::path pointer = DataFolderPointer();
  if (!pointer.empty()) {
    std::ifstream in(pointer, std::ios::binary);
    std::string line;
    if (in && std::getline(in, line)) {
      if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // a BOM
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
      if (!line.empty()) return fs::path(std::u8string(line.begin(), line.end()));
    }
  }
  return DefaultDataFolder();
}

bool SetDataFolder(const fs::path& folder) {
  const fs::path pointer = DataFolderPointer();
  if (pointer.empty()) return false;
  std::error_code ec;
  if (folder.empty() || fs::equivalent(folder, DefaultDataFolder(), ec) || folder == DefaultDataFolder()) {
    fs::remove(pointer, ec);
    return !fs::exists(pointer, ec);
  }
  fs::create_directories(pointer.parent_path(), ec);
  std::ofstream out(pointer, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  // With a byte order mark: the launcher's uninstaller (Inno Setup) then reads it as UTF-8.
  out << "\xEF\xBB\xBF" << Utf8(folder) << "\r\n";
  return bool(out);
}

bool IsGameFolderName(const fs::path& name) {
  const std::string text = Utf8(name);
  for (const auto& [exe, folder] : kGames) {
    if (text == folder) return true;
  }
  return false;
}

std::string_view GameFolderForExe(std::string_view exe_name) {
  for (const auto& [exe, folder] : kGames) {
    if (exe == exe_name) return folder;
  }
  return {};
}

fs::path SharedBase(const fs::path& user_data_root) {
  if (user_data_root.parent_path().empty()) return user_data_root;
  if (IsGameFolderName(user_data_root.filename())) return user_data_root.parent_path();
  return user_data_root.parent_path() / "Skylanders";
}

CopyResult CopyFolderVerified(const fs::path& from, const fs::path& to,
                              const std::function<void(const fs::path&)>& on_file) {
  CopyResult result;
  std::error_code ec;
  if (fs::exists(to, ec)) {
    result.error = "the target is already there: " + Utf8(to);
    return result;
  }
  fs::path partial = to;
  partial += ".partial";
  fs::remove_all(partial, ec);  // left from a copy that didn't finish
  ec.clear();
  if (!fs::create_directories(partial, ec) && ec) {
    result.error = "cannot create " + Utf8(partial) + ": " + ec.message();
    return result;
  }
  for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    std::error_code file_ec;
    const fs::path rel = fs::relative(it->path(), from, file_ec);
    if (file_ec) {
      result.error = "cannot place " + Utf8(it->path()) + ": " + file_ec.message();
      return result;
    }
    const fs::path dest = partial / rel;
    if (it->is_directory(file_ec)) {
      fs::create_directories(dest, file_ec);
      continue;
    }
    if (!it->is_regular_file(file_ec)) continue;
    if (on_file) on_file(it->path());
    fs::create_directories(dest.parent_path(), file_ec);
    file_ec.clear();
    if (!fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, file_ec) || file_ec) {
      result.error = "cannot copy " + Utf8(it->path()) + ": " + file_ec.message();
      return result;
    }
    const uintmax_t size = fs::file_size(it->path(), file_ec);
    std::error_code dest_ec;
    if (file_ec || fs::file_size(dest, dest_ec) != size || dest_ec) {
      result.error = "the copy of " + Utf8(it->path()) + " has the wrong size";
      return result;
    }
    ++result.files;
    result.bytes += size;
  }
  if (ec) {
    result.error = "cannot read " + Utf8(from) + ": " + ec.message();
    return result;
  }
  fs::rename(partial, to, ec);
  if (ec) {
    result.error = "cannot rename " + Utf8(partial) + ": " + ec.message();
    return result;
  }
  result.ok = true;
  return result;
}

bool AllFilesThere(const fs::path& old_dir, const fs::path& new_dir) {
  std::error_code ec;
  if (!fs::is_directory(new_dir, ec)) return false;
  for (auto it = fs::recursive_directory_iterator(old_dir, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec)) continue;
    if (it->path().filename() == kMovedNote) continue;
    const fs::path rel = fs::relative(it->path(), old_dir, file_ec);
    if (file_ec || !fs::exists(new_dir / rel, file_ec)) return false;
  }
  return !ec;
}

void WriteMovedNote(const fs::path& old_dir, const fs::path& new_dir) {
  std::ofstream out(old_dir / kMovedNote, std::ios::binary | std::ios::trunc);
  out << Utf8(new_dir) << "\r\n"
      << "This folder was copied there (Skylanders: one data folder for every game). The copy is "
         "what the games use now; Core of Light asks whether this old folder may be deleted.\r\n";
}

}  // namespace skylanders
