#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include <memory>
#include <span>

namespace Engine {

class DockSpaceView {
public:
    void Render(std::span<const std::unique_ptr<IImGuiWindow>> windows);
};

} // namespace Engine
