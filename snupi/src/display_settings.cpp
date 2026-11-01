#include "display_settings.h"
#include "display_guest.h"
#include "frame_limiter.h"
#include "vsync_settings.h"
#include "internal_resolution.h"
#include "window_resize.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/ui/window_listener.h>

REXCVAR_DEFINE_BOOL(display_settings_selftest, false, "Debug",
                    "Exercise native display changes and persistence before starting the game")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr uint32_t kResolutionStrings = 0x7100, kModeStrings = 0x7200;
constexpr uint32_t kFramerateStrings = 0x7300;
constexpr uint32_t kVsyncStrings = 0x7400;
constexpr uint32_t kInternalResolutionStrings = 0x7500;
constexpr int kMaxResolutions = 32;
struct Resolution { int width, height; };
std::vector<Resolution> resolutions;  // Immutable after initialization.
rex::ui::Window* window = nullptr;
rex::ui::WindowedAppContext* context = nullptr;
HWND native_window = nullptr;
MONITORINFOEXW monitor{};
DEVMODEW desktop{};
bool exclusive_active = false, applying = false;
std::filesystem::path settings_path;
std::atomic<bool> ready = false;
std::atomic<int> selected_resolution = 0, selected_mode = 0;
int desktop_index = 0;

int ChoiceIndex(uint32_t tag) {
  if (tag == kInternalResolutionStrings) return snupi::internal_resolution::SelectedIndex();
  if (tag == kVsyncStrings) return snupi::vsync::SelectedIndex();
  if (tag == kFramerateStrings) return snupi::framerate::SelectedIndex();
  return tag == kResolutionStrings ? selected_resolution.load() : selected_mode.load();
}

uint32_t Word(const uint8_t* base, uint32_t address) {
  return uint32_t(base[address]) << 24 | uint32_t(base[address + 1]) << 16 |
         uint32_t(base[address + 2]) << 8 | base[address + 3];
}

// Identify only our FLocString arrays, using their reserved localization IDs.
// Other subclasses of GlobalVariable have different layouts and must not be read.
uint32_t Tag(const uint8_t* base, uint32_t object) {
  if (!ready) return 0;
  return snupi::display::VariableTag([&](uint32_t address) { return Word(base, address); }, object);
}

int IndexFor(int width, int height) {
  for (size_t i = 0; i < resolutions.size(); ++i) {
    if (resolutions[i].width == width && resolutions[i].height == height) return int(i);
  }
  return -1;
}

// UI thread only. The runtime DLL owns SDL, so use its Window API to keep
// fullscreen state synchronized, and Win32 for client size/display modes.
void RestoreDesktop() {
  if (exclusive_active) {
    ChangeDisplaySettingsExW(monitor.szDevice, &desktop, nullptr, 0, nullptr);
    exclusive_active = false;
  }
}

bool Apply(int resolution, int mode) {
  const auto size = resolutions.at(resolution);
  DEVMODEW exclusive{};
  if (mode == 2) {
    bool found = false;
    DEVMODEW candidate{};
    candidate.dmSize = sizeof(candidate);
    for (DWORD i = 0; EnumDisplaySettingsW(monitor.szDevice, i, &candidate); ++i) {
      if (candidate.dmPelsWidth != size.width || candidate.dmPelsHeight != size.height ||
          candidate.dmBitsPerPel != desktop.dmBitsPerPel) continue;
      if (!found || abs(int(candidate.dmDisplayFrequency) - int(desktop.dmDisplayFrequency)) <
                       abs(int(exclusive.dmDisplayFrequency) - int(desktop.dmDisplayFrequency))) {
        exclusive = candidate; found = true;
      }
    }
    exclusive.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    const auto test = found ? ChangeDisplaySettingsExW(monitor.szDevice, &exclusive, nullptr,
                                                      CDS_TEST, nullptr) : DISP_CHANGE_BADMODE;
    if (test != DISP_CHANGE_SUCCESSFUL) {
      REXLOG_WARN("Fullscreen mode test failed: {} ({}x{} at {} Hz)", test,
                  size.width, size.height, exclusive.dmDisplayFrequency);
      return false;
    }
  }
  applying = true;
  window->SetFullscreen(false);
  RestoreDesktop();
  bool success = true;
  if (mode == 0) {
    RECT rect{0, 0, size.width, size.height};
    const auto style = DWORD(GetWindowLongPtrW(native_window, GWL_STYLE));
    const auto ex_style = DWORD(GetWindowLongPtrW(native_window, GWL_EXSTYLE));
    AdjustWindowRectExForDpi(&rect, style, false, ex_style, GetDpiForWindow(native_window));
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const int x = monitor.rcWork.left + std::max(0L, (monitor.rcWork.right - monitor.rcWork.left - width) / 2);
    const int y = monitor.rcWork.top + std::max(0L, (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2);
    success = SetWindowPos(native_window, nullptr, x, y, width, height,
                           SWP_NOZORDER | SWP_NOACTIVATE) != 0;
  } else {
    window->SetFullscreen(true);
    if (mode == 2) {
      const auto status = ChangeDisplaySettingsExW(monitor.szDevice, &exclusive, nullptr,
                                                   CDS_FULLSCREEN, nullptr);
      success = status == DISP_CHANGE_SUCCESSFUL;
      if (!success) REXLOG_WARN("Fullscreen mode apply failed: {}", status);
      exclusive_active = success;
    }
    if (success) {
      success = SetWindowPos(native_window, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
          size.width, size.height, SWP_NOZORDER | SWP_NOACTIVATE) != 0;
    }
  }
  applying = false;
  if (success) REXLOG_INFO("Display settings applied: {}x{}, {}", size.width, size.height,
              mode == 0 ? "Windowed" : mode == 1 ? "Borderless" : "Fullscreen");
  return success;
}

class DisplayListener final : public rex::ui::WindowListener {
 public:
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    if (!applying && exclusive_active) {
      RestoreDesktop();
      ShowWindow(native_window, SW_MINIMIZE);
    }
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    if (ready && !applying && selected_mode == 2 && !exclusive_active)
      Apply(selected_resolution, 2);
  }
  void OnClosing(rex::ui::UIEvent&) override { RestoreDesktop(); }
} listener;

void Save() {
  const auto size = resolutions.at(selected_resolution.load());
  std::filesystem::create_directories(settings_path.parent_path());
  auto temporary = settings_path;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  output << size.width << ' ' << size.height << ' ' << selected_mode.load() << '\n';
  output.close();
  if (!output) { REXLOG_WARN("Could not save display settings"); return; }
  if (!MoveFileExW(temporary.c_str(), settings_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    REXLOG_WARN("Could not save display settings: Win32 error {}", GetLastError());
}

int Change(uint32_t tag, int index) {
  if (tag == kInternalResolutionStrings) return snupi::internal_resolution::Select(index);
  if (tag == kVsyncStrings) {
    int result = snupi::vsync::SelectedIndex();
    context->CallInUIThreadSynchronous([&] { result = snupi::vsync::Select(index); });
    return result;
  }
  if (tag == kFramerateStrings) return snupi::framerate::Select(index);
  int result = tag == kResolutionStrings ? selected_resolution.load() : selected_mode.load();
  context->CallInUIThreadSynchronous([&] {
    if (!ready) return;
    const int old_resolution = selected_resolution, old_mode = selected_mode;
    int resolution = tag == kResolutionStrings ? index : old_resolution;
    const int mode = tag == kModeStrings ? index : old_mode;
    if (resolution < 0 || resolution >= int(resolutions.size()) || mode < 0 || mode > 2) return;
    // Desktop fullscreen always uses the monitor's desktop dimensions.
    if (mode == 1) resolution = desktop_index;
    if (resolution != old_resolution || mode != old_mode) {
      if (Apply(resolution, mode)) {
        selected_resolution = resolution;
        selected_mode = mode;
        try { Save(); } catch (const std::exception& e) {
          REXLOG_WARN("Could not save display settings: {}", e.what());
        }
      } else {
        REXLOG_WARN("Display change failed; restoring previous mode");
        Apply(old_resolution, old_mode);
      }
    }
    result = tag == kResolutionStrings ? selected_resolution.load() : selected_mode.load();
  });
  return result;
}

void PrepareLocalization(rex::Runtime& runtime, const std::filesystem::path& game_root,
                         const std::filesystem::path& cache_root) {
  std::string entries = "\r\n[PC Display]\r\nResolution[28672]=\"Resolution\"\r\n"
                        "DisplayMode[28673]=\"Display Mode\"\r\n"
                        "VSync[28674]=\"VSync\"\r\n"
                        "InternalResolution[28675]=\"Internal resolution (restart)\"\r\n"
                        "Internal1x[29952]=\"1x\"\r\nInternal2x[29953]=\"2x\"\r\n"
                        "VSyncOff[29696]=\"Off\"\r\nVSyncOn[29697]=\"On\"\r\n";
  for (int i = 0; i < kMaxResolutions; ++i) {
    const auto size = resolutions[std::min(i, int(resolutions.size()) - 1)];
    entries += "Resolution" + std::to_string(i) + "[" + std::to_string(kResolutionStrings + i) +
               "]=\"" + std::to_string(size.width) + " x " + std::to_string(size.height) + "\"\r\n";
  }
  entries += "Windowed[29184]=\"Windowed\"\r\nBorderless[29185]=\"Borderless\"\r\n"
             "Fullscreen[29186]=\"Fullscreen\"\r\n";
  for (int i = 0; i < snupi::framerate::kChoiceCount; ++i) {
    const auto label = i == 0 ? std::string("Off") :
        std::to_string(snupi::framerate::kRates[i]) + "fps";
    entries += "FrameLimit" + std::to_string(i) + "[" +
               std::to_string(kFramerateStrings + i) + "]=\"" + label + "\"\r\n";
  }
  const auto folder = cache_root / "display-localization";
  std::filesystem::create_directories(folder);
  for (const auto& item : std::filesystem::directory_iterator(game_root / "Localization")) {
    if (!item.is_regular_file()) continue;
    std::ifstream input(item.path(), std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
    if (item.path().extension() == ".loc") {
      if (data.size() < 2 || uint8_t(data[0]) != 0xff || uint8_t(data[1]) != 0xfe)
        throw std::runtime_error("Unsupported localization encoding: " + item.path().string());
      for (char c : entries) { data.push_back(c); data.push_back(0); }
    }
    std::ofstream output(folder / item.path().filename(), std::ios::binary | std::ios::trunc);
    output.write(data.data(), data.size());
    output.close();
    if (!output) throw std::runtime_error("Cannot write display localization cache");
  }
  constexpr auto mount = "\\Device\\SnupiDisplayLocalization";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder, true);
  if (!device->Initialize()) throw std::runtime_error("Cannot mount display localization");
  runtime.file_system()->RegisterDevice(std::move(device));
  runtime.file_system()->RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\Localization", mount);
  auto* parent = runtime.file_system()->ResolvePath("D:\\Localization");
  const auto* entry = parent ? parent->GetChild("FlyingAce.eng.loc") : nullptr;
  if (!entry || entry->device()->mount_path() != mount)
    throw std::runtime_error("Display localization redirect failed");
}

void SelfTest() {
  const int original_resolution = selected_resolution, original_mode = selected_mode;
  const auto original_path = settings_path;
  settings_path = original_path.parent_path() / "cache/display-test-settings.txt";
  auto restore = [&] {
    Apply(original_resolution, original_mode);
    selected_resolution = original_resolution;
    selected_mode = original_mode;
    std::filesystem::remove(settings_path);
    settings_path = original_path;
  };
  try {
    auto check = [&](int resolution, int mode) {
      Change(kModeStrings, mode);
      Change(kResolutionStrings, resolution);
      const int expected_resolution = mode == 1 ? desktop_index : resolution;
      const auto size = resolutions[expected_resolution];
      RECT rect{};
      DEVMODEW actual{};
      actual.dmSize = sizeof(actual);
      if (selected_mode != mode || selected_resolution != expected_resolution ||
          !GetClientRect(native_window, &rect) || rect.right != size.width || rect.bottom != size.height ||
          !EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &actual) ||
          actual.dmPelsWidth != (mode == 2 ? size.width : desktop.dmPelsWidth) ||
          actual.dmPelsHeight != (mode == 2 ? size.height : desktop.dmPelsHeight))
        throw std::runtime_error("Display self-test: mode/size mismatch");
      Save();
      int w, h, saved_mode;
      std::ifstream input(settings_path);
      if (!(input >> w >> h >> saved_mode) || w != size.width || h != size.height || saved_mode != mode)
        throw std::runtime_error("Display self-test: persistence mismatch");
    };
    check(0, 0);
    check(0, 1);
    check(0, 2);
    check(desktop_index, 2);
    check(original_resolution, 0);
    restore();
    REXLOG_INFO("Display self-test passed: all three modes, physical sizes, desktop restoration and saved choices");
  } catch (...) { restore(); throw; }
}
}

namespace snupi::display {
void Initialize(rex::Runtime& runtime, rex::ui::Window& app_window,
                rex::ui::WindowedAppContext& app_context,
                const std::filesystem::path& game_root,
                const std::filesystem::path& user_root,
                const std::filesystem::path& cache_root) {
  window = &app_window;
  context = &app_context;
  settings_path = user_root / "display-settings.txt";
  snupi::framerate::Initialize(user_root);
  REXLOG_INFO("Preparing native display settings (UI thread: {})", context->IsInUIThread());
  if (!context->CallInUIThreadSynchronous([&] {
    window->SetTitle("Snoopy Flying Ace");
    snupi::vsync::Initialize(user_root);
    if (auto* graphics = runtime.graphics_system())
      if (auto* presenter = graphics->presenter())
        snupi::framerate::AttachPresenter(*presenter);
    native_window = static_cast<HWND>(window->GetNativeWindowHandle());
    snupi::resize::Initialize(*window);
    monitor.cbSize = sizeof(monitor);
    desktop.dmSize = sizeof(desktop);
    if (!native_window || !GetMonitorInfoW(MonitorFromWindow(native_window, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &desktop))
      throw std::runtime_error("Cannot query the game's display");
    auto add = [&](int w, int h) {
      if (w >= 640 && h >= 480 && IndexFor(w, h) < 0 && resolutions.size() < kMaxResolutions)
        resolutions.push_back({w, h});
    };
    add(desktop.dmPelsWidth, desktop.dmPelsHeight);
    DEVMODEW candidate{};
    candidate.dmSize = sizeof(candidate);
    for (DWORD i = 0; EnumDisplaySettingsW(monitor.szDevice, i, &candidate); ++i)
      if (candidate.dmBitsPerPel == desktop.dmBitsPerPel) add(candidate.dmPelsWidth, candidate.dmPelsHeight);
    std::sort(resolutions.begin(), resolutions.end(), [](const auto& a, const auto& b) {
      return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    desktop_index = IndexFor(desktop.dmPelsWidth, desktop.dmPelsHeight);
    int w = 1280, h = 720, mode = window->IsFullscreen() ? 1 : 0;
    RECT client{};
    if (GetClientRect(native_window, &client)) { w = client.right; h = client.bottom; }
    int saved_w, saved_h, saved_mode;
    std::ifstream input(settings_path);
    if (input >> saved_w >> saved_h >> saved_mode && saved_mode >= 0 && saved_mode <= 2 &&
        IndexFor(saved_w, saved_h) >= 0) {
      w = saved_w; h = saved_h; mode = saved_mode;
    }
    if (rex::cvar::GetFlagSource("fullscreen") == rex::cvar::Source::kCommandLine)
      mode = window->IsFullscreen() ? 1 : 0;
    int resolution = IndexFor(w, h);
    if (resolution < 0 || mode == 1) resolution = desktop_index;
    if (!Apply(resolution, mode)) {
      resolution = desktop_index; mode = 0;
      if (!Apply(resolution, mode)) throw std::runtime_error("Cannot initialize display settings");
    }
    selected_resolution = resolution;
    selected_mode = mode;
  })) throw std::runtime_error("UI thread unavailable for display settings");
  PrepareLocalization(runtime, game_root, cache_root);
  window->AddListener(&listener);
  ready = true;
  if (REXCVAR_GET(display_settings_selftest)) SelfTest();
  REXLOG_INFO("Native display selectors ready: {} supported resolutions", resolutions.size());
}
void Shutdown() {
  ready = false;
  if (window) window->RemoveListener(&listener);
  RestoreDesktop();
}
}

REX_EXTERN(__imp__sub_828D7FF0);
REX_EXTERN(__imp__sub_828F0210);
REX_EXTERN(__imp__sub_82302A98);
REX_EXTERN(__imp__sub_822E8A20);

// GlobalVariable::SetCurrentIndex: the game's own +/- actions have already
// wrapped the requested index. Preserve native invalidation and change events.
REX_HOOK_RAW(sub_828D7FF0) {
  if (const auto tag = Tag(base, ctx.r3.u32)) ctx.r4.s64 = Change(tag, ctx.r4.s32);
  __imp__sub_828D7FF0(ctx, base);
}

// Array count used by CurrentIndex wrap/clamp. Only supported resolutions cycle.
REX_HOOK_RAW(sub_82302A98) {
  if (Tag(base, ctx.r3.u32) == kResolutionStrings) { ctx.r3.u64 = resolutions.size(); return; }
  __imp__sub_82302A98(ctx, base);
}

// Every newly instantiated settings interface starts with the persisted choice.
REX_HOOK_RAW(sub_828F0210) {
  const auto object = ctx.r3.u32;
  __imp__sub_828F0210(ctx, base);
  if (const auto tag = Tag(base, object)) {
    auto update = ctx;
    update.r3.u64 = object;
    update.r4.s64 = ChoiceIndex(tag);
    __imp__sub_828D7FF0(update, base);
    REXLOG_INFO("Native settings selector initialized: {}, index {}", tag, ChoiceIndex(tag));
  }
}

// Refresh the displayed value when changing mode also changes the resolution
// (desktop-sized borderless). No cached pointers into short-lived guest menus.
REX_HOOK_RAW(sub_822E8A20) {
  if (const auto tag = Tag(base, ctx.r4.u32)) {
    auto update = ctx;
    update.r3.u64 = ctx.r4.u32;
    update.r4.s64 = ChoiceIndex(tag);
    __imp__sub_828D7FF0(update, base);
  }
  __imp__sub_822E8A20(ctx, base);
}
