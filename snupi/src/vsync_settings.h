#pragma once
#include <filesystem>
namespace snupi::vsync {
// Initialize/Select run on the UI thread, where host presentations are made.
void Initialize(const std::filesystem::path& user_root);
int SelectedIndex();
int Select(int index);
void SelfTestTick();
}
