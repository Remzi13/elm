#pragma once

#include "editor/ui/IImGuiWindow.hpp"

#include <memory>
#include <span>

namespace elm  {

class ProjectMenu;
class SceneMenu;
class EditorToolbar;

class DockSpaceView {
public:
    void Render(std::span<const UniquePtr<IImGuiWindow>> windows, ProjectMenu& projectMenu, SceneMenu& sceneMenu, EditorToolbar& toolbar);
};

} // namespace Engine
