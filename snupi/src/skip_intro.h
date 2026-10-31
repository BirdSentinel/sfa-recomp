#pragma once
#include <filesystem>
namespace rex { class Runtime; }
namespace snupi::startup {
void PrepareSkipIntro(rex::Runtime& runtime, const std::filesystem::path& game_root,
                     const std::filesystem::path& cache_root);
}
