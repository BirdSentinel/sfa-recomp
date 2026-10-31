#include "vsync_settings.h"
#include "frame_limiter.h"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <chrono>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(vsync_selftest, false, "Debug",
                    "Exercise live VSync, persistence and FPS independence, then restore settings");

namespace snupi::vsync {
namespace {
std::atomic<int> selected = 0;
std::filesystem::path settings_path;
}
void Initialize(const std::filesystem::path& user_root) {
  settings_path = user_root / "vsync.txt";
  int index = 0;
  std::ifstream input(settings_path);
  if (!(input >> index) || index < 0 || index > 1) index = 0;
  if (!rex::cvar::SetFlagByName("host_vsync", index ? "true" : "false"))
    throw std::runtime_error("VSync requires the game's current RTV presenter plugin");
  selected = index;
  REXLOG_INFO("VSync initialized: {}", index ? "On" : "Off");
}
int SelectedIndex() { return selected.load(); }
int Select(int index) {
  if (index < 0 || index > 1 || index == selected.load()) return selected.load();
  if (!rex::cvar::SetFlagByName("host_vsync", index ? "true" : "false")) {
    REXLOG_WARN("VSync change could not be applied");
    return selected.load();
  }
  selected = index;
  try {
    std::filesystem::create_directories(settings_path.parent_path());
    auto temporary = settings_path;
    temporary += ".tmp";
    std::ofstream output(temporary);
    output << index << '\n';
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), settings_path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      REXLOG_WARN("Could not save VSync setting");
  } catch (const std::exception& error) {
    REXLOG_WARN("Could not save VSync setting: {}", error.what());
  }
  REXLOG_INFO("VSync applied: {}", index ? "On" : "Off");
  return index;
}

void SelfTestTick() {
  if (!REXCVAR_GET(vsync_selftest)) return;
  using Clock = std::chrono::steady_clock;
  static auto last = Clock::now();
  static int phase = 0;
  static const int original = SelectedIndex();
  if (phase == 2 || Clock::now() - last < std::chrono::seconds(2)) return;
  const int wanted = phase == 0 ? 1 - original : original;
  if (Select(wanted) != wanted) throw std::runtime_error("Live VSync self-test failed");
  int saved = -1;
  std::ifstream input(settings_path);
  input >> saved;
  const auto value = wanted ? "true" : "false";
  if (saved != wanted || rex::cvar::GetFlagByName("host_vsync") != value)
    throw std::runtime_error("VSync persistence self-test failed");
  const auto fps = snupi::framerate::SelectedIndex();
  snupi::framerate::Select((fps + 1) % snupi::framerate::kChoiceCount);
  const bool independent = rex::cvar::GetFlagByName("host_vsync") == value;
  snupi::framerate::Select(fps);
  if (!independent) throw std::runtime_error("FPS selector changed VSync");
  last = Clock::now();
  if (++phase == 2)
    REXLOG_INFO("VSync self-test passed: live On/Off, persistence, FPS independence; settings restored");
}
}
