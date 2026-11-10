#include "portal/mod_list.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace skylanders::portal {

namespace {

namespace fs = std::filesystem;

// The largest mod.toml read (a manifest is a few lines).
constexpr size_t kMaxManifestBytes = 64 * 1024;

bool IsIdChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '_' || c == '.';
}

bool IsValidId(std::string_view id) {
  return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), IsIdChar);
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// A string value at the start of `s` ("..." with escapes or '...' as it is); `s` is left after
// it. False if there is none or it isn't closed on the line.
bool ReadString(std::string_view& s, std::string& out, std::string& error) {
  out.clear();
  if (s.empty() || (s.front() != '"' && s.front() != '\'')) {
    error = "a value in quotes";
    return false;
  }
  const char quote = s.front();
  s.remove_prefix(1);
  while (!s.empty()) {
    const char c = s.front();
    s.remove_prefix(1);
    if (c == quote) return true;
    if (quote == '"' && c == '\\') {
      if (s.empty()) break;
      const char e = s.front();
      s.remove_prefix(1);
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        default:
          error = std::string("an unknown escape \\") + e;
          return false;
      }
      continue;
    }
    out += c;
  }
  error = "a closing quote";
  return false;
}

// What may follow a value: nothing, or a comment.
bool AtLineEnd(std::string_view s) {
  s = Trim(s);
  return s.empty() || s.front() == '#';
}

}  // namespace

bool ParseModManifest(std::string_view text, ModManifest& manifest, std::string& error) {
  manifest = {};
  error.clear();
  // A UTF-8 byte order mark (Notepad's "UTF-8 with BOM").
  if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  std::vector<std::string> seen;
  size_t line_number = 0;
  while (!text.empty()) {
    ++line_number;
    const size_t end = text.find('\n');
    std::string_view line = Trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    if (line.empty() || line.front() == '#') continue;
    auto fail = [&](const std::string& what) {
      error = "mod.toml line " + std::to_string(line_number) + ": " + what;
      return false;
    };
    const size_t equals = line.find('=');
    if (equals == std::string_view::npos) return fail("expected key = value");
    const std::string key(Trim(line.substr(0, equals)));
    if (key.empty() || !std::all_of(key.begin(), key.end(), IsIdChar)) {
      return fail("expected a key before =");
    }
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) return fail(key + " twice");
    seen.push_back(key);
    std::string_view value = Trim(line.substr(equals + 1));
    std::string* text_field = nullptr;
    std::vector<std::string>* list_field = nullptr;
    if (key == "id") text_field = &manifest.id;
    else if (key == "name") text_field = &manifest.name;
    else if (key == "version") text_field = &manifest.version;
    else if (key == "author") text_field = &manifest.author;
    else if (key == "description") text_field = &manifest.description;
    else if (key == "game") text_field = &manifest.game;
    else if (key == "discs") list_field = &manifest.discs;
    else if (key == "requires") list_field = &manifest.needs;
    else if (key == "conflicts") list_field = &manifest.conflicts;
    std::string read_error;
    if (list_field || (!text_field && !value.empty() && value.front() == '[')) {
      if (value.empty() || value.front() != '[') return fail(key + " needs a list: [\"a\", \"b\"]");
      value.remove_prefix(1);
      std::vector<std::string> items;
      while (true) {
        value = Trim(value);
        if (!value.empty() && value.front() == ']') {
          value.remove_prefix(1);
          break;
        }
        std::string item;
        if (!ReadString(value, item, read_error)) return fail(key + ": expected " + read_error);
        items.push_back(std::move(item));
        value = Trim(value);
        if (!value.empty() && value.front() == ',') {
          value.remove_prefix(1);
          continue;
        }
        if (!value.empty() && value.front() == ']') {
          value.remove_prefix(1);
          break;
        }
        return fail(key + ": expected , or ] (a list stays on one line)");
      }
      if (!AtLineEnd(value)) return fail("something after the list");
      if (list_field) *list_field = std::move(items);
      continue;
    }
    std::string read;
    if (!ReadString(value, read, read_error)) return fail(key + ": expected " + read_error);
    if (!AtLineEnd(value)) return fail("something after the value");
    if (text_field) *text_field = std::move(read);
  }
  return true;
}

std::vector<FoundMod> FindMods(const fs::path& mods_dir) {
  std::vector<FoundMod> found;
  std::error_code ec;
  if (!fs::is_directory(mods_dir, ec)) return found;
  for (const fs::directory_entry& entry : fs::directory_iterator(mods_dir, ec)) {
    std::error_code entry_ec;
    if (!entry.is_directory(entry_ec)) continue;
    FoundMod mod;
    mod.folder = entry.path();
    const std::u8string folder_u8 = entry.path().filename().u8string();
    const std::string folder_name(folder_u8.begin(), folder_u8.end());
    const fs::path manifest_path = entry.path() / "mod.toml";
    std::ifstream file(manifest_path, std::ios::binary);
    if (!IsValidId(folder_name)) {
      mod.problem = "the folder's name can only have letters, digits, - _ and .";
    } else if (!file) {
      mod.problem = "no mod.toml";
    } else {
      std::string text;
      text.resize(kMaxManifestBytes + 1);
      file.read(text.data(), std::streamsize(text.size()));
      text.resize(size_t(file.gcount()));
      std::string error;
      if (text.size() > kMaxManifestBytes) {
        mod.problem = "mod.toml is too long";
      } else if (!ParseModManifest(text, mod.manifest, error)) {
        mod.problem = error;
      } else if (mod.manifest.id.empty()) {
        mod.manifest.id = folder_name;
      } else if (mod.manifest.id != folder_name) {
        mod.problem = "mod.toml says id \"" + mod.manifest.id + "\": the folder must be called that";
        // Found by its folder's name, with the problem.
        mod.manifest.id = folder_name;
      }
      if (mod.problem.empty() && mod.manifest.game.empty()) {
        mod.problem = "mod.toml doesn't say which game it is for (game = \"...\")";
      }
    }
    if (mod.manifest.id.empty()) mod.manifest.id = folder_name;
    if (mod.manifest.name.empty()) mod.manifest.name = mod.manifest.id;
    found.push_back(std::move(mod));
  }
  std::sort(found.begin(), found.end(), [](const FoundMod& a, const FoundMod& b) {
    return a.folder.filename() < b.folder.filename();
  });
  return found;
}

std::vector<std::string> ModListIds(std::string_view enabled) {
  std::vector<std::string> ids;
  while (!enabled.empty()) {
    const size_t end = enabled.find(';');
    const std::string_view id = Trim(enabled.substr(0, end));
    if (!id.empty()) ids.emplace_back(id);
    if (end == std::string_view::npos) break;
    enabled.remove_prefix(end + 1);
  }
  return ids;
}

bool ModListHas(std::string_view enabled, std::string_view id) {
  const std::vector<std::string> ids = ModListIds(enabled);
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string ModListWith(std::string_view enabled, std::string_view id, bool on) {
  std::vector<std::string> ids = ModListIds(enabled);
  ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
  if (on) ids.emplace_back(id);
  std::string list;
  for (const std::string& i : ids) {
    if (!list.empty()) list += ';';
    list += i;
  }
  return list;
}

ModPlan PlanMods(const std::vector<FoundMod>& found, std::string_view enabled, std::string_view game,
                 std::string_view disc_pin) {
  ModPlan plan;
  std::vector<std::string> ids = ModListIds(enabled);
  // Named twice: the first counts.
  std::vector<std::string> unique;
  for (const std::string& id : ids) {
    if (std::find(unique.begin(), unique.end(), id) == unique.end()) unique.push_back(id);
  }
  auto find = [&](std::string_view id) -> const FoundMod* {
    for (const FoundMod& mod : found) {
      if (mod.manifest.id == id) return &mod;
    }
    return nullptr;
  };
  // First which ones can be used on their own, then the requirements between them (a mod that
  // needs a left-out one is left out too, until nothing changes).
  std::vector<std::string> reason(unique.size());
  for (size_t i = 0; i < unique.size(); ++i) {
    const FoundMod* mod = find(unique[i]);
    std::error_code ec;
    if (!mod) {
      reason[i] = "not in the mods folder";
    } else if (!mod->problem.empty()) {
      reason[i] = mod->problem;
    } else if (mod->manifest.game != game) {
      reason[i] = "made for another game (" + mod->manifest.game + ")";
    } else if (!disc_pin.empty() && !mod->manifest.discs.empty() &&
               std::find(mod->manifest.discs.begin(), mod->manifest.discs.end(), disc_pin) ==
                   mod->manifest.discs.end()) {
      reason[i] = "made for another disc";
    } else if (!fs::is_directory(mod->folder / "files", ec)) {
      reason[i] = "no files folder";
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < unique.size(); ++i) {
      if (!reason[i].empty()) continue;
      for (const std::string& need : find(unique[i])->manifest.needs) {
        const auto it = std::find(unique.begin(), unique.end(), need);
        if (it == unique.end() || !reason[size_t(it - unique.begin())].empty()) {
          reason[i] = "needs " + need + " switched on";
          changed = true;
          break;
        }
      }
    }
  }
  // Conflicts: the one switched on first stays.
  for (size_t i = 0; i < unique.size(); ++i) {
    if (!reason[i].empty()) continue;
    const FoundMod& mod = *find(unique[i]);
    for (size_t j = 0; j < i && reason[i].empty(); ++j) {
      if (!reason[j].empty()) continue;
      const FoundMod& earlier = *find(unique[j]);
      const auto& a = mod.manifest.conflicts;
      const auto& b = earlier.manifest.conflicts;
      if (std::find(a.begin(), a.end(), earlier.manifest.id) != a.end() ||
          std::find(b.begin(), b.end(), mod.manifest.id) != b.end()) {
        reason[i] = "can't run with " + earlier.manifest.id;
      }
    }
  }
  for (size_t i = 0; i < unique.size(); ++i) {
    const FoundMod* mod = find(unique[i]);
    std::string line = "mod " + unique[i];
    if (mod && !mod->manifest.version.empty()) line += " " + mod->manifest.version;
    if (reason[i].empty()) {
      plan.overlay_roots.push_back(mod->folder / "files");
      line += ": on";
    } else {
      line += ": left out, " + reason[i];
    }
    plan.lines.push_back(std::move(line));
  }
  return plan;
}

}  // namespace skylanders::portal
