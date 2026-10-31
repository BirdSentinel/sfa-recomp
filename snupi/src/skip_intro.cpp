#include "skip_intro.h"
#include "intro_package.h"

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
void PrepareSkipIntro(rex::Runtime& runtime, const std::filesystem::path& game_root,
                     const std::filesystem::path& cache_root) {
  if (!REXCVAR_GET(skipintro)) return;
  const auto source = game_root / "Packages/UI/InitialLoadingInterface.pkg";
  std::ifstream input(source, std::ios::binary);
  if (!input) throw std::runtime_error("-skipintro: cannot read " + source.string());
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  PatchIntroPackage(data);
  const auto folder = cache_root / "skipintro";
  std::filesystem::create_directories(folder);
  const auto destination = folder / "InitialLoadingInterface.pkg";
  // OpenFile resolves the parent then GetChild, so a file symlink is bypassed.
  // Redirect the UI directory, retaining all its other packages as hard links
  // (or copies when the cache resides on a different volume).
  for (const auto& item : std::filesystem::directory_iterator(source.parent_path())) {
    if (!item.is_regular_file() || item.path().extension() != ".pkg" ||
        item.path().filename() == source.filename()) continue;
    const auto target = folder / item.path().filename();
    std::error_code error;
    if (std::filesystem::exists(target) && std::filesystem::equivalent(item.path(), target, error)) continue;
    std::filesystem::remove(target);
    std::filesystem::create_hard_link(item.path(), target, error);
    if (error) std::filesystem::copy_file(item.path(), target);
  }
  std::ifstream cached(destination, std::ios::binary);
  const std::vector<uint8_t> existing((std::istreambuf_iterator<char>(cached)), {});
  cached.close();
  if (existing != data) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data.data()), data.size());
    output.close();
    if (!output) throw std::runtime_error("-skipintro: cannot write cached intro package");
  }

  constexpr auto mount = "\\Device\\SnupiSkipIntro";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder, true);
  if (!device->Initialize()) throw std::runtime_error("-skipintro: cannot mount intro package");
  auto* vfs = runtime.file_system();
  vfs->RegisterDevice(std::move(device));
  // Redirect the parent directory, including the game's actual OpenFile path.
  vfs->RegisterSymbolicLink(
      "\\Device\\Harddisk0\\Partition1\\Packages\\UI", mount);
  auto* parent = vfs->ResolvePath("game:\\Packages\\UI");
  const auto* entry = parent ? parent->GetChild("InitialLoadingInterface.pkg") : nullptr;
  if (!entry || entry->device()->mount_path() != mount) {
    throw std::runtime_error("-skipintro: intro package redirect failed");
  }
  // Regression check using OpenFile, not ResolvePath: the old file-level link
  // passed ResolvePath but was ignored by the game's actual file-open route.
  rex::filesystem::File* file = nullptr;
  rex::filesystem::FileAction action;
  const auto status = vfs->OpenFile(nullptr, "D:\\Packages\\UI\\InitialLoadingInterface.pkg",
      rex::filesystem::FileDisposition::kOpen,
      rex::filesystem::FileAccess::kGenericRead, false, true, &file, &action);
  if (status != 0 || !file) throw std::runtime_error("-skipintro: cannot open redirected package");
  std::vector<uint8_t> actual(data.size());
  size_t bytes_read = 0;
  const auto read_status = file->ReadSync(actual, 0, &bytes_read);
  file->Destroy();
  if (read_status != 0 || bytes_read != data.size() || actual != data) {
    throw std::runtime_error("-skipintro: game file-open route returned unpatched data");
  }
  REXLOG_INFO("Skip intro enabled: startup package redirected; Press Start input remains manual");
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

