// snupi - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <stdexcept>
#include "skip_intro.h"
#include "display_settings.h"
#include "internal_resolution.h"

class SnupiApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SnupiApp>(new SnupiApp(ctx, "snupi",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  void OnPreSetup(rex::RuntimeConfig& config) override {
    snupi::internal_resolution::Initialize(user_data_root());
    // The launchers previously forced windowed mode. Keep that first-run
    // default, then let the saved in-game display choice take precedence.
    if (rex::cvar::GetFlagSource("fullscreen") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("fullscreen", "false");
    }
    // Keyboard emulation merges into player 1 alongside the physical pad.
    // Keep explicit config/command-line choices, including controller-only mode.
    if (rex::cvar::GetFlagSource("mnk_mode") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("mnk_mode", "true");
    }
    // Fixed game setting, applied after config/CLI parsing on every launch.
    if (!rex::cvar::SetFlagByName("license_mask", "1") ||
        rex::cvar::GetFlagByName("license_mask") != "1")
      throw std::runtime_error("Cannot initialize the fixed game license mask");
    REXLOG_INFO("Game license_mask fixed to 1");
    config.gpu_plugin = "xenos";
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnPostLoadXexImage() override {
    snupi::startup::PrepareStartupPackages(*runtime(), game_data_root(), cache_root());
    snupi::display::Initialize(*runtime(), *window(), app_context(), game_data_root(),
                               user_data_root(), cache_root());
  }
  // void OnPostSetup() override {}
  void OnShutdown() override {
    snupi::display::Shutdown();
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // Resolve against the executable, not the shortcut/shell working directory.
    // An explicit --game_data_root keeps precedence.
    if (paths.game_data_root.empty()) {
      const auto folder = rex::filesystem::GetExecutableFolder();
      std::error_code error;
      if (std::filesystem::is_regular_file(folder / "default.xex", error)) {
        paths.game_data_root = folder;
      }
    }
    // Keep profiles and progress with the game, including build-folder launches.
    // Explicit user/cache paths still take precedence.
    if (rex::cvar::GetFlagByName("user_data_root").empty()) {
      const auto folder = paths.game_data_root.empty()
                              ? rex::filesystem::GetExecutableFolder()
                              : paths.game_data_root;
      paths.user_data_root = folder / "saves";
      if (rex::cvar::GetFlagByName("cache_root").empty()) {
        paths.cache_root = paths.user_data_root / "cache";
      }
    }
  }
};
