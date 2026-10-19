#include "portal/portal_mode.h"
#include "test_util.h"

using skylanders::portal::ParsePortalMode;
using skylanders::portal::PortalMode;

int main() {
  CHECK(ParsePortalMode("software") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("none") == PortalMode::kNone);
  CHECK(ParsePortalMode("  Software \t") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("NONE") == PortalMode::kNone);

  CHECK(ParsePortalMode("usb") == PortalMode::kUsb);
  CHECK(ParsePortalMode("Hybrid") == PortalMode::kHybrid);

  // Unknown values are rejected.
  CHECK(!ParsePortalMode("").has_value());
  CHECK(!ParsePortalMode("   ").has_value());
  CHECK(!ParsePortalMode("banana").has_value());
  CHECK(!ParsePortalMode("software extra").has_value());

  return Finish("portal_mode");
}
