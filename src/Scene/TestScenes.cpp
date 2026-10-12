#include "Scene/TestScenes.hpp"

#include <cmath>
#include <limits>

namespace elm {

namespace {

    auto AddMesh(graphics::ModelManager& models, const MeshData& data, String name = {})
        -> graphics::ModelHandler
    {     
        graphics::Model asset;
        asset.name = name;        
        asset.AddSubmesh(data);

        return models.Insert(std::move(asset));
    }

    auto AddNode(Scene& scene, const graphics::ModelHandler& model, const Matrix4x4& transform,
        const Vector4& color = { 0.8f, 0.8f, 0.8f, 1.0f }) -> Scene::Node&
    {
        auto& node = scene.AddNode(model);
        node.localTransform = transform;
        node.worldTransform = transform;
        node.color = color;
        return node;
    }

    auto MakeWallTransform(float width, float height, float thickness, const Vector3& position) -> Matrix4x4
    {
        return Matrix4x4::Scaling(Vector3 { width, height, thickness }) * Matrix4x4::Translation(position);
    }

    auto TryInverse(const Matrix4x4& matrix, Matrix4x4& inverse) -> bool
    {
        float augmented[4][8] {};
        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column)
                augmented[row][column] = matrix(row, column);
            augmented[row][row + 4] = 1.0f;
        }

        for (size_t column = 0; column < 4; ++column) {
            size_t pivot = column;
            for (size_t row = column + 1; row < 4; ++row) {
                if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
                    pivot = row;
            }
            if (std::abs(augmented[pivot][column]) <= std::numeric_limits<float>::epsilon())
                return false;

            if (pivot != column) {
                for (size_t entry = 0; entry < 8; ++entry)
                    std::swap(augmented[pivot][entry], augmented[column][entry]);
            }
            const float divisor = augmented[column][column];
            for (float& entry : augmented[column])
                entry /= divisor;

            for (size_t row = 0; row < 4; ++row) {
                if (row == column)
                    continue;
                const float factor = augmented[row][column];
                for (size_t entry = 0; entry < 8; ++entry)
                    augmented[row][entry] -= factor * augmented[column][entry];
            }
        }

        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column)
                inverse(row, column) = augmented[row][column + 4];
        }
        return true;
    }

} // namespace

Scene::Node& Scene::AddNode(graphics::ModelHandler model, String name)
{
    nodes.push_back(Node{});
    nodes.back().model = std::move(model);
    nodes.back().name = std::move(name);
    return nodes.back();
}

bool Scene::SetWorldTransform(size_t nodeIndex, const Matrix4x4& transform)
{
    if (nodeIndex >= nodes.size())
        return false;

    auto& node = nodes[nodeIndex];
    if (node.parentIndex == NoNode) {
        node.localTransform = transform;
    } else {
        if (node.parentIndex >= nodes.size())
            return false;
        Matrix4x4 parentInverse;
        if (!TryInverse(nodes[node.parentIndex].worldTransform, parentInverse))
            return false;
        node.localTransform = parentInverse * transform;
    }
    node.worldTransform = transform;

    auto updateChildren = [&](auto&& self, size_t parentIndex) -> void {
        for (size_t childIndex = 0; childIndex < nodes.size(); ++childIndex) {
            auto& child = nodes[childIndex];
            if (child.parentIndex != parentIndex)
                continue;
            child.worldTransform = nodes[parentIndex].worldTransform * child.localTransform;
            self(self, childIndex);
        }
    };
    updateChildren(updateChildren, nodeIndex);
    return true;
}

size_t Scene::Append(SceneAsset&& asset, graphics::ModelManager& models)
{
    Vector<graphics::ModelHandler> handlers;
    handlers.reserve(asset.models.size());
    for (auto& model : asset.models)
        handlers.push_back(models.Insert(std::move(model)));

    const size_t nodeOffset = nodes.size();
    nodes.reserve(nodes.size() + asset.scene.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < asset.scene.nodes.size(); ++nodeIndex) {
        auto& node = asset.scene.nodes[nodeIndex];
        if (node.parentIndex != NoNode)
            node.parentIndex += nodeOffset;
        const size_t modelIndex = nodeIndex < asset.nodeModels.size() ? asset.nodeModels[nodeIndex] : SceneAsset::NoModel;
        node.model = modelIndex < handlers.size() ? handlers[modelIndex] : graphics::ModelHandler {};
        nodes.push_back(std::move(node));
    }
    return nodeOffset;
}

void Scene::Clear()
{
    nodes.clear();
}

void TestScenes::BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene,
    graphics::ModelManager& models)
{
    scene.Clear();
    models.Clear();
    scene.nodes.reserve(targetInstanceCount + 16);

    switch (preset) {
    case ScenePreset::Box:
        BuildBox(targetInstanceCount, scene, models);
        break;
    case ScenePreset::WallAndGrid:
        BuildWallAndGrid(targetInstanceCount, scene, models);
        break;
    case ScenePreset::RoomsAndCorridors:
        BuildRooms(targetInstanceCount, scene, models);
        break;
    case ScenePreset::PhysicsSandbox:
        BuildPhysicsSandbox(targetInstanceCount, scene, models);
        break;
    }
}

void TestScenes::BuildBox([[maybe_unused]] uint32_t count, Scene& scene, graphics::ModelManager& models)
{
    const auto cubeModel = AddMesh(models, GeometryPrimitives::CreateCube(1.0f), "Cube");
    AddNode(scene, cubeModel, Matrix4x4::Translation(Vector3 { 0.0f, 0.5f, 0.0f }),
        Vector4 { 0.8f, 0.2f, 0.2f, 1.0f });
}

void TestScenes::BuildWallAndGrid(uint32_t count, Scene& scene, graphics::ModelManager& models)
{
    const auto wallModel = AddMesh(models, GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f), "Wall");
    AddNode(scene, wallModel, MakeWallTransform(18.0f, 9.0f, 1.5f, Vector3 { -10.5f, 4.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(18.0f, 9.0f, 1.5f, Vector3 { 10.5f, 4.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(3.0f, 3.0f, 1.5f, Vector3 { 0.0f, 7.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(12.0f, 8.0f, 1.0f, Vector3 { 0.0f, 4.0f, 18.0f }));

    const uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count) * 1.5f)));
    const uint32_t rows = cols == 0 ? 0 : static_cast<uint32_t>(std::ceil(static_cast<float>(count) / static_cast<float>(cols)));
    const float stepX = 40.0f / static_cast<float>(cols > 1 ? cols - 1 : 1);
    const float stepZ = 40.0f / static_cast<float>(rows > 1 ? rows - 1 : 1);
    const auto cubeModel = AddMesh(models, GeometryPrimitives::CreateCube(0.8f), "Grid Cube");

    uint32_t id = 0;
    for (uint32_t row = 0; row < rows && id < count; ++row) {
        for (uint32_t column = 0; column < cols && id < count; ++column, ++id) {
            const float x = -20.0f + static_cast<float>(column) * stepX;
            const float z = 4.0f + static_cast<float>(row) * stepZ;
            const float y = 0.5f + static_cast<float>(id % 4) * 0.9f;
            const float tX = static_cast<float>(column) / static_cast<float>(cols);
            const float tZ = static_cast<float>(row) / static_cast<float>(rows);
            AddNode(scene, cubeModel, Matrix4x4::Translation(Vector3 { x, y, z }),
                Vector4 { 0.2f + 0.7f * tX, 0.4f + 0.5f * (1.0f - tZ), 0.3f + 0.6f * tZ, 1.0f });
        }
    }
}

void TestScenes::BuildRooms(uint32_t count, Scene& scene, graphics::ModelManager& models)
{
    const auto wallModel = AddMesh(models, GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f), "Wall");
    const float roomSize = 16.0f;
    const float wallH = 6.0f;
    const float wallT = 1.0f;

    AddNode(scene, wallModel, MakeWallTransform(14.0f, wallH, wallT, Vector3 { -8.5f, wallH * 0.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(14.0f, wallH, wallT, Vector3 { 8.5f, wallH * 0.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(wallT, wallH, 14.0f, Vector3 { 0.0f, wallH * 0.5f, -8.5f }));
    AddNode(scene, wallModel, MakeWallTransform(wallT, wallH, 14.0f, Vector3 { 0.0f, wallH * 0.5f, 8.5f }));
    AddNode(scene, wallModel, MakeWallTransform(roomSize * 2.0f, wallH, wallT, Vector3 { 0.0f, wallH * 0.5f, roomSize }));
    AddNode(scene, wallModel, MakeWallTransform(roomSize * 2.0f, wallH, wallT, Vector3 { 0.0f, wallH * 0.5f, -roomSize }));
    AddNode(scene, wallModel, MakeWallTransform(wallT, wallH, roomSize * 2.0f, Vector3 { roomSize, wallH * 0.5f, 0.0f }));
    AddNode(scene, wallModel, MakeWallTransform(wallT, wallH, roomSize * 2.0f, Vector3 { -roomSize, wallH * 0.5f, 0.0f }));

    const auto cubeModel = AddMesh(models, GeometryPrimitives::CreateCube(0.7f), "Cube");
    for (uint32_t index = 0; index < count; ++index) {
        const int room = static_cast<int>(index % 4);
        const float roomX = (room % 2 == 0 ? -1.0f : 1.0f) * 8.0f;
        const float roomZ = (room / 2 == 0 ? -1.0f : 1.0f) * 8.0f;
        const float angle = static_cast<float>(index) * 0.4f;
        const float radius = 1.5f + static_cast<float>(index % 8) * 0.5f;
        const float x = roomX + std::cos(angle) * radius;
        const float z = roomZ + std::sin(angle) * radius;
        const float y = 0.5f + static_cast<float>(index % 3) * 0.8f;

        Vector4 color;
        if (room == 0)
            color = Vector4 { 0.9f, 0.3f, 0.3f, 1.0f };
        else if (room == 1)
            color = Vector4 { 0.3f, 0.9f, 0.3f, 1.0f };
        else if (room == 2)
            color = Vector4 { 0.3f, 0.3f, 0.9f, 1.0f };
        else
            color = Vector4 { 0.9f, 0.8f, 0.2f, 1.0f };
        AddNode(scene, cubeModel, Matrix4x4::Translation(Vector3 { x, y, z }), color);
    }
}

void TestScenes::BuildPhysicsSandbox(uint32_t count, Scene& scene, graphics::ModelManager& models)
{
    const auto wallModel = AddMesh(models, GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f), "Barrier");
    AddNode(scene, wallModel, MakeWallTransform(16.0f, 6.0f, 1.5f, Vector3 { 0.0f, 3.0f, 0.0f }));

    const auto cubeModel = AddMesh(models, GeometryPrimitives::CreateCube(0.9f), "Cube");
    for (uint32_t index = 0; index < count; ++index) {
        const bool behindWall = (index % 3) != 0;
        const float z = behindWall
            ? (4.0f + static_cast<float>(index % 10) * 1.5f)
            : (-5.0f - static_cast<float>(index % 5) * 1.5f);
        const float x = -6.0f + static_cast<float>(index % 12);
        const float y = 0.5f + static_cast<float>(index / 12);
        const Vector4 color = behindWall
            ? Vector4 { 0.2f, 0.7f, 0.9f, 1.0f }
            : Vector4 { 0.9f, 0.6f, 0.2f, 1.0f };
        AddNode(scene, cubeModel, Matrix4x4::Translation(Vector3 { x, y, z }), color);
    }
}

} // namespace elm
