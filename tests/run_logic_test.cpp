// run_logic: the plain rules of a run (Core of Light #71 / #52) without the menu or ReXGlue.

#include <string>

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
  CHECK(FirstFinish("Level_027", ""));
  CHECK(FirstFinish("Level_028", "Level_027"));
  CHECK(!FirstFinish("Level_027", "Level_026;Level_027"));
  CHECK(!FirstFinish("Level_Hub", ""));
  CHECK(!FirstFinish("", ""));

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

  return Finish("run_logic");
}
