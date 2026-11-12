// soul_gem_names: the helpers the games use to name the Skylander of a Soul Gem movie they open.

#include <string>

#include "portal/soul_gem_names.h"
#include "test_util.h"

using namespace skylanders::portal;

int main() {
  // The file's name without its folders and an extension in any case; "" for another extension.
  CHECK(FileStem("game:\\movies\\Bash.mov", ".mov") == "Bash");
  CHECK(FileStem("game:\\movies\\chopchop.BIK", ".bik") == "chopchop");
  CHECK(FileStem("D:/archives/SG_KingPen.bk2", ".bk2") == "SG_KingPen");
  CHECK(FileStem("game:\\movies\\Bash.mov", ".bik") == "");
  CHECK(FileStem("game:\\movies\\.mov", ".mov") == "");
  CHECK(FileStem("", ".mov") == "");

  // The file's own folder.
  CHECK(InFolder("game:\\movies\\bash.bik", "movies"));
  CHECK(InFolder("game:\\Movies\\bash.bik", "movies"));
  CHECK(!InFolder("game:\\movies\\ger\\legal.bik", "movies"));
  CHECK(!InFolder("bash.bik", "movies"));

  // A prefix in any case.
  CHECK(AfterPrefix("SG_KingPen", "SG_") == "KingPen");
  CHECK(AfterPrefix("sg_kingpen", "SG_") == "kingpen");
  CHECK(AfterPrefix("SG_", "SG_") == "");
  CHECK(AfterPrefix("Soul_Gem_Aviator_Mix_360", "SG_") == "");

  CHECK(SameName("DaBomb", "Dabomb"));
  CHECK(!SameName("DaBomb", "DaBombs"));

  // Catalog names from movie names (the first in release order) and toy ids.
  CHECK(SkylanderNamed("chopchop") == "Chop Chop");
  CHECK(SkylanderNamed("ChopChop") == "Chop Chop");
  CHECK(SkylanderNamed("drillsergeant") == "Drill Sergeant");
  CHECK(SkylanderNamed("Bash") == "Bash");
  CHECK(SkylanderNamed("JetVac") == "Jet-Vac");
  CHECK(SkylanderNamed("HotStreak") == "Hot Streak");
  CHECK(SkylanderNamed("KingPen") == "King Pen");
  CHECK(SkylanderNamed("atvilogo") == "");
  CHECK(SkylanderNamed("sl_sc52") == "");
  CHECK(SkylanderNamed("") == "");
  CHECK(SkylanderWithId(450) == "Gusto");
  CHECK(SkylanderWithId(462) == "Snap Shot");
  CHECK(SkylanderWithId(485) == "Blackout");
  CHECK(SkylanderWithId(9999) == "");

  return Finish("soul_gem_names");
}
