// run_logic: the plain rules of a run (Core of Light #71 / #52) without the menu or ReXGlue.

#include <string>
#include <vector>

#include "portal/run_logic.h"
#include "test_util.h"

using namespace skylanders::portal::run;

int main() {
  // Names compare by their letters and digits only.
  CHECK(NameKey("Jet-Vac") == "jetvac");
  CHECK(NameKey("Ka-Boom") == "kaboom");
  CHECK(NameKey("Dr. Krankcase") == "drkrankcase");
  CHECK(NameKey("") == "");

  // The hubs, as dev/where.h names them in the six games, and an unknown level (empty) count as hubs.
  CHECK(IsHubLevel("Level_Hub"));
  CHECK(IsHubLevel("Level_Hub_2_Stage_4"));
  CHECK(IsHubLevel("Level_Hub_2014_01"));
  CHECK(IsHubLevel("town_state5"));
  CHECK(IsHubLevel("Academy"));
  CHECK(IsHubLevel("Hub_2016"));
  CHECK(IsHubLevel("Overland_Map"));
  CHECK(IsHubLevel(""));
  CHECK(!IsHubLevel("Level_027"));
  CHECK(!IsHubLevel("Level_210_TheVillage"));
  CHECK(!IsHubLevel("ww1"));
  CHECK(!IsHubLevel("Nat1"));
  CHECK(!IsHubLevel("Level_500"));

  // Each game's starter pack, by name in any spelling.
  CHECK(IsStarter("Spyro's Adventure", "Spyro"));
  CHECK(IsStarter("Spyro's Adventure", "Gill Grunt"));
  CHECK(!IsStarter("Spyro's Adventure", "Bash"));
  CHECK(IsStarter("Giants", "jetvac"));
  CHECK(IsStarter("Swap Force", "Stealth Elf"));
  CHECK(IsStarter("Imaginators", "King Pen"));
  CHECK(!IsStarter("Imaginators", "Spyro"));
  CHECK(!IsStarter("Unknown game", "Spyro"));
  CHECK(!IsStarter("Trap Team", ""));
  CHECK(StartersOf("Spyro's Adventure").size() == 3);
  CHECK(StartersOf("Trap Team").size() == 2);
  CHECK(StartersOf("").size() == 15);
  CHECK(StartersOf("Unknown game").empty());
  // No game known: every game's starters (a run never starts with none).
  CHECK(IsStarter("", "Spyro"));
  CHECK(IsStarter("", "King Pen"));
  CHECK(!IsStarter("", "Bash"));

  // A ';' list of names: empty parts dropped, lookups by name in any spelling.
  CHECK(SplitList("Chop Chop;;Bash;").size() == 2);
  CHECK(ListHas("Chop Chop;Bash", "chopchop"));
  CHECK(!ListHas("Chop Chop;Bash", "Spyro"));
  CHECK(!ListHas("", "Spyro"));
  CHECK(ListAdd("", "Chop Chop") == "Chop Chop");
  CHECK(ListAdd("Chop Chop", "Bash") == "Chop Chop;Bash");

  // A level's first finish brings a Skylander, a hub or a level finished before doesn't.
  CHECK(FirstFinish("Spyro's Adventure", "Level_027", ""));
  CHECK(FirstFinish("Spyro's Adventure", "Level_028", "Spyro's Adventure/Level_027"));
  CHECK(!FirstFinish("Spyro's Adventure", "Level_027", "Spyro's Adventure/Level_026;Spyro's Adventure/Level_027"));
  CHECK(FirstFinish("Giants", "Level_027", "Spyro's Adventure/Level_027"));  // another game's level
  CHECK(!FirstFinish("Spyro's Adventure", "Level_Hub", ""));
  CHECK(!FirstFinish("Swap Force", "town_state5", ""));
  CHECK(!FirstFinish("Spyro's Adventure", "", ""));

  // Spread over the story: the ones left over the levels left, rounded up; no level left: all.
  CHECK(RewardCount(31, 16) == 2);
  CHECK(RewardCount(5, 25) == 1);
  CHECK(RewardCount(0, 10) == 0);
  CHECK(RewardCount(3, 0) == 3);
  CHECK(RewardCount(80, 20) == 4);
  CHECK(RewardCount(81, 20) == 5);

  // The run's state as text, one for all six games.
  {
    const RunState s = ParseRunState("open=Chop Chop;Bash\r\ndone=Spyro's Adventure/Level_027\nother=1\n");
    CHECK(s.open == "Chop Chop;Bash");
    CHECK(s.done == "Spyro's Adventure/Level_027");
    CHECK(FormatRunState(s) == "open=Chop Chop;Bash\ndone=Spyro's Adventure/Level_027\n");
    CHECK(ParseRunState("").open.empty() && ParseRunState("").done.empty());
    CHECK(ParseRunState("open=Bash").open == "Bash");  // no line end at the end
    CHECK(LevelKey("Giants", "Level_200") == "Giants/Level_200");
  }

  // Soul Gems by game, as on the discs, and what a first finish can bring.
  CHECK(HasSoulGem("Spyro's Adventure", "Chop Chop"));
  CHECK(!HasSoulGem("Spyro's Adventure", "Bash"));
  CHECK(HasSoulGem("Giants", "Jet-Vac"));
  CHECK(!HasSoulGem("Giants", "Chop Chop"));  // Spyro's Adventure's, no gem in Giants
  CHECK(HasSoulGem("Trap Team", "Snap Shot"));
  CHECK(!HasSoulGem("Unknown game", "Bash"));
  {
    auto list = [](const std::vector<std::string>& v) {
      std::string out;
      for (const std::string& n : v) out = ListAdd(out, n);
      return out;
    };
    const std::vector<std::string> ssa = RewardCandidates("Spyro's Adventure");
    CHECK(ListHas(list(ssa), "Bash") && ListHas(list(ssa), "Zap") && ListHas(list(ssa), "Drobot") &&
          ListHas(list(ssa), "Boomer"));
    CHECK(!ListHas(list(ssa), "Chop Chop"));  // has a gem
    CHECK(!ListHas(list(ssa), "Spyro"));      // a starter
    CHECK(!ListHas(list(ssa), "Jet-Vac"));    // a later game's
    const std::vector<std::string> giants = RewardCandidates("Giants");
    CHECK(ListHas(list(giants), "Chop Chop") && ListHas(list(giants), "Bash") && ListHas(list(giants), "Spyro"));
    CHECK(!ListHas(list(giants), "Cynder"));   // Giants' starter
    CHECK(!ListHas(list(giants), "Jet-Vac"));  // its gem is in Giants
    CHECK(!ListHas(list(giants), "Wash Buckler"));
    const std::vector<std::string> tt = RewardCandidates("Trap Team");
    CHECK(ListHas(list(tt), "Jet-Vac") && ListHas(list(tt), "Wash Buckler") && !ListHas(list(tt), "Food Fight"));
    // The counts (2026-11-13, checked name by name): Spyro's Adventure's four without a gem; Giants
    // the 32 of Spyro's Adventure less Cynder (a starter); Swap Force those 48 less Stealth Elf; Trap
    // Team 80; SuperChargers with Trap Team's 36; Imaginators with SuperChargers' 10. No sidekicks,
    // Minis (Weeruptor is no Eruptor), vehicles or the Nintendo-only Bowser and Donkey Kong.
    CHECK(ssa.size() == 4);
    CHECK(giants.size() == 31);
    CHECK(RewardCandidates("Swap Force").size() == 47);
    CHECK(tt.size() == 80);
    CHECK(RewardCandidates("SuperChargers").size() == 116);
    CHECK(RewardCandidates("Imaginators").size() == 126);
    CHECK(!ListHas(list(RewardCandidates("Imaginators")), "Tomb Buggy"));
    CHECK(!ListHas(list(RewardCandidates("SuperChargers")), "Barkley"));
    CHECK(ListHas(list(tt), "Eruptor"));
  }

  // The reward: one of the candidates the run doesn't have yet, by the number drawn; none left: "".
  {
    const std::vector<std::string> gemless = {"Bash", "Chop Chop", "Eruptor"};
    CHECK(PickReward(gemless, "", 0) == "Bash");
    CHECK(PickReward(gemless, "", 1) == "Chop Chop");
    CHECK(PickReward(gemless, "", 5) == "Eruptor");               // 5 % 3
    CHECK(PickReward(gemless, "chopchop", 1) == "Eruptor");       // Chop Chop is open already
    CHECK(PickReward(gemless, "Bash;Chop Chop;Eruptor", 7).empty());
    CHECK(PickReward({}, "", 3).empty());
    CHECK(PickReward({"Jet-Vac", "jetvac", "Pop Fizz"}, "", 1) == "Pop Fizz");  // one name, two spellings
    // Every candidate comes once, whatever is drawn: three finishes open all three.
    std::string open;
    for (const uint32_t r : {2u, 2u, 2u}) open = ListAdd(open, PickReward(gemless, open, r));
    CHECK(ListHas(open, "Bash") && ListHas(open, "Chop Chop") && ListHas(open, "Eruptor"));
    CHECK(SplitList(open).size() == 3);
  }

  // Teams per level and player.
  Teams teams;
  teams.Follow("Level_027");
  CHECK(!teams.Refuses(0, "bash", 1));  // an empty team takes anyone
  CHECK(teams.Add(0, "bash"));
  CHECK(!teams.Add(0, "bash"));         // once only
  CHECK(teams.Count(0) == 1);
  CHECK(teams.Refuses(0, "spyro", 1));  // full at 1
  CHECK(!teams.Refuses(0, "bash", 1));  // its own member again is fine
  CHECK(!teams.Refuses(0, "spyro", 2)); // room at 2
  CHECK(!teams.Refuses(1, "spyro", 1)); // another player's team is apart
  CHECK(!teams.Refuses(0, "spyro", 0)); // 0: no limit
  CHECK(!teams.Refuses(7, "spyro", 1)); // a player number out of range never refuses
  teams.Follow("Level_027");            // the same level: the team stays
  CHECK(teams.Count(0) == 1);
  teams.Follow("Level_028");            // a new level: every team starts empty
  CHECK(teams.Count(0) == 0);
  CHECK(!teams.Refuses(0, "spyro", 1));
  teams.Follow("Level_Hub");            // the hub: nothing is refused or counted
  CHECK(!teams.Add(0, "bash"));
  CHECK(!teams.Refuses(0, "spyro", 1));

  // Format then parse keeps the open and done lists.
  {
    const RunState before = {"Chop Chop;Bash", "Spyro's Adventure/Level_027;Giants/Level_200"};
    const RunState after = ParseRunState(FormatRunState(before));
    CHECK(after.open == before.open);
    CHECK(after.done == before.done);
  }

  // Windows line ends are read like Unix ones.
  {
    const RunState s = ParseRunState("open=Bash\r\ndone=Giants/Level_200\r\n");
    CHECK(s.open == "Bash");
    CHECK(s.done == "Giants/Level_200");
  }

  // A done line before an open line is read fine.
  {
    const RunState s = ParseRunState("done=Giants/Level_200\nopen=Bash\n");
    CHECK(s.open == "Bash");
    CHECK(s.done == "Giants/Level_200");
  }

  // Unknown lines are ignored.
  {
    const RunState s = ParseRunState("version=2\nopen=Bash\nnoise\n");
    CHECK(s.open == "Bash");
    CHECK(s.done.empty());
  }

  // An empty open= line gives an empty list.
  {
    const RunState s = ParseRunState("open=\ndone=\n");
    CHECK(s.open.empty());
    CHECK(s.done.empty());
  }

  // Nobody left stays none, whatever the levels left.
  {
    CHECK(RewardCount(0, 0) == 0);
    CHECK(RewardCount(0, 7) == 0);
  }

  // Fewer left than levels left still frees one (rounded up).
  {
    CHECK(RewardCount(2, 5) == 1);
    CHECK(RewardCount(1, 100) == 1);
  }

  // Left equal to the levels left frees one per level.
  {
    CHECK(RewardCount(5, 5) == 1);
    CHECK(RewardCount(16, 16) == 1);
  }

  // A large left over few levels rounds up.
  {
    CHECK(RewardCount(1000, 3) == 334);
  }

  // A game name with an apostrophe keys its levels as expected.
  {
    const std::string key = LevelKey("Spyro's Adventure", "Level_027");
    CHECK(key == "Spyro's Adventure/Level_027");
    CHECK(!FirstFinish("Spyro's Adventure", "Level_027", key));
    CHECK(FirstFinish("Spyro's Adventure", "Level_028", key));
  }

  // The same level name in two games counts separately.
  {
    const std::string done = LevelKey("Spyro's Adventure", "Level_027");
    CHECK(FirstFinish("Giants", "Level_027", done));
    CHECK(LevelKey("Giants", "Level_027") != done);
  }

  // The same number skips the names already open and picks a later one.
  {
    const std::vector<std::string> names = {"Bash", "Chop Chop", "Eruptor"};
    CHECK(PickReward(names, "", 1) == "Chop Chop");
    CHECK(PickReward(names, "Bash", 1) == "Eruptor");
    CHECK(PickReward(names, "Bash;Chop Chop", 1) == "Eruptor");
  }

  // An empty candidate list gives an empty name.
  {
    CHECK(PickReward({}, "", 0).empty());
  }

  return Finish("run_logic");
}
