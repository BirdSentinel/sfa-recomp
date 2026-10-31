#pragma once
#include <array>
#include <cstdint>
#include <filesystem>

namespace rex::ui { class Presenter; }

namespace snupi::framerate {
inline constexpr std::array<int, 9> kRates{0, 30, 60, 90, 120, 144, 165, 180, 240};
// Nine choices: Off and the eight requested numerical caps.
inline constexpr int kChoiceCount = 9;
void Initialize(const std::filesystem::path& user_root);
// Call on the UI thread before starting guest execution.
void AttachPresenter(rex::ui::Presenter& presenter);
int SelectedIndex();
int Select(int index);

class Schedule {
 public:
  int64_t Deadline(int64_t now, int fps) {
    if (fps <= 0 || fps != fps_ || !started_) {
      fps_ = fps; started_ = fps > 0; last_ = now;
      return now;
    }
    const auto due = last_ + (1000000000LL + fps - 1) / fps;
    return due > now ? due : now;
  }
  void Presented(int64_t now) { last_ = now; }
 private:
  int fps_ = 0;
  bool started_ = false;
  int64_t last_ = 0;
};
}
