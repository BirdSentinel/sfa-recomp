#include "internal_resolution.h"
#include <atomic>
#include <fstream>
#include <stdexcept>
#include <array>
#include <bit>
#include <windows.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(snupi_internal_resolution, 1, "Game", "Saved internal draw scale (1x or 2x)")
    .range(1, 2).lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(internal_resolution_selftest, false, "Debug",
    "Validate scale persistence and fixed autosave without modifying user settings");
REX_EXTERN(sub_828504B8);

namespace snupi::internal_resolution {
namespace {
std::atomic<int> selected = 0;
std::filesystem::path settings_path;
void SelfTest();
}
void Initialize(const std::filesystem::path& user_root) {
  settings_path = user_root / "internal-resolution.txt";
  int scale = 1;
  std::ifstream input(settings_path);
  if (!(input >> scale) || (scale != 1 && scale != 2)) scale = 1;
  selected = scale - 1;
  if (!rex::cvar::SetFlagByName("snupi_internal_resolution", std::to_string(scale)))
    throw std::runtime_error("Cannot initialize internal resolution");
  REXLOG_INFO("Internal resolution initialized: {}x ({}x{} scene); Autosave forced On",
              scale, 1152 * scale, 640 * scale);
  if (REXCVAR_GET(internal_resolution_selftest)) SelfTest();
}
int SelectedIndex() { return selected.load(); }
int Select(int index) {
  if (index < 0 || index > 1 || index == selected.load()) return selected;
  try {
    std::filesystem::create_directories(settings_path.parent_path());
    auto temporary = settings_path; temporary += ".tmp";
    std::ofstream output(temporary); output << index + 1 << '\n'; output.close();
    if (!output || !MoveFileExW(temporary.c_str(), settings_path.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Cannot persist internal resolution");
    selected = index;
    REXLOG_INFO("Internal resolution saved: {}x; applies after restarting the game", index + 1);
  } catch (const std::exception& e) { REXLOG_WARN("{}", e.what()); }
  return selected;
}
namespace {
void SelfTest() {
  const auto original_path = settings_path;
  const auto original_index = selected.load();
  settings_path = original_path.parent_path() / "cache/internal-resolution-selftest.txt";
  auto restore = [&] { std::filesystem::remove(settings_path); settings_path = original_path; selected = original_index; };
  try {
    selected = 0;
    for (int index : {1, 0}) {
      if (Select(index) != index) throw std::runtime_error("Scale selection failed");
      int saved = 0; std::ifstream input(settings_path); input >> saved;
      if (saved != index + 1) throw std::runtime_error("Scale persistence failed");
    }
    if (Select(-1) != 0 || Select(2) != 0) throw std::runtime_error("Invalid scale accepted");
    // Exercise the actual native getter and hook on a synthetic local-player
    // table. No game profile or save data is involved in this regression test.
    std::array<uint8_t, 1024> memory{};
    auto store = [&](uint32_t a, uint32_t v) { v = std::byteswap(v); std::memcpy(memory.data() + a, &v, 4); };
    auto read = [&](uint32_t a) { uint32_t v; std::memcpy(&v, memory.data() + a, 4); return std::byteswap(v); };
    store(64 + 176, 1); store(64 + 184, 512); store(512, 640);
    PPCContext probe{}; probe.r3.u64 = 64; probe.r4.u64 = 0;
    sub_828504B8(probe, memory.data());
    if (probe.r3.u32 != 640 || read(640 + 56) != 1)
      throw std::runtime_error("Legacy Autosave Off was not forced On");
    probe.r3.u64 = 64; probe.r4.s64 = -1;
    sub_828504B8(probe, memory.data());
    if (probe.r3.u32 || read(56)) throw std::runtime_error("Invalid player changed by autosave hook");
    restore();
    REXLOG_INFO("Internal resolution self-test PASSED: 1x/2x persistence, invalid choices, Autosave forced On");
  } catch (...) { restore(); throw; }
}
}
}

// LocalPlayer::savingEnabled is the 32-bit boolean at +56, read by the
// native IsPlayerSavingEnabled condition and written by PlayerSavingEnabled.
// Reinstate it when obtaining a player, including profiles loaded with Off.
REX_EXTERN(__imp__sub_828504B8);
REX_HOOK_RAW(sub_828504B8) {
  __imp__sub_828504B8(ctx, base);
  const uint32_t on = std::byteswap(uint32_t(1));
  if (ctx.r3.u32) std::memcpy(base + ctx.r3.u32 + 56, &on, 4);
}

// Force the native action's requested value to On before it can disable saving
// or invoke the disable-saving callback. This covers non-settings call sites.
REX_EXTERN(__imp__sub_828E7CC0);
REX_HOOK_RAW(sub_828E7CC0) {
  const uint32_t on = std::byteswap(uint32_t(1));
  std::memcpy(base + ctx.r3.u32 + 76, &on, 4);
  __imp__sub_828E7CC0(ctx, base);
}
