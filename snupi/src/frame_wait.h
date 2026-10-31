#pragma once
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <thread>

namespace snupi::framerate {
inline int64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void WaitUntil(int64_t due) {
  struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~Timer() { if (handle) CloseHandle(handle); }
  };
  thread_local Timer timer;
  for (auto remaining = due - Now(); remaining > 0; remaining = due - Now()) {
    // Windows may deliver a timer several hundred microseconds late. Leave a
    // 1ms finishing margin so high caps (especially 240) do not undershoot.
    if (remaining > 1250000) {
      if (timer.handle) {
        LARGE_INTEGER relative;
        relative.QuadPart = -(remaining - 1000000) / 100;
        if (SetWaitableTimer(timer.handle, &relative, 0, nullptr, nullptr, false))
          WaitForSingleObject(timer.handle, INFINITE);
        else std::this_thread::sleep_for(std::chrono::nanoseconds(remaining - 1000000));
      } else std::this_thread::sleep_for(std::chrono::nanoseconds(remaining - 1000000));
    } else YieldProcessor();
  }
}
}
