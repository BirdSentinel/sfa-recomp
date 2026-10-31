#include "frame_limiter.h"
#include "frame_wait.h"
#include <cstdlib>
#include <iostream>

void Check(bool value) { if (!value) std::abort(); }
int main() {
  using namespace snupi::framerate;
  Check((kRates == std::array<int, 9>{0,30,60,90,120,144,165,180,240}));
  for (const int fps : kRates) {
    Schedule clock;
    int64_t now = 1000000000;
    Check(clock.Deadline(now, fps) == now);
    clock.Presented(now);
    if (!fps) { Check(clock.Deadline(now + 1, fps) == now + 1); continue; }
    const int64_t period = (1000000000LL + fps - 1) / fps;
    const auto start = now;
    for (int i = 0; i < fps * 2; ++i) {
      const auto due = clock.Deadline(now + 1000000, fps); // 1ms rendering work
      Check(due == now + period);
      now = due;
      clock.Presented(now);
    }
    Check(now - start >= 2000000000LL && now - start < 2000001000LL);
    now += 500000000; // loading stall: no catch-up bursts
    Check(clock.Deadline(now, fps) == now);
    clock.Presented(now);
    Check(clock.Deadline(now, fps) == now + period);
    Check(clock.Deadline(now, 0) == now); // Off removes the deadline immediately
    Check(clock.Deadline(now, 60) == now); // changing caps resets old deadlines
    // Exercise the actual Windows wait path used by the presentation hook.
    Schedule real;
    const auto begin = Now();
    real.Deadline(begin, fps);
    real.Presented(begin);
    const int frames = fps / 3;
    for (int i = 0; i < frames; ++i) {
      WaitUntil(real.Deadline(Now(), fps));
      real.Presented(Now());
    }
    const double measured = double(frames) * 1e9 / double(Now() - begin);
    Check(measured <= fps * 1.001);
    Check(measured >= fps * 0.85); // allow scheduler load on the test host
    std::cout << fps << " fps timer: " << measured << " fps measured\n";
  }
  std::cout << "All FPS choices, uncapped mode, stalls and live changes passed\n";
}
