#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include <memory>
#include <span>

namespace elm  {

class DockSpaceView {
public:
    void Update(std::span<const UniquePtr<IImGuiWindow>> windows);
};

} // namespace Engine
