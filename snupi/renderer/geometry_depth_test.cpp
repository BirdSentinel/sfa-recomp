#include <cstdio>
#include "rtv_geometry_depth.h"

int main() {
  using namespace rex::graphics;
  reg::RB_DEPTHCONTROL prepass{};
  prepass.z_enable = 1;
  prepass.z_write_enable = 1;
  prepass.zfunc = xenos::CompareFunction::kGreaterEqual;
  auto material = prepass;
  material.z_write_enable = 0;
  material.stencil_enable = 1;
  auto policy = [](reg::RB_DEPTHCONTROL depth, bool writes_depth = false,
                   uint32_t colors = 1,
                   xenos::DepthRenderTargetFormat format = xenos::DepthRenderTargetFormat::kD24FS8,
                   xenos::MsaaSamples samples = xenos::MsaaSamples::k1X,
                   bool alpha_to_coverage = false, bool polygonal = true) {
    return snupi_renderer::ShouldRoundGeometryDepth(writes_depth, colors, depth,
                                                    format, samples, alpha_to_coverage, polygonal);
  };
  if (!policy(prepass) || !policy(material)) {
    std::fputs("Prepass and stencil-enabled material pass must both round depth.\n", stderr);
    return 1;
  }
  // Captured at (611,555), terrain-bias_capture.rdc, main-scene depth.
  // The former policy rounded the prepass upwards but compared unrounded
  // material depth against it, rejecting an otherwise identical surface.
  const float host_depth = 0.000548002659343183f;
  auto round_host = [](float z) {
    return xenos::Float20e4To32(xenos::Float32To20e4(z * 2.0f, true)) * 0.5f;
  };
  const float stored = policy(prepass) ? round_host(host_depth) : host_depth;
  const float compared = policy(material) ? round_host(host_depth) : host_depth;
  if (!(host_depth < stored) || compared != stored || !(compared >= stored)) {
    std::fputs("Captured depth comparison regression failed.\n", stderr);
    return 1;
  }
  // Shadow depth-only draws, explicit depth, other depth formats, and MSAA
  // must retain the original pipeline behavior.
  if (policy(prepass, false, 0) || policy(prepass, true) ||
      policy(prepass, false, 1, xenos::DepthRenderTargetFormat::kD24S8) ||
      policy(prepass, false, 1, xenos::DepthRenderTargetFormat::kD24FS8, xenos::MsaaSamples::k2X) ||
      policy(prepass, false, 1, xenos::DepthRenderTargetFormat::kD24FS8, xenos::MsaaSamples::k1X, true) ||
      policy(prepass, false, 1, xenos::DepthRenderTargetFormat::kD24FS8, xenos::MsaaSamples::k1X, false, false)) return 1;
  prepass.z_enable = 0;
  if (policy(prepass)) return 1;
  std::puts("Matched prepass/material precision and captured depth comparison passed.");
}
