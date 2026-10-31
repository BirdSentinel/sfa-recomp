// Build the presenter inside our plugin under a distinct class name. This
// preserves the SDK runtime ABI while letting the game control DXGI Present.
#define D3D12Presenter SnupiD3D12Presenter
#include "src/ui/d3d12/d3d12_presenter.cpp"
#undef D3D12Presenter
#include "snupi_presenter.h"

namespace snupi::renderer {
std::unique_ptr<rex::ui::Presenter> CreatePresenter(
    rex::ui::GraphicsProvider& provider,
    rex::ui::Presenter::HostGpuLossCallback loss_callback) {
  return rex::ui::d3d12::SnupiD3D12Presenter::Create(
      std::move(loss_callback), static_cast<rex::ui::d3d12::D3D12Provider&>(provider));
}
}
