// LightForOldPortal: what a portal from before Trap Team gets for the light commands of the
// newer games (the owner's portal stayed dark in Trap Team on Automatic, 2026-11-10).

#include <initializer_list>

#include "portal/old_portal_light.h"
#include "test_util.h"

using namespace skylanders::portal;

namespace {

Report R(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

}  // namespace

int main() {
  // Side 0 of a J (as Trap Team sent it to that portal: red with a 2 s fade) becomes C, red.
  const auto red = LightForOldPortal(R({'J', 0x00, 0xFF, 0x00, 0x00, 0xD0, 0x07}));
  REQUIRE_OR_RETURN(red.has_value());
  CHECK(*red == R({'C', 0xFF, 0x00, 0x00}));

  // Another side's J and the trap light's L send nothing.
  CHECK(!LightForOldPortal(R({'J', 0x02, 0x00, 0x00, 0xFF, 0xD0, 0x07})).has_value());
  CHECK(!LightForOldPortal(R({'J', 0x01, 0x00, 0xFF, 0x00, 0xD0, 0x07})).has_value());
  CHECK(!LightForOldPortal(R({'L', 0x01})).has_value());

  // C (Spyro's Adventure, Giants, Swap Force) and every other command go on as they are.
  const Report off = R({'C', 0x00, 0x00, 0x00});
  CHECK(LightForOldPortal(off) == off);
  const Report query = R({'Q', 0x10, 0x01});
  CHECK(LightForOldPortal(query) == query);
  const Report ready = R({'R'});
  CHECK(LightForOldPortal(ready) == ready);

  return Finish("old_portal_light");
}
