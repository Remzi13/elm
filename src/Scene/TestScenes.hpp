#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/Scene.hpp"

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

class TestScenes {
public:
    static void BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene,
        graphics::ModelManager& models);

private:
    static void BuildWallAndGrid(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildRooms(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildPhysicsSandbox(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildBox(uint32_t count, Scene& scene, graphics::ModelManager& models);
};

} // namespace elm
