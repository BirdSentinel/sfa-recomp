#pragma once
#include <rex/ui/graphics_provider.h>

namespace snupi::renderer {
std::unique_ptr<rex::ui::Presenter> CreatePresenter(
    rex::ui::GraphicsProvider& provider,
    rex::ui::Presenter::HostGpuLossCallback loss_callback);
}
