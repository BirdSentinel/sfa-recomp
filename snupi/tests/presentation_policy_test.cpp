#include "presentation_policy.h"
#include <cstdlib>
#include <iostream>
int main() {
  for (bool tearing : {false, true}) {
    const auto on = snupi::renderer::Policy(true, tearing);
    const auto off = snupi::renderer::Policy(false, tearing);
    if (on.interval != 1 || on.flags != 0 || off.interval != 0 ||
        bool(off.flags & DXGI_PRESENT_ALLOW_TEARING) != tearing ||
        !(off.flags & DXGI_PRESENT_RESTART)) std::abort();
  }
  std::cout << "VSync presentation intervals and tearing flags passed\n";
}
