#pragma once

#include <rex/graphics/registers.h>

namespace snupi_renderer {

inline bool ShouldRoundGeometryDepth(
    bool writes_depth, uint32_t color_targets,
    rex::graphics::reg::RB_DEPTHCONTROL depth,
    rex::graphics::xenos::DepthRenderTargetFormat format,
    rex::graphics::xenos::MsaaSamples samples, bool alpha_to_coverage,
    bool polygonal) {
  using namespace rex::graphics;
  // The normal/depth prepass has stencil disabled, while the matching material
  // pass enables stencil. Both must use the same depth representation. Stencil
  // state must not select whether depth is rounded.
  return !writes_depth && color_targets && depth.z_enable &&
         depth.zfunc == xenos::CompareFunction::kGreaterEqual &&
         format == xenos::DepthRenderTargetFormat::kD24FS8 &&
         samples == xenos::MsaaSamples::k1X && !alpha_to_coverage && polygonal;
}

}  // namespace snupi_renderer
