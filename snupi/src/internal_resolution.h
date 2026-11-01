#pragma once
#include <filesystem>
namespace snupi::internal_resolution {
void Initialize(const std::filesystem::path& user_root);
int SelectedIndex();
int Select(int index);
}
