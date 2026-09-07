#pragma once

#include "graphics/culling/OcclusionCullingSystem.hpp"

namespace elm {

class VisibilitySystem {
public:
    VisibilitySystem() = default;

    void Execute(const Scene& scene, const Matrix4x4& viewProjection, Vector<OccludeeInstance>& visibleObjects) {
        m_occlusion.ExecuteCulling(scene, viewProjection, visibleObjects);
    }

    [[nodiscard]] OcclusionCullingSystem& GetSoftwareOcclusionCuller() noexcept { return m_occlusion; }
    [[nodiscard]] const OcclusionCullingSystem& GetSoftwareOcclusionCuller() const noexcept { return m_occlusion; }

private:
    OcclusionCullingSystem m_occlusion;
};

} // namespace elm
