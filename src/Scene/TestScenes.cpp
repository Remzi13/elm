#include "Scene/TestScenes.hpp"

#include "math/Rotation.hpp"

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

} // namespace

Scene::Node& Scene::AddNode(graphics::ModelHandler model, String name)
{
    Node node;
    node.model = std::move(model);
    node.name = std::move(name);
    return AppendNode(std::move(node));
}

Scene::Node& Scene::AppendNode(Node node)
{
    node.id = AllocateId(nodes.size());
    nodes.push_back(std::move(node));
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
        const auto parentInverse = Inverse(nodes[node.parentIndex].worldTransform);
        if (!parentInverse)
            return false;
        node.localTransform = *parentInverse * transform;
    }
    node.worldTransform = transform;

    // Children follow, together with the local changes made since the last update
    m_transformsDirty = true;
    UpdateWorldTransforms();
    return true;
}

bool Scene::SetLocalTransform(size_t nodeIndex, const Matrix4x4& transform)
{
    if (nodeIndex >= nodes.size())
        return false;
    nodes[nodeIndex].localTransform = transform;
    m_transformsDirty = true;
    return true;
}

void Scene::UpdateWorldTransforms()
{
    if (!m_transformsDirty)
        return;
    m_transformsDirty = false;

    enum : uint8_t { Unvisited, Visiting, Done };
    Vector<uint8_t> state(nodes.size(), Unvisited);
    Vector<size_t> chain;
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        // Walk up to a computed ancestor or a root, then compute the chain top down.
        // A parent cycle or an invalid parent index makes the node a root.
        chain.clear();
        for (size_t current = nodeIndex; current < nodes.size() && state[current] == Unvisited;
             current = nodes[current].parentIndex) {
            state[current] = Visiting;
            chain.push_back(current);
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto& node = nodes[*it];
            const size_t parent = node.parentIndex;
            node.worldTransform = parent < nodes.size() && state[parent] == Done
                ? nodes[parent].worldTransform * node.localTransform
                : node.localTransform;
            state[*it] = Done;
        }
    }
}

bool Scene::RemoveNode(size_t nodeIndex)
{
    if (nodeIndex >= nodes.size())
        return false;

    const Node& removed = nodes[nodeIndex];
    const Matrix4x4 removedLocal = removed.localTransform;
    const size_t removedParent = removed.parentIndex;
    const size_t newParent = removedParent != NoNode && removedParent > nodeIndex ? removedParent - 1 : removedParent;
    // A copy pushed into nodes directly may share the id of another node, that one keeps it
    if (IndexOf(removed.id) == nodeIndex) {
        for (const auto& store : m_componentStores) {
            if (store)
                store->Remove(removed.id);
        }
        FreeId(removed.id);
    }
    nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(nodeIndex));

    for (size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        if (node.parentIndex == nodeIndex) {
            // world = parentWorld * removedLocal * childLocal stays the same under the new parent
            node.parentIndex = newParent;
            node.localTransform = removedLocal * node.localTransform;
        } else if (node.parentIndex != NoNode && node.parentIndex > nodeIndex) {
            --node.parentIndex;
        }

        if (index >= nodeIndex && node.id) {
            const uint32_t slotIndex = node.id.Index() - 1;
            if (slotIndex < m_idSlots.size() && m_idSlots[slotIndex].generation == node.id.Generation()
                && m_idSlots[slotIndex].index == index + 1)
                m_idSlots[slotIndex].index = index;
        }
    }
    return true;
}

size_t Scene::Append(SceneAsset&& asset, graphics::ModelManager& models)
{
    Vector<graphics::ModelHandler> handlers;
    handlers.reserve(asset.models.size());
    for (auto& model : asset.models)
        handlers.push_back(models.Insert(std::move(model)));

    const size_t nodeOffset = nodes.size();
    const size_t nodeCount = asset.scene.nodes.size();
    nodes.reserve(nodes.size() + nodeCount);
    for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        auto& node = asset.scene.nodes[nodeIndex];
        if (node.parentIndex != NoNode)
            node.parentIndex += nodeOffset;
        const size_t modelIndex = nodeIndex < asset.nodeModels.size() ? asset.nodeModels[nodeIndex] : SceneAsset::NoModel;
        node.model = modelIndex < handlers.size() ? handlers[modelIndex] : graphics::ModelHandler {};
        AppendNode(std::move(node));
    }

    // Entity props refer to nodes of the asset: they get the ids those nodes have now
    auto idOf = [&](NodeId reference) {
        const size_t nodeIndex = SceneAsset::NodeIndexOf(reference);
        return nodeIndex < nodeCount ? nodes[nodeOffset + nodeIndex].id : NodeId {};
    };
    for (size_t nodeIndex = 0; nodeIndex < nodeCount && nodeIndex < asset.nodeComponents.size(); ++nodeIndex) {
        auto& components = asset.nodeComponents[nodeIndex];
        const NodeId id = nodes[nodeOffset + nodeIndex].id;
        if (components.script) {
            for (auto& prop : components.script->props) {
                if (auto* reference = std::get_if<NodeId>(&prop.second))
                    *reference = idOf(*reference);
            }
            Components<ScriptComponent>().Emplace(id, std::move(*components.script));
        }
        if (components.rigidBody)
            Components<RigidBody>().Emplace(id, *components.rigidBody);
        if (components.collider)
            Components<Collider>().Emplace(id, *components.collider);
    }
    return nodeOffset;
}

void Scene::Clear()
{
    for (const auto& node : nodes)
        FreeId(node.id);
    nodes.clear();
    for (const auto& store : m_componentStores) {
        if (store)
            store->Clear();
    }
    m_transformsDirty = false;
}

void Scene::ReserveNodes(size_t capacity)
{
    nodes.reserve(capacity);
}

void Scene::CopyComponents(NodeId from, NodeId to)
{
    for (const auto& store : m_componentStores) {
        if (store)
            store->Copy(from, to);
    }
}

Scene Scene::Clone() const
{
    Scene copy;
    copy.nodes = nodes;
    copy.m_idSlots = m_idSlots;
    copy.m_freeIdSlots = m_freeIdSlots;
    copy.m_transformsDirty = m_transformsDirty;
    copy.m_componentStores.resize(m_componentStores.size());
    for (size_t type = 0; type < m_componentStores.size(); ++type) {
        if (m_componentStores[type])
            copy.m_componentStores[type] = m_componentStores[type]->Clone();
    }
    return copy;
}

void Scene::RestoreFrom(Scene&& snapshot)
{
    // Slots of the snapshot's nodes come back as they were. Every other slot may have handed out
    // ids since: it stays free with the generation it reached, one more when it is in use now.
    for (size_t slotIndex = 0; slotIndex < m_idSlots.size(); ++slotIndex) {
        const bool snapshotUses = slotIndex < snapshot.m_idSlots.size() && snapshot.m_idSlots[slotIndex].index != NoNode;
        if (snapshotUses)
            continue;
        const IdSlot& current = m_idSlots[slotIndex];
        IdSlot restored;
        restored.generation = current.generation;
        if (current.index != NoNode)
            ++restored.generation; // wraps to 0: retired, see FreeId
        if (slotIndex < snapshot.m_idSlots.size())
            snapshot.m_idSlots[slotIndex] = restored;
        else
            snapshot.m_idSlots.push_back(restored);
    }
    // A free slot at generation 0 has wrapped: it is retired and never handed out again
    snapshot.m_freeIdSlots.clear();
    for (size_t slotIndex = 0; slotIndex < snapshot.m_idSlots.size(); ++slotIndex) {
        const IdSlot& slot = snapshot.m_idSlots[slotIndex];
        if (slot.index == NoNode && slot.generation != 0)
            snapshot.m_freeIdSlots.push_back(static_cast<uint32_t>(slotIndex));
    }

    nodes = std::move(snapshot.nodes);
    m_idSlots = std::move(snapshot.m_idSlots);
    m_freeIdSlots = std::move(snapshot.m_freeIdSlots);
    m_componentStores = std::move(snapshot.m_componentStores);
    m_transformsDirty = true;
}

NodeId Scene::IdOf(size_t nodeIndex)
{
    if (nodeIndex >= nodes.size())
        return { };
    auto& node = nodes[nodeIndex];
    if (!node.id || IndexOf(node.id) != nodeIndex)
        node.id = AllocateId(nodeIndex);
    return node.id;
}

size_t Scene::IndexOf(NodeId id) const
{
    if (!id)
        return NoNode;
    const uint32_t slotIndex = id.Index() - 1;
    if (slotIndex >= m_idSlots.size())
        return NoNode;
    const auto& slot = m_idSlots[slotIndex];
    if (slot.generation != id.Generation() || slot.index == NoNode)
        return NoNode;
    if (slot.index < nodes.size() && nodes[slot.index].id == id)
        return slot.index;

    // nodes was edited directly: find the node again and remember where it went
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        if (nodes[nodeIndex].id == id) {
            slot.index = nodeIndex;
            return nodeIndex;
        }
    }
    slot.index = NoNode;
    return NoNode;
}

Scene::Node* Scene::Find(NodeId id)
{
    const size_t nodeIndex = IndexOf(id);
    return nodeIndex == NoNode ? nullptr : &nodes[nodeIndex];
}

const Scene::Node* Scene::Find(NodeId id) const
{
    const size_t nodeIndex = IndexOf(id);
    return nodeIndex == NoNode ? nullptr : &nodes[nodeIndex];
}

NodeId Scene::AllocateId(size_t nodeIndex)
{
    uint32_t slotIndex;
    if (!m_freeIdSlots.empty()) {
        slotIndex = m_freeIdSlots.back();
        m_freeIdSlots.pop_back();
    } else {
        slotIndex = static_cast<uint32_t>(m_idSlots.size());
        m_idSlots.emplace_back();
    }
    m_idSlots[slotIndex].index = nodeIndex;
    return NodeId(slotIndex + 1, m_idSlots[slotIndex].generation);
}

void Scene::FreeId(NodeId id)
{
    if (!id)
        return;
    const uint32_t slotIndex = id.Index() - 1;
    if (slotIndex >= m_idSlots.size() || m_idSlots[slotIndex].generation != id.Generation())
        return;
    auto& slot = m_idSlots[slotIndex];
    slot.index = NoNode;
    // A slot whose generation would wrap is retired, so an old id never resolves to a new node
    if (++slot.generation != 0)
        m_freeIdSlots.push_back(slotIndex);
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
