#pragma once

#include "Scene/TestScenes.hpp"

namespace elm {

class RenderWorld {
public:
    RenderWorld() = default;

    void SetScene(const Scene* scene) noexcept { m_scene = scene; }
    [[nodiscard]] const Scene* GetScene() const noexcept { return m_scene; }

private:
    const Scene* m_scene{ nullptr };
};

} // namespace elm
