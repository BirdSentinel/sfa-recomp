#pragma once
#include <filesystem>
namespace rex { class Runtime; namespace ui { class Window; class WindowedAppContext; } }
namespace snupi::display {
void Initialize(rex::Runtime&, rex::ui::Window&, rex::ui::WindowedAppContext&,
                const std::filesystem::path& game_root,
                const std::filesystem::path& user_root,
                const std::filesystem::path& cache_root);
void Shutdown();
}
