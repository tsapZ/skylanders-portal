#include "portal_rex/settings_schema.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <filesystem>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

REXCVAR_DEFINE_STRING(settings_schema, "", "Launcher",
                      "Write the settings menu's pages to this file (JSON) and quit; for the "
                      "Core of Light launcher");
REXCVAR_DEFINE_BOOL(hd_textures, true, "Graphics",
                    "Use the HD texture pack in Documents\\Skylanders\\Textures\\<game> when there "
                    "is one (false: the game's own textures); read at start");

namespace skylanders {
namespace {

std::string Json(std::string_view s) {
  std::string out = "\"";
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
          out += escaped;
        } else {
          out += c;  // UTF-8 goes through as it is
        }
    }
  }
  return out + "\"";
}

const char* TypeName(rex::cvar::FlagType type) {
  switch (type) {
    case rex::cvar::FlagType::Boolean: return "bool";
    case rex::cvar::FlagType::Int32:
    case rex::cvar::FlagType::Int64:
    case rex::cvar::FlagType::Uint32:
    case rex::cvar::FlagType::Uint64: return "int";
    case rex::cvar::FlagType::Double: return "float";
    default: return "string";
  }
}

std::string Options(const std::vector<SettingOption>& options) {
  std::string out = "[";
  for (size_t i = 0; i < options.size(); ++i) {
    if (i) out += ", ";
    out += "{\"value\": " + Json(options[i].value) + ", \"en\": " + Json(options[i].en) +
           ", \"de\": " + Json(options[i].de) + "}";
  }
  return out + "]";
}

// One setting as JSON, or empty for a row without one.
std::string SettingJson(const Setting& s) {
  std::string out = "{";
  const auto texts = [&] {
    return "\"en\": " + Json(s.en) + ", \"de\": " + Json(s.de) + ", \"help_en\": " + Json(s.help_en) +
           ", \"help_de\": " + Json(s.help_de);
  };
  if (s.preset) {
    // A preset: its options and the settings each one stands for.
    out += "\"preset\": true, " + texts() + ", \"options\": " + Options(s.options) + ", \"presets\": {";
    for (size_t i = 0; i < s.options.size(); ++i) {
      if (i) out += ", ";
      out += Json(s.options[i].value) + ": [";
      const auto values = s.preset(s.options[i].value);
      for (size_t k = 0; k < values.size(); ++k) {
        const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(values[k].first);
        out += std::string(k ? ", " : "") + "[" + Json(values[k].first) + ", " + Json(values[k].second) +
               ", " + Json(info ? info->category : "") + ", " +
               Json(info ? TypeName(info->type) : "string") + "]";
      }
      out += "]";
    }
    out += "}, \"current\": " + Json(s.current ? s.current() : "") + ", \"restart\": " +
           (s.restart ? "true" : "false") + "}";
    return out;
  }
  if (s.cvar.empty()) return {};
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(s.cvar);
  if (!info) return {};  // a setting of a part this build doesn't have
  // Copied out: the pointer is only valid until the registry changes.
  const std::string category = info->category, default_value = info->default_value;
  const char* type = TypeName(info->type);
  const bool restart = s.restart || info->lifecycle != rex::cvar::Lifecycle::kHotReload;
  out += "\"cvar\": " + Json(s.cvar) + ", " + texts() + ", \"type\": " + Json(type) +
         ", \"category\": " + Json(category) + ", \"default\": " + Json(default_value) +
         ", \"value\": " + Json(rex::cvar::GetFlagByName(s.cvar)) + ", \"restart\": " +
         (restart ? "true" : "false") + ", \"buttons\": " + (s.buttons ? "true" : "false") +
         ", \"enabled_by\": " + Json(s.enabled_by) + ", \"options\": " + Options(s.options) + "}";
  return out;
}

}  // namespace

bool SettingsSchemaRequested() { return !REXCVAR_GET(settings_schema).empty(); }

void ApplyGraphicsDefaults() {
  if (!rex::cvar::HasNonDefaultValue("anisotropic_override") &&
      !rex::cvar::SetFlagByName("anisotropic_override", "5")) {
    REXLOG_WARN("Could not set anisotropic_override to 16x");
  }
}

void UseTexturePack(const std::filesystem::path& user_data_root, std::string_view game) {
  if (rex::cvar::HasNonDefaultValue("texture_replace_dir")) return;
  // Where SharedPicturesDir (portal_menu.h) puts the pictures, with Textures for Pictures.
  const std::filesystem::path base =
      user_data_root.parent_path().empty() ? user_data_root : user_data_root.parent_path() / "Skylanders";
  const std::filesystem::path dir =
      base / "Textures" / std::filesystem::path(std::u8string(game.begin(), game.end()));
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return;
  const std::u8string u8 = dir.u8string();
  if (!REXCVAR_GET(hd_textures)) {
    REXLOG_INFO("HD textures off (hd_textures = false): the game's own, not {}", std::string(u8.begin(), u8.end()));
    return;
  }
  if (rex::cvar::SetFlagByName("texture_replace_dir", std::string(u8.begin(), u8.end()))) {
    REXLOG_INFO("HD textures from {}", std::string(u8.begin(), u8.end()));
  }
}

Setting HdTexturesSetting() {
  Setting s{"hd_textures", "HD textures", "HD-Texturen",
            "With an HD texture pack installed (Documents\\Skylanders\\Textures\\<game>): on, the "
            "game uses its sharper textures; off, the game's own, as on the console. Without a "
            "pack nothing changes. Takes effect when the game starts again.",
            "Mit einem installierten HD-Texturpaket (Dokumente\\Skylanders\\Textures\\<Spiel>): an, "
            "das Spiel nimmt die sch\xC3\xA4rferen Texturen; aus, die eigenen wie auf der Konsole. "
            "Ohne Paket \xC3\xA4ndert sich nichts. Wirkt beim n\xC3\xA4" "chsten Spielstart."};
  s.restart = true;
  return s;
}

void WriteSettingsSchemaIfRequested(rex::RuntimeConfig& config,
                                    const std::function<std::vector<SettingsPage>()>& pages,
                                    const std::filesystem::path& config_file) {
  const std::string target = REXCVAR_GET(settings_schema);
  if (target.empty()) return;
  if (!config.graphics && !config.gpu_plugin.empty()) {
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
  }
  ApplyGraphicsDefaults();  // as the game sets them once it runs
  const std::u8string file = config_file.u8string();
  std::string out = "{\"version\": 1, \"config_file\": " + Json(std::string(file.begin(), file.end())) +
                    ", \"pages\": [";
  bool first_page = true;
  for (const SettingsPage& page : pages()) {
    std::string settings;
    for (const Setting& s : page.settings) {
      const std::string json = SettingJson(s);
      if (json.empty()) continue;
      settings += (settings.empty() ? "" : ", ") + json;
    }
    if (settings.empty()) continue;
    out += std::string(first_page ? "" : ", ") + "{\"en\": " + Json(page.en) + ", \"de\": " + Json(page.de) +
           ", \"settings\": [" + settings + "]}";
    first_page = false;
  }
  out += "]}\n";
  const std::u8string u8(target.begin(), target.end());
  std::ofstream f(std::filesystem::path(u8), std::ios::binary | std::ios::trunc);
  f << out;
  f.close();
  REXLOG_INFO("Settings schema written to {} ({} bytes)", target, out.size());
  rex::FlushLogging();
  // Before any window or the runtime: nothing to shut down. Without static destructors, as the
  // input script's quit does (the loaded GPU plugin's would run after the host's).
  std::_Exit(f ? 0 : 1);
}

}  // namespace skylanders
