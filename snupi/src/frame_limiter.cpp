#include "frame_limiter.h"
#include "frame_wait.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>
#include <limits>

REXCVAR_DEFINE_BOOL(frame_limit_audit, false, "Debug", "Log game and host presentation rates");

namespace snupi::framerate {
namespace {
std::atomic<int> selected = 0;
std::filesystem::path settings_path;
}

void Initialize(const std::filesystem::path& user_root) {
  settings_path = user_root / "framerate.txt";
  int fps = 0;
  std::ifstream input(settings_path);
  if (input >> fps) {
    for (int i = 0; i < kChoiceCount; ++i) if (kRates[i] == fps) selected = i;
  }
  // The guest's 60 Hz vblank emulation must not impose a second, lower cap.
  // Game updates and host presentations both use the selected limit below.
  rex::cvar::SetFlagByName("vsync", "false");
  REXLOG_INFO("Frame limit initialized: {} fps (0 = Off)", kRates[selected]);
}

int SelectedIndex() { return selected.load(); }
int Select(int index) {
  if (index < 0 || index >= kChoiceCount) return selected;
  if (selected.exchange(index) == index) return index;
  rex::cvar::SetFlagByName("vsync", "false");
  try {
    std::filesystem::create_directories(settings_path.parent_path());
    auto temporary = settings_path;
    temporary += ".tmp";
    std::ofstream output(temporary);
    output << kRates[index] << '\n';
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), settings_path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      REXLOG_WARN("Could not save frame limit");
  } catch (const std::exception& error) {
    REXLOG_WARN("Could not save frame limit: {}", error.what());
  }
  REXLOG_INFO("Frame limit applied: {} fps (0 = Off), menus and gameplay", kRates[index]);
  return index;
}

void Pace(const char* path) {
  thread_local Schedule schedule;
  const int fps = kRates[selected.load()];
  const auto due = schedule.Deadline(Now(), fps);
  WaitUntil(due);
  // Use actual presentation time: never burst to catch up after a slow frame.
  schedule.Presented(Now());
  if (REXCVAR_GET(frame_limit_audit)) {
    thread_local int64_t start = Now();
    thread_local unsigned frames = 0;
    ++frames;
    const auto elapsed = Now() - start;
    if (elapsed >= 2000000000LL) {
      REXLOG_INFO("Frame pacing ({}): limit {}, measured {:.2f} fps over {} frames", path, fps,
                  double(frames) * 1e9 / double(elapsed), frames);
      start = Now(); frames = 0;
    }
  }
}

namespace {
class PresentationPacer final : public rex::ui::UIDrawer {
 public:
  void Draw(rex::ui::UIDrawContext&) override { Pace("host"); }
};
// The presenter stores a non-owning pointer; keep it alive through shutdown.
PresentationPacer presentation_pacer;
}

void AttachPresenter(rex::ui::Presenter& presenter) {
  // A registered drawer routes ALL swapchain presentations through the UI
  // thread, including guest requests and the independent monitor-rate repaint
  // loop. Pacing only guest D3D Present leaves that second stream uncapped
  // (e.g. 30 guest frames + 180 UI repaints = 210 host presents per second).
  // Run last, immediately before submission/presentation. Do not request extra
  // repaints ourselves. The guest retains its own clock to bound simulation
  // work; these are separate threads, not two waits on the same schedule.
  presenter.AddUIDrawerFromUIThread(&presentation_pacer,
                                  std::numeric_limits<size_t>::max());
  REXLOG_INFO("Frame limiter attached to host presentation path");
}
}

REX_EXTERN(__imp__sub_823747C0);
// Shared D3D presentation path, including frontend movies, menus and gameplay.
REX_HOOK_RAW(sub_823747C0) {
  snupi::framerate::Pace("guest");
  __imp__sub_823747C0(ctx, base);
}

