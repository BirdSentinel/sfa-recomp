#include <bit>
#include <cstdio>
#include <rex/graphics/register_file.h>
#include <rex/graphics/util/draw.h>

// Exercise the real viewport calculation with all previously cached register
// values unchanged. A depth-format-only transition must change RTV Z bounds.
int main() {
  using namespace rex::graphics;
  RegisterFile regs;
  reg::PA_CL_CLIP_CNTL clip{};
  clip.dx_clip_space_def = 1;
  regs[clip.register_index] = clip.value;
  reg::PA_CL_VTE_CNTL vte{};
  vte.vport_z_scale_ena = 1;
  vte.vport_z_offset_ena = 1;
  regs[vte.register_index] = vte.value;
  reg::RB_DEPTHCONTROL depth{};
  depth.z_enable = 1;
  regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE] = std::bit_cast<uint32_t>(1.0f);
  regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET] = std::bit_cast<uint32_t>(0.0f);
  auto check = [&](xenos::DepthRenderTargetFormat format, bool rtv, bool convert,
                   float expected_min, float expected_max) {
    reg::RB_DEPTH_INFO info{};
    info.depth_format = format;
    regs[info.register_index] = info.value;
    draw_util::ViewportInfo result{};
    draw_util::GetHostViewportInfo(regs, 1, 1, true, 32767, 32767, false,
                                  depth, convert, rtv, false, result);
    if (result.z_min != expected_min || result.z_max != expected_max) {
      std::fprintf(stderr, "Unexpected range: %g..%g (wanted %g..%g)\n",
                   result.z_min, result.z_max, expected_min, expected_max);
      return false;
    }
    return true;
  };
  using Format = xenos::DepthRenderTargetFormat;
  for (bool convert : {false, true}) {
    if (!check(Format::kD24S8, true, convert, 0.0f, 1.0f) ||
        !check(Format::kD24FS8, true, convert, 0.0f, 0.5f) ||
        !check(Format::kD24S8, true, convert, 0.0f, 1.0f) ||
        !check(Format::kD24FS8, false, convert, 0.0f, 1.0f)) return 1;
  }
  // Reversed-Z geometry must retain its narrower viewport range too.
  regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE] = std::bit_cast<uint32_t>(-0.5f);
  regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET] = std::bit_cast<uint32_t>(0.75f);
  if (!check(Format::kD24S8, true, false, 0.25f, 0.75f) ||
      !check(Format::kD24FS8, true, false, 0.125f, 0.375f)) return 1;
  std::puts("RTV depth-format transitions, reversed Z and ROV ranges passed.");
}
