#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "render/api/RenderResources.hpp"

#include "graphics/ModelManager.hpp"

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

class Scene {
public:
    static constexpr size_t NoNode = ~size_t{ 0 };

    struct Node {
        size_t parentIndex { NoNode };
        Matrix4x4 localTransform;
        Matrix4x4 worldTransform;
        String name;
        graphics::ModelHandler model;
        Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
        bool visible { true };
    };

    [[nodiscard]] Node& AddNode(graphics::ModelHandler model, String name = { });
    [[nodiscard]] Node& AppendNode(Node node);
    [[nodiscard]] bool SetWorldTransform(size_t nodeIndex, const Matrix4x4& transform);
    [[nodiscard]] bool RemoveNode(size_t nodeIndex);
    void Append(Scene&& other);
    void Clear();
    void ReserveNodes(size_t capacity);
    
    [[nodiscard]] size_t NodeCount() const noexcept { return nodes.size(); }
    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }
    
    Vector<Node> nodes;
};

class TestScenes {
public:
    static void BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene,
        graphics::ModelManager& models, render::RenderResources& resources);

private:
    static void BuildWallAndGrid(uint32_t count, Scene& scene, graphics::ModelManager& models, render::RenderResources& resources);
    static void BuildRooms(uint32_t count, Scene& scene, graphics::ModelManager& models, render::RenderResources& resources);
    static void BuildPhysicsSandbox(uint32_t count, Scene& scene, graphics::ModelManager& models, render::RenderResources& resources);
    static void BuildBox(uint32_t count, Scene& scene, graphics::ModelManager& models, render::RenderResources& resources);
};

} // namespace Engine
