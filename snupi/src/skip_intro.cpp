#include "skip_intro.h"
#include "intro_package.h"
#include "main_menu_package.h"
#include <map>

#include <fstream>
#include <iterator>
#include <atomic>
#include <bit>
#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>

REXCVAR_DEFINE_BOOL(skipintro, false, "Game", "Skip startup logos and save disclaimer; keep FrontEnd_Intro")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(hidelive, false, "Game", "Hide Xbox LIVE, Leaderboards and Download Content from the main menu")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
std::atomic<bool> intro_complete = false;
std::atomic<uint32_t> intro_controller = 0;
std::atomic<uint32_t> intro_timer = 0;
std::atomic<bool> intro_gate_released = false;

uint32_t GuestWord(const uint8_t* base, uint32_t address) {
  return (uint32_t(base[address]) << 24) | (uint32_t(base[address + 1]) << 16) |
         (uint32_t(base[address + 2]) << 8) | base[address + 3];
}

bool GuestTextEquals(const uint8_t* base, uint32_t address, std::string_view text) {
  if (!address) return false;
  for (char c : text) {
    if (base[address] != 0 || base[address + 1] != static_cast<uint8_t>(c)) return false;
    address += 2;
  }
  return base[address] == 0 && base[address + 1] == 0;
}
}

namespace snupi::startup {
void PrepareStartupPackages(rex::Runtime& runtime, const std::filesystem::path& game_root,
                            const std::filesystem::path& cache_root) {
  const bool skip = REXCVAR_GET(skipintro), hide = REXCVAR_GET(hidelive);
  const auto source = game_root / "Packages/UI";
  std::map<std::string, std::vector<uint8_t>> patched;
  auto read = [&](const char* name) -> std::vector<uint8_t>& {
    std::ifstream input(source / name, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read startup package: " + std::string(name));
    auto& data = patched[name];
    data.assign(std::istreambuf_iterator<char>(input), {});
    return data;
  };
  if (skip) PatchIntroPackage(read("InitialLoadingInterface.pkg"));
  auto& frontend = read("FrontEnd.pkg");
  PatchMainMenuReturnAnimation(frontend);
  if (hide) PatchMainMenuPackage(frontend);
  // Separate caches prevent a previous -hidelive launch from affecting a later
  // normal or -skipintro-only launch, and let both options compose.
  const auto folder = cache_root / (skip ? (hide ? "skipintro-hidelive" : "skipintro") :
                                         (hide ? "hidelive" : "frontend"));
  std::filesystem::create_directories(folder);
  for (const auto& item : std::filesystem::directory_iterator(source)) {
    if (!item.is_regular_file() || item.path().extension() != ".pkg") continue;
    const auto target = folder / item.path().filename();
    auto patch = patched.find(item.path().filename().string());
    if (patch != patched.end()) {
      // Unlink before writing: a previous cache may contain a hard link to the
      // original. Never modify game files through a cached hard link.
      std::filesystem::remove(target);
      std::ofstream output(target, std::ios::binary);
      const auto& data = patch->second;
      output.write(reinterpret_cast<const char*>(data.data()), data.size());
      output.close();
      if (!output) throw std::runtime_error("Cannot write cached startup package");
    } else {
      std::error_code error;
      if (std::filesystem::exists(target) && std::filesystem::equivalent(item.path(), target, error)) continue;
      std::filesystem::remove(target);
      std::filesystem::create_hard_link(item.path(), target, error);
      if (error) std::filesystem::copy_file(item.path(), target);
    }
  }
  constexpr auto mount = "\\Device\\SnupiStartup";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder, true);
  if (!device->Initialize()) throw std::runtime_error("Cannot mount startup packages");
  auto* vfs = runtime.file_system();
  vfs->RegisterDevice(std::move(device));
  vfs->RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\Packages\\UI", mount);
  for (const auto& [name, data] : patched) {
    // Validate the actual game OpenFile route, not just ResolvePath.
    rex::filesystem::File* file = nullptr;
    rex::filesystem::FileAction action;
    const auto status = vfs->OpenFile(nullptr, "D:\\Packages\\UI\\" + name,
        rex::filesystem::FileDisposition::kOpen,
        rex::filesystem::FileAccess::kGenericRead, false, true, &file, &action);
    if (status != 0 || !file) throw std::runtime_error("Cannot open redirected startup package");
    std::vector<uint8_t> actual(data.size());
    size_t bytes_read = 0;
    const auto read_status = file->ReadSync(actual, 0, &bytes_read);
    file->Destroy();
    if (read_status != 0 || bytes_read != data.size() || actual != data)
      throw std::runtime_error("Game file-open route returned unpatched startup data");
  }
  REXLOG_INFO("Main menu return animation fixed: Enlist Back uses native Load -> Default");
  if (skip) REXLOG_INFO("Skip intro enabled: startup package redirected; Press Start input remains manual");
  if (hide) REXLOG_INFO("Hide Live enabled: Xbox LIVE, Leaderboards and Download Content removed; initial selection is Enlist Now");
}

REX_EXTERN(__imp__sub_829085B0);
// FMVController::next movie: +172 is intro/idle/playlist phase, +20 is
// intro-key count, +28 its FString array, +128 the current decoder.
REX_HOOK_RAW(sub_829085B0) {
  const auto controller = ctx.r3.u32;
  if (REXCVAR_GET(skipintro) && !intro_complete && !intro_controller &&
      GuestWord(base, controller + 172) == 0 && GuestWord(base, controller + 20) == 1) {
    const auto keys = GuestWord(base, controller + 28);
    if (keys && GuestTextEquals(base, GuestWord(base, keys + 8), "FrontEnd_Intro")) {
      intro_controller = controller;
      REXLOG_INFO("Skip intro: FrontEnd_Intro starting with its native music cue");
    }
  }
  __imp__sub_829085B0(ctx, base);
  if (controller == intro_controller && !intro_complete &&
      (GuestWord(base, controller + 172) != 0 || GuestWord(base, controller + 128) == 0)) {
    intro_complete = true;
    if (GuestWord(base, controller + 172) != 0) {
      REXLOG_INFO("Skip intro: FrontEnd_Intro finished; releasing Press Start gate");
    } else {
      REXLOG_WARN("Skip intro: FrontEnd_Intro decoder could not open; releasing Press Start gate");
    }
  }
}

REX_EXTERN(sub_828EA9B8);
REX_EXTERN(__imp__sub_828EC0F8);
// GlobalVariable_Timer::update. Freeze only IntroLoading's tagged timer until
// the native decoder advances out of the intro phase. Keep the original
// timer-expired event, loading checks and title transition intact.
REX_HOOK_RAW(sub_828EC0F8) {
  const auto timer = ctx.r3.u32;
  if (REXCVAR_GET(skipintro) && !intro_complete) {
    if (!intro_timer) {
      auto value_ctx = ctx;
      sub_828EA9B8(value_ctx, base);
      if (value_ctx.f1.f64 == std::bit_cast<float>(kIntroGateTimerBits)) {
        intro_timer = timer;
        REXLOG_INFO("Skip intro: Press Start gate waiting for FrontEnd_Intro completion");
      }
    }
    if (timer == intro_timer) ctx.f1.f64 = 0.0;
  }
  __imp__sub_828EC0F8(ctx, base);
  if (timer == intro_timer && intro_complete && !intro_gate_released) {
    auto value_ctx = ctx;
    value_ctx.r3.u64 = timer;
    sub_828EA9B8(value_ctx, base);
    if (value_ctx.f1.f64 <= 0) {
      intro_gate_released = true;
      REXLOG_INFO("Skip intro: Press Start timer expired after intro completion");
    }
  }
}
}

REX_EXTERN(__imp__sub_821E0AE8);
REX_EXTERN(__imp__sub_828C5AB0);

// The renderer draws the first PNG inside splash initialization, before the
// engine's splash index can be advanced. Suppress that dedicated image draw
// too; its caller still clears/presents the frame and initializes the renderer.
REX_HOOK_RAW(sub_828C5AB0) {
  if (REXCVAR_GET(skipintro)) {
    static std::atomic<bool> logged = false;
    if (!logged.exchange(true)) {
      REXLOG_INFO("Skip intro: startup splash PNG draw suppressed");
    }
    return;
  }
  __imp__sub_828C5AB0(ctx, base);
}

REX_HOOK_RAW(sub_821E0AE8) {
  const uint32_t engine = ctx.r3.u32;
  __imp__sub_821E0AE8(ctx, base);
  if (REXCVAR_GET(skipintro) && ctx.r3.u32 != 0) {
    // Renderer initialization must still run. Mark its splash sequence complete
    // afterward: the update/query functions compare current +10120 to +10124.
    std::memcpy(base + engine + 10120, base + engine + 10124, 4);
    REXLOG_INFO("Skip intro: initial splash sequence complete");
  }
}

