#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/ModelManager.hpp"

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

struct SceneAsset;

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
    /// Uploads the asset models into models and appends its nodes. Returns the index of the first appended node.
    size_t Append(SceneAsset&& asset, graphics::ModelManager& models);
    void Clear();
    void ReserveNodes(size_t capacity);
    
    [[nodiscard]] size_t NodeCount() const noexcept { return nodes.size(); }
    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }
    
    Vector<Node> nodes;
};

/// Scene as read from a file: models are CPU-side only and not owned by any ModelManager yet.
struct SceneAsset {
    static constexpr size_t NoModel = ~size_t{ 0 };

    /// Node models are left empty, nodeModels tells which model each node uses.
    Scene scene;
    Vector<graphics::Model> models;
    /// Per scene node: index into models or NoModel.
    Vector<size_t> nodeModels;
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

} // namespace Engine
