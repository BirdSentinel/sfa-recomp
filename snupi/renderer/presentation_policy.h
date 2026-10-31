#pragma once
#include <dxgi.h>

namespace snupi::renderer {
struct PresentationPolicy {
  UINT interval;
  UINT flags;
};
constexpr PresentationPolicy Policy(bool vsync, bool allows_tearing) {
  // DXGI forbids ALLOW_TEARING with a nonzero sync interval. Do not discard
  // the synchronized queue with RESTART either.
  return vsync ? PresentationPolicy{1, 0}
               : PresentationPolicy{0, DXGI_PRESENT_RESTART |
                    (allows_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u)};
}
}
