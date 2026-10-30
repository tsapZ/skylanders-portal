#include "portal_rex/discord_presence.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "dev/where.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_info.h"
#include "portal/software/software_portal.h"
#include "portal/trap_villain.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_rex.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(discord_presence, false, "General",
                    "Show the game, the Skylander and the level as your Discord status while the "
                    "Discord app runs on this PC (your Discord friends see it)");

namespace skylanders {
namespace {

using Clock = std::chrono::steady_clock;

// The owner's Discord applications, one per game (their names are what Discord shows as the game).
struct DiscordApp {
  std::string_view game;
  const char* client_id;
};
constexpr DiscordApp kApps[] = {
    {"Spyro's Adventure", "1565863249361113129"}, {"Giants", "1565863323222937681"},
    {"Swap Force", "1565863366130671806"},        {"Trap Team", "1565863404860870807"},
    {"SuperChargers", "1565863449848840312"},     {"Imaginators", "1565863523152822362"},
};

// Discord allows 5 status changes in 20 seconds; a change comes at most this often, the last one
// of a quicker series a little later.
constexpr auto kMinGap = std::chrono::seconds(5);
// While Discord isn't running (or refused), how often to look again.
constexpr auto kRetry = std::chrono::seconds(15);
constexpr auto kRetryAfterRefusal = std::chrono::minutes(2);

std::mutex g_names_mutex;
DiscordLevelNameProvider g_level_names;

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

// The name to show for a catalog entry: "Legendary Chop Chop", without "(Series 3)" and the like.
std::string ShownName(const portal::SkylanderInfo& sky) {
  std::string name = portal::DisplayName(sky.name);
  if (const size_t paren = name.find(" ("); paren != std::string::npos) name.resize(paren);
  return name;
}

bool IsVehicle(const portal::SkylanderInfo& sky) {
  return sky.category.find("Vehicle") != std::string_view::npos;
}

// Who is on the portal: the players' Skylanders (a swapper's two halves as "Freeze Blade" or
// "Freeze Blade / Night Shift") and what goes with them, a SuperChargers vehicle or the villain in
// a Trap Team trap.
struct Lineup {
  std::vector<std::string> characters;
  std::string companion;
};

Lineup PortalLineup() {
  Lineup lineup;
  const portal::SkylanderInfo* top = nullptr;
  const portal::SkylanderInfo* bottom = nullptr;
  auto add = [&](const portal::FigureData& data) {
    if (auto trap = portal::ReadTrapVillain(data); trap && trap->villain_id != 0) {
      if (const portal::TrapVillain* villain = portal::FindTrapVillain(trap->villain_id, trap->variant)) {
        if (lineup.companion.empty()) lineup.companion = portal::DisplayName(villain->name);
      }
      return;
    }
    const portal::SkylanderInfo* sky =
        portal::FindSkylander(portal::ReadFigureId(data), portal::ReadFigureVariant(data));
    if (!sky) return;
    if (IsVehicle(*sky)) {
      if (lineup.companion.empty()) lineup.companion = ShownName(*sky);
    } else if (portal::GetSwapperHalf(*sky) == portal::SwapperHalf::kTop) {
      top = sky;
    } else if (portal::GetSwapperHalf(*sky) == portal::SwapperHalf::kBottom) {
      bottom = sky;
    } else if (portal::FigureElement(*sky) != portal::Element::kNone) {
      lineup.characters.push_back(ShownName(*sky));
    }
  };
  if (portal::SoftwarePortal* software = GetSoftwarePortal()) {
    for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
      if (auto data = software->Figure(slot)) add(*data);
    }
  } else if (GetUsbPortal()) {
    for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
      if (auto data = ReadRealFigureBlocks(slot)) add(*data);
    }
  }
  if (top || bottom) {
    std::string a(portal::SwapperBaseName(top ? *top : *bottom));
    std::string b(portal::SwapperBaseName(bottom ? *bottom : *top));
    lineup.characters.insert(lineup.characters.begin(), a == b ? a : a + " / " + b);
  }
  if (lineup.characters.size() > 2) lineup.characters.resize(2);  // two players at most
  return lineup;
}

// "Playing as Chop Chop", with one player's companion "... Chop Chop with Buzz Wing" ("Spielt mit
// Chop Chop und Buzz Wing"), two players "... Chop Chop & Spyro". Empty with no Skylander.
std::string DetailsText(const Lineup& lineup, bool german) {
  if (lineup.characters.empty()) return {};
  std::string text = (german ? "Spielt mit " : "Playing as ") + lineup.characters[0];
  if (lineup.characters.size() > 1) {
    text += " & " + lineup.characters[1];
  } else if (!lineup.companion.empty()) {
    text += (german ? " und " : " with ") + lineup.companion;
  }
  return text;
}

std::string LevelText() {
  const std::string level = dev::CurrentLevel();
  if (level.empty()) return {};
  std::lock_guard lock(g_names_mutex);
  return g_level_names ? g_level_names(level) : std::string();
}

// The activity as Discord's JSON; `start` is the play time's start (Unix seconds), `party` this
// game's party id. Two players are a party of two, which Discord shows after the level as
// "(2 of 2)" ("Co-op (2 of 2)" without a level name).
std::string Activity(int64_t start, const std::string& party) {
  const bool german = menu_draw::GermanText();
  const Lineup lineup = PortalLineup();
  const std::string details = DetailsText(lineup, german);
  const bool coop = lineup.characters.size() == 2;
  std::string level = LevelText();
  if (level.size() < 2 && coop) level = german ? "Koop" : "Co-op";
  std::string json = "{";
  if (!details.empty()) json += "\"details\":" + JsonString(details) + ",";
  if (level.size() >= 2) json += "\"state\":" + JsonString(level) + ",";
  if (coop) json += "\"party\":{\"id\":" + JsonString(party) + ",\"size\":[2,2]},";
  json += "\"timestamps\":{\"start\":" + std::to_string(start) + "},";
  json += "\"assets\":{\"large_image\":\"logo\",\"large_text\":" +
          JsonString(german ? "Inoffizieller PC-Port (Recomp)" : "Unofficial PC port (recomp)") +
          "}}";
  return json;
}

#ifdef _WIN32

// One connection to the Discord app: frames of an opcode, a length and JSON.
class DiscordPipe {
 public:
  enum Op : uint32_t { kHandshake = 0, kFrame = 1, kClose = 2, kPing = 3, kPong = 4 };

  ~DiscordPipe() { Close(); }

  bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

  // `refused`: Discord answered, but said no (an unknown application, for instance).
  bool Connect(const char* client_id, bool* refused) {
    *refused = false;
    for (int i = 0; i < 10 && !connected(); ++i) {
      const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
      pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (!connected()) return false;
    if (!Write(kHandshake, std::string("{\"v\":1,\"client_id\":\"") + client_id + "\"}")) {
      Close();
      return false;
    }
    // The answer: READY, or a close with the reason.
    for (int i = 0; i < 50; ++i) {
      uint32_t op = 0;
      std::string payload;
      const int got = Read(&op, &payload);
      if (got < 0) break;
      if (got == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      if (op == kFrame && payload.find("\"READY\"") != std::string::npos) return true;
      if (op == kClose) {
        REXLOG_WARN("Discord status: Discord refused the connection ({})", Field(payload, "message"));
        *refused = true;
        break;
      }
    }
    Close();
    return false;
  }

  void Close() {
    if (connected()) CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }

  bool Write(uint32_t op, const std::string& json) {
    std::string frame(8 + json.size(), '\0');
    const uint32_t size = static_cast<uint32_t>(json.size());
    std::memcpy(frame.data(), &op, 4);
    std::memcpy(frame.data() + 4, &size, 4);
    std::memcpy(frame.data() + 8, json.data(), json.size());
    DWORD written = 0;
    if (!WriteFile(pipe_, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) ||
        written != frame.size()) {
      Close();
      return false;
    }
    return true;
  }

  // 1: a frame read; 0: none waiting; -1: the connection is gone.
  int Read(uint32_t* op, std::string* payload) {
    DWORD waiting = 0;
    if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &waiting, nullptr)) {
      Close();
      return -1;
    }
    if (waiting < 8) return 0;
    uint32_t header[2];
    if (!ReadAll(header, sizeof(header)) || header[1] > 1 << 20) {
      Close();
      return -1;
    }
    *op = header[0];
    payload->resize(header[1]);
    if (header[1] && !ReadAll(payload->data(), header[1])) {
      Close();
      return -1;
    }
    return 1;
  }

  // Answers Discord's pings and notes its errors; false once the connection is gone.
  bool Drain() {
    for (;;) {
      uint32_t op = 0;
      std::string payload;
      const int got = Read(&op, &payload);
      if (got < 0) return false;
      if (got == 0) return true;
      if (op == kPing) {
        if (!Write(kPong, payload)) return false;
      } else if (op == kClose) {
        Close();
        return false;
      } else if (op == kFrame && payload.find("\"evt\":\"ERROR\"") != std::string::npos) {
        REXLOG_WARN("Discord status: Discord said {}", Field(payload, "message"));
      }
    }
  }

  // A string field's value from Discord's JSON, for the log (only errors' messages are logged:
  // the other answers carry the player's Discord account).
  static std::string Field(const std::string& json, const char* name) {
    const std::string key = std::string("\"") + name + "\":\"";
    const size_t at = json.find(key);
    if (at == std::string::npos) return "no reason given";
    const size_t end = json.find('"', at + key.size());
    return json.substr(at + key.size(), end == std::string::npos ? std::string::npos : end - at - key.size());
  }

 private:
  bool ReadAll(void* to, DWORD size) {
    auto* p = static_cast<char*>(to);
    while (size) {
      DWORD got = 0;
      if (!ReadFile(pipe_, p, size, &got, nullptr) || got == 0) return false;
      p += got;
      size -= got;
    }
    return true;
  }

  HANDLE pipe_ = INVALID_HANDLE_VALUE;
};

void Run(const char* client_id) {
  const int64_t start = static_cast<int64_t>(std::time(nullptr));
  const DWORD pid = GetCurrentProcessId();
  const std::string party = "skylanders-" + std::to_string(pid) + "-" + std::to_string(start);
  DiscordPipe pipe;
  Clock::time_point next_try{}, last_send{};
  std::string sent;  // the activity Discord has
  int nonce = 0;
  bool said_missing = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const Clock::time_point now = Clock::now();
    if (!REXCVAR_GET(discord_presence)) {
      if (pipe.connected()) {
        // Switched off: the status goes away with the connection.
        pipe.Close();
        sent.clear();
        REXLOG_INFO("Discord status: off");
      }
      continue;
    }
    if (!pipe.connected()) {
      if (now < next_try) continue;
      bool refused = false;
      if (!pipe.Connect(client_id, &refused)) {
        next_try = now + (refused ? kRetryAfterRefusal : kRetry);
        if (!refused && !said_missing) REXLOG_INFO("Discord status: Discord isn't running; looking again now and then");
        said_missing = true;
        continue;
      }
      REXLOG_INFO("Discord status: connected");
      said_missing = false;
      sent.clear();
    }
    if (!pipe.Drain()) {
      sent.clear();
      next_try = now + kRetry;
      continue;
    }
    const std::string activity = Activity(start, party);
    if (activity == sent || now - last_send < kMinGap) continue;
    const std::string command = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
                                ",\"activity\":" + activity + "},\"nonce\":\"" +
                                std::to_string(++nonce) + "\"}";
    if (!pipe.Write(DiscordPipe::kFrame, command)) {
      sent.clear();
      next_try = now + kRetry;
      continue;
    }
    if (sent.empty()) REXLOG_INFO("Discord status: shown");
    sent = activity;
    last_send = now;
  }
}

#endif  // _WIN32

}  // namespace

void StartDiscordPresence(std::string_view game) {
#ifdef _WIN32
  for (const DiscordApp& app : kApps) {
    if (app.game != game) continue;
    std::thread([id = app.client_id] { Run(id); }).detach();
    return;
  }
#else
  (void)game;
#endif
}

void SetDiscordLevelNames(DiscordLevelNameProvider provider) {
  std::lock_guard lock(g_names_mutex);
  g_level_names = std::move(provider);
}

Setting DiscordPresenceSetting() {
  return {"discord_presence", "Discord status", "Discord-Status",
          "Shows on your Discord profile which Skylanders game you play, with which Skylander and "
          "where, while the Discord app runs on this PC.\n"
          "Your Discord friends see it, so it stays off until you switch it on.",
          "Zeigt in deinem Discord-Profil, welches Skylanders-Spiel du spielst, mit welchem "
          "Skylander und wo, solange die Discord-App auf diesem PC l\xC3\xA4uft.\n"
          "Deine Discord-Freunde sehen das, darum bleibt es aus, bis du es einschaltest."};
}

}  // namespace skylanders
