#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"
#include "math/Primitivs.hpp"

#include "render/api/RenderResources.hpp"

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

struct Scene {
    struct Instance {
        render::MeshDataPtr meshData;
        render::MeshHandle renderMesh;
        AABB localBounds;
        Matrix4x4 worldTransform;
        Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
        bool visible { true };
        String name;
    };

    /// Creates the GPU mesh once; instances refer to it by handle, so equal geometry is drawn instanced.
    [[nodiscard]] Instance MakeInstance(render::RenderResources& resources, const MeshData& data);
    [[nodiscard]] Instance MakeInstance(const render::Mesh& mesh) const;
    void Clear();

    ScenePreset preset { ScenePreset::Box };
    Vector<Instance> instances;
    /// Owns the GPU meshes the instances refer to
    Vector<render::Mesh> meshes;
};

class TestScenes {
public:
    static void BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene, render::RenderResources& resources);

private:
    static void BuildWallAndGrid(uint32_t count, Scene& scene, render::RenderResources& resources);
    static void BuildRooms(uint32_t count, Scene& scene, render::RenderResources& resources);
    static void BuildPhysicsSandbox(uint32_t count, Scene& scene, render::RenderResources& resources);
    static void BuildBox(uint32_t count, Scene& scene, render::RenderResources& resources);
};

} // namespace Engine
