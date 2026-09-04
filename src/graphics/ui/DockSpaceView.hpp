#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include <memory>
#include <span>

namespace elm  {

class DockSpaceView {
public:
    void Render(std::span<const UniquePtr<IImGuiWindow>> windows);
};

} // namespace Engine
