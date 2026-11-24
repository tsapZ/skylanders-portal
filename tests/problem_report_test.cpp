#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "portal_rex/problem_report.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace skylanders;

namespace {

void WriteFile(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), std::streamsize(text.size()));
}

void Age(const fs::path& path, int minutes) {
  fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::minutes(minutes));
}

std::vector<std::string> PartNames(const std::string& report) {
  std::vector<std::string> names;
  const std::string prefix = "===== ";
  size_t pos = 0;
  while ((pos = report.find(prefix, pos)) != std::string::npos) {
    const size_t begin = pos + prefix.size();
    const size_t end = report.find(" =====", begin);
    if (end == std::string::npos) break;
    names.push_back(report.substr(begin, end - begin));
    pos = end + 6;
  }
  return names;
}

std::string PartText(const std::string& report, const std::string& name) {
  const std::string header = "===== " + name + " =====\n";
  const size_t pos = report.find(header);
  if (pos == std::string::npos) return {};
  const size_t begin = pos + header.size();
  const size_t end = report.find("===== ", begin);
  return report.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / L"gr_problem_report";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / L"work" / L"logs", ec);
  fs::create_directories(root / L"exe" / L"logs", ec);
  const fs::path work = root / L"work";
  const fs::path exe = root / L"exe";

  // The game's settings file, with the private folder in it.
  WriteFile(exe / L"testgame.toml",
            "path = \"C:\\Users\\TestPlayer\\Documents\"\nsettings = \"on\"\n");

  // Five logs across the two logs folders, one longer than the 512 KB kept.
  const size_t big_size = 600 * 1024;
  std::string big(big_size, 'x');
  const std::string old_marker = "OLD-START";
  const std::string end_marker = "NEW-END";
  big.replace(0, old_marker.size(), old_marker);
  big.replace(big.size() - end_marker.size() - 1, end_marker.size(), end_marker);
  big.back() = '\n';
  WriteFile(work / L"logs" / L"big.log", big);
  WriteFile(work / L"logs" / L"work_1.log", "work one C:/Users/TESTPLAYER/save\n");
  WriteFile(work / L"logs" / L"work_2.log", "work two\n");
  WriteFile(exe / L"logs" / L"exe_1.log", "exe one\n");
  WriteFile(exe / L"logs" / L"exe_2.log", "exe two\n");
  Age(work / L"logs" / L"big.log", 0);
  Age(exe / L"logs" / L"exe_1.log", 5);
  Age(work / L"logs" / L"work_1.log", 15);
  Age(exe / L"logs" / L"exe_2.log", 25);
  Age(work / L"logs" / L"work_2.log", 35);

  // Four notes, so the oldest is left out too.
  WriteFile(work / L"logs" / L"problem-work-new.txt", "NOTE work new\n");
  WriteFile(work / L"logs" / L"problem-work-old.txt", "NOTE work old\n");
  WriteFile(exe / L"logs" / L"problem-exe-new.txt", "NOTE exe new\n");
  WriteFile(exe / L"logs" / L"problem-exe-old.txt", "NOTE exe old\n");
  Age(work / L"logs" / L"problem-work-new.txt", 0);
  Age(exe / L"logs" / L"problem-exe-new.txt", 10);
  Age(work / L"logs" / L"problem-work-old.txt", 30);
  Age(exe / L"logs" / L"problem-exe-old.txt", 40);

  const std::vector<std::pair<std::string, std::string>> words = {
      {"C:\\Users\\TestPlayer", "%USERPROFILE%"},
      {"C:/Users/TestPlayer", "%USERPROFILE%"},
  };
  const std::string report = BuildGameReport(work, exe, "testgame", words);

  const std::vector<std::string> names = PartNames(report);
  const std::vector<std::string> expected = {
      "info",
      "testgame/testgame.toml",
      "testgame/logs/big.log",
      "testgame/logs/exe_1.log",
      "testgame/logs/work_1.log",
      "testgame/logs/exe_2.log",
      "testgame/notes/problem-work-new.txt",
      "testgame/notes/problem-exe-new.txt",
      "testgame/notes/problem-work-old.txt",
  };
  CHECK(names == expected);

  CHECK(PartText(report, "info") ==
        "Game: testgame\nMade by the game's Report a problem (without Core of Light)\n");

  const std::string settings = PartText(report, "testgame/testgame.toml");
  CHECK(settings.find("settings = \"on\"") != std::string::npos);
  CHECK(settings.find("%USERPROFILE%") != std::string::npos);
  CHECK(settings.find("TestPlayer") == std::string::npos);

  const std::string big_text = PartText(report, "testgame/logs/big.log");
  CHECK(big_text.size() == 512 * 1024);
  CHECK(big_text.find(old_marker) == std::string::npos);
  CHECK(big_text.find(end_marker) != std::string::npos);

  const std::string log_text = PartText(report, "testgame/logs/work_1.log");
  CHECK(log_text.find("%USERPROFILE%") != std::string::npos);
  CHECK(log_text.find("TESTPLAYER") == std::string::npos);

  CHECK(PartText(report, "testgame/notes/problem-work-new.txt").find("NOTE work new") !=
        std::string::npos);
  CHECK(PartText(report, "testgame/notes/problem-exe-new.txt").find("NOTE exe new") !=
        std::string::npos);
  CHECK(PartText(report, "testgame/notes/problem-work-old.txt").find("NOTE work old") !=
        std::string::npos);

  // Linux: the home folder, user and host name, the longest match first (CoL #135).
  {
    const auto words = PosixPrivateWords("/home/tuxplayer/", "tuxplayer", "tuxbox.lan");
    const fs::path linux_exe = root / L"linux_exe";
    fs::create_directories(linux_exe / L"logs", ec);
    WriteFile(linux_exe / L"testgame.toml",
              "dir = \"/home/tuxplayer/Documents/Skylanders\"\nuser = tuxplayer\nhost = tuxbox\n"
              "fqdn = tuxbox.lan\n");
    const std::string linux_report = BuildGameReport(linux_exe, linux_exe, "testgame", words);
    const std::string linux_settings = PartText(linux_report, "testgame/testgame.toml");
    CHECK(linux_settings.find("\"$HOME/Documents/Skylanders\"") != std::string::npos);
    CHECK(linux_settings.find("user = $USER") != std::string::npos);
    CHECK(linux_settings.find("host = $HOSTNAME\n") != std::string::npos);
    CHECK(linux_settings.find("fqdn = $HOSTNAME\n") != std::string::npos);
    CHECK(linux_settings.find("tux") == std::string::npos);
  }
  // A home of "/" and names under 3 characters stay, as on Windows.
  CHECK(PosixPrivateWords("/", "ab", "pc").empty());
  CHECK(PosixPrivateWords("", "", "").empty());
  CHECK(PosixPrivateWords("/home/deck", "deck", "steamdeck").size() == 3);

  fs::remove_all(root, ec);
  return Finish("problem_report");
}
