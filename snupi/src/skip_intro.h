#pragma once
#include <filesystem>
namespace rex { class Runtime; }
namespace snupi::startup {
void PrepareStartupPackages(rex::Runtime& runtime, const std::filesystem::path& game_root,
                     const std::filesystem::path& cache_root);
}
