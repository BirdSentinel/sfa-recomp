#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace snupi::startup {
// Distinct initial value identifies this one runtime timer after the engine
// replaces package object names with generated instance names.
inline constexpr uint32_t kIntroGateTimerBits = 0x3b1a7f41;
// InitialLoadingInterface.pkg, package v25. Preserve every export offset and
// reference. Remove the logo movie, loading-screen wait, and save disclaimer.
inline void PatchIntroPackage(std::vector<uint8_t>& data) {
  auto word = [&](size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Truncated intro package");
    return (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
           (uint32_t(data[offset + 2]) << 8) | data[offset + 3];
  };
  auto write = [&](size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[offset + i] = uint8_t(value >> (24 - 8 * i));
  };
  // Guard the package layout and the exact serialized intro keys before edits.
  constexpr std::string_view logos = "FrontEnd_Logos";
  constexpr std::string_view intro = "FrontEnd_Intro";
  if (data.size() != 61751 || word(0) != 0x9e2a83c1 || word(4) != 25 ||
      word(5640 + 24) != 16481 || word(5640 + 20) != 306 ||
      word(5996 + 24) != 17059 || word(5996 + 20) != 52 ||
      word(0x40ab) != 38 || word(0x40b1) != 2 || word(0x409f) != 1 ||
      !std::equal(logos.begin(), logos.end(), data.begin() + 0x40b7) ||
      !std::equal(intro.begin(), intro.end(), data.begin() + 0x40c8) ||
      word(0x42cb) != 0x41400000 || word(0x3eef) != 0x40900000 ||
      word(0x4e77) != 4 || word(9327 + 20) != 149) {
    throw std::runtime_error("-skipintro: unsupported InitialLoadingInterface.pkg");
  }
  // Remove only the first serialized string (FrontEnd_Logos). Keep
  // FrontEnd_Intro intact. Its playback is separate from the UI timer below.
  constexpr size_t end = 16481 + 306;
  std::move(data.begin() + 0x40c6, data.begin() + end, data.begin() + 0x40b5);
  std::fill(data.begin() + end - 17, data.begin() + end, 0);
  write(0x40ab, 21);
  write(0x40b1, 1);
  write(5640 + 20, 306 - 17);
  write(5640 + 28, 306 - 17);
  // The sound cue belonged to the second movie (after Logos). Intro is now
  // first, so let the native FMV controller start its music at index zero.
  write(0x409f, 0);
  // This timer gates WaitingForLoadingInterfaceToExit, not movie playback.
  // The runtime holds this timer until FrontEnd_Intro completes, then lets
  // it expire. IsBusyLoading still gates the transition independently.
  write(0x42cb, kIntroGateTimerBits);
  // Keep the disclaimer's existing close/next-interface action, but make it
  // fire on the first update and render nothing while it transitions.
  write(0x3eef, 0x3a83126f);  // 0.001 seconds (positive timer crossing).
  constexpr size_t disclaimer_end = 20043 + 149;
  std::move(data.begin() + 0x4e8b, data.begin() + disclaimer_end,
            data.begin() + 0x4e7b);
  std::fill(data.begin() + disclaimer_end - 16, data.begin() + disclaimer_end, 0);
  write(0x4e71, 4);
  write(0x4e77, 0);
  write(9327 + 20, 149 - 16);
  write(9327 + 28, 149 - 16);
}
}  // namespace snupi::startup
