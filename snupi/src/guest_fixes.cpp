#include <rex/hook.h>

REX_EXTERN(__imp__sub_829B1688);

// The constructor at 0x829B1688 initializes the queues and pending flag but
// leaves its XAM_OVERLAPPED at +0xC4 uninitialized. The polling function at
// 0x829B1A98 clears these seven words only AFTER the first completion.
// Initialize once, before any request: this object polls the result and does
// not install an event or completion callback. Keep later async state intact.
REX_HOOK_RAW(sub_829B1688) {
  const uint32_t object = ctx.r3.u32;
  __imp__sub_829B1688(ctx, base);
  std::memset(base + object + 0xC4, 0, 0x1C);
}

// SDK 0.10.0 InputSystem::RefreshDevices mutates shared device/owner vectors
// without synchronization. This game polls and sends rumble from different
// guest threads. Serialize all three guest XInput wrappers, including the
// capabilities query made inside the vibration wrapper. These cover every
// XamInput call emitted for this image; leave the original behavior intact.
#include <mutex>

namespace {
std::mutex input_mutex;
}

REX_EXTERN(__imp__sub_8239F470);
REX_EXTERN(__imp__sub_8239F478);
REX_EXTERN(__imp__sub_8239F488);

REX_HOOK_RAW(sub_8239F470) {
  const std::lock_guard lock(input_mutex);
  __imp__sub_8239F470(ctx, base);
}

REX_HOOK_RAW(sub_8239F478) {
  const std::lock_guard lock(input_mutex);
  __imp__sub_8239F478(ctx, base);
}

REX_HOOK_RAW(sub_8239F488) {
  const std::lock_guard lock(input_mutex);
  __imp__sub_8239F488(ctx, base);
}

#include <bit>
#include <rex/cvar.h>

REX_EXTERN(__imp__sub_828AB920);

namespace {
uint32_t ReadGuestWord(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}
}

// The half-resolution transparency pass initializes RGB to zero and alpha
// to one using SRC_ALPHA / INV_SRC_ALPHA blending. RTV emulates Xenos
// fixed16 (-32..32) with SNORM (-1..1), scaling all four outputs by 1/32.
// Consequently that initialization blends with alpha 1/32, retaining old
// EDRAM depth bits as color. Those values later flood the HDR composite.
// Use float16 for this pass's color surface: the same 64bpp footprint and
// existing float16 resolve destination, with native, unscaled alpha blending.
// RenderDoc pixel history and a live surface-format test verified this path.
REX_HOOK_RAW(sub_828AB920) {
  static const bool use_float_hdr =
      rex::cvar::GetFlagByName("render_target_path_d3d12") == "rtv";
  if (use_float_hdr) {
    // Renderer +608 -> half-resolution target; target +168 -> D3D surface.
    const uint32_t target = ReadGuestWord(base, ctx.r3.u32 + 608);
    if (target) {
      const uint32_t surface = ReadGuestWord(base, target + 168);
      if (surface) {
        const uint32_t color_info = ReadGuestWord(base, surface + 28);
        if (((color_info >> 16) & 0xF) == 5) {
          // RB_COLOR_INFO: k_16_16_16_16 (5) -> k_16_16_16_16_FLOAT (7).
          const uint32_t replacement =
              std::byteswap((color_info & ~0x000F0000u) | 0x00070000u);
          std::memcpy(base + surface + 28, &replacement, sizeof(replacement));
        }
      }
    }
  }
  __imp__sub_828AB920(ctx, base);
}
// The scene wrapper discards its elapsed-time argument and calls PhysX simulate
// with 1/30 second once per rendered frame (call site 0x8281D740). Preserve the
// game-provided, pause/time-scale-aware delta across that wrapper's other calls.
// Keep the original 1/60-second maximum substep and two-substep configuration.
REX_EXTERN(__imp__sub_8281D6C8);
REX_EXTERN(__imp__sub_826412B0);

namespace {
thread_local const double* physics_elapsed_time = nullptr;
struct PhysicsElapsedScope {
  const double* previous = physics_elapsed_time;
  explicit PhysicsElapsedScope(const double& elapsed) {
    physics_elapsed_time = &elapsed;
  }
  ~PhysicsElapsedScope() { physics_elapsed_time = previous; }
};
}

REX_HOOK_RAW(sub_8281D6C8) {
  const double elapsed = ctx.f1.f64;
  const PhysicsElapsedScope scope(elapsed);
  __imp__sub_8281D6C8(ctx, base);
}

REX_HOOK_RAW(sub_826412B0) {
  if (physics_elapsed_time && ctx.lr == 0x8281D744) {
    ctx.f1.f64 = *physics_elapsed_time;
  }
  __imp__sub_826412B0(ctx, base);
}

REX_EXTERN(__imp__sub_8230D678);

// Splash-screen DrawPrimitiveUP: sixteen rectangle-list strips, with guest
// vertex depths (1, 0, 0). Rectangle expansion extrapolates the fourth depth
// to -1, and host depth clipping cuts away part of each strip even with the
// depth test disabled. Keep this screen-space image at a uniform valid depth.
// Restrict the correction to this call site and its exact vertex layout.
REX_HOOK_RAW(sub_8230D678) {
  if (ctx.lr == 0x828C5FE0 && ctx.r4.u32 == 8 && ctx.r5.u32 == 16 &&
      ctx.r7.u32 == 36) {
    for (uint32_t vertex = 0; vertex < 48; ++vertex) {
      const uint32_t depth = 0;
      std::memcpy(base + ctx.r6.u32 + vertex * 36 + 8, &depth, sizeof(depth));
    }
  }
  __imp__sub_8230D678(ctx, base);
}
