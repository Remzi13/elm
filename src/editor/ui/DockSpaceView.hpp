#pragma once

#include "editor/ui/IImGuiWindow.hpp"

#include <memory>
#include <span>

namespace elm  {

class ProjectMenu;

class DockSpaceView {
public:
    void Render(std::span<const UniquePtr<IImGuiWindow>> windows, ProjectMenu& projectMenu);
};

} // namespace Engine
