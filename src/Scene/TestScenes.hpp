#pragma once

#include "graphics/culling/OcclusionCullingSystem.hpp"
#include <string>

namespace elm {

enum class ScenePreset {
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

class TestScenes {
public:
    static void BuildScene(ScenePreset preset,
                           uint32_t targetInstanceCount,
                           std::vector<OccluderInstance>& outOccluders,
                           std::vector<OccludeeInstance>& outOccludees);

private:
    static void BuildWallAndGrid(uint32_t count,
                                 std::vector<OccluderInstance>& outOccluders,
                                 std::vector<OccludeeInstance>& outOccludees);

    static void BuildRooms(uint32_t count,
                           std::vector<OccluderInstance>& outOccluders,
                           std::vector<OccludeeInstance>& outOccludees);

    static void BuildPhysicsSandbox(uint32_t count,
                                    std::vector<OccluderInstance>& outOccluders,
                                    std::vector<OccludeeInstance>& outOccludees);
};

} // namespace Engine
