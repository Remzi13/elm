#include "editor/NodeEditing.hpp"

#include "core/Unexpected.hpp"

#include "graphics/Camera.hpp"

#include "math/Matrix.hpp"
#include "math/Primitivs.hpp"

#include "model/Model.hpp"
#include "model/ModelCatalog.hpp"

#include "resource/AssetImport.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace elm {

    namespace {

        constexpr math::Vector4 DefaultModelColor { 0.8f, 0.8f, 0.8f, 1.0f };
        /// Bounds of a node without a model when the camera frames it.
        constexpr float NodeWithoutModelExtent = 0.5f;

        /// How far along the ray it enters the bounds; nothing when it misses them or starts inside.
        std::optional<float> EnterDistance(const ViewRay& ray, const AABB& bounds)
        {
            const float origin[3] { ray.origin.x, ray.origin.y, ray.origin.z };
            const float direction[3] { ray.direction.x, ray.direction.y, ray.direction.z };
            const float low[3] { bounds.minBounds.x, bounds.minBounds.y, bounds.minBounds.z };
            const float high[3] { bounds.maxBounds.x, bounds.maxBounds.y, bounds.maxBounds.z };
            float enter = 0.0f;
            float leave = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis) {
                // Along the slab: the ray is in it all the way or never
                if (direction[axis] == 0.0f) {
                    if (origin[axis] < low[axis] || origin[axis] > high[axis])
                        return std::nullopt;
                    continue;
                }
                const float first = (low[axis] - origin[axis]) / direction[axis];
                const float second = (high[axis] - origin[axis]) / direction[axis];
                enter = (std::max)(enter, (std::min)(first, second));
                leave = (std::min)(leave, (std::max)(first, second));
            }
            if (enter > leave || enter <= 0.0f)
                return std::nullopt;
            return enter;
        }

        /// The rectangle of the view the part of the bounds in front of the camera covers; nothing when all of it is
        /// behind. The part is the corners in front and where the lines between corners cross the camera plane: clip
        /// space is linear, so the crossings of the edges are among them, and the other crossings lie inside the part.
        std::optional<ViewRect> ProjectBounds(const Matrix4x4& viewProjection, const AABB& bounds)
        {
            /// A point this near the camera plane is still in front of it, far out on the view.
            constexpr float NearW = 1e-3f;
            constexpr float Inf = std::numeric_limits<float>::max();
            std::array<Vector4, 8> clip;
            const auto corners = bounds.GetCorners();
            std::transform(corners.begin(), corners.end(), clip.begin(), [&](const Vector3& corner) { return viewProjection.TransformPoint(corner); });
            ViewRect covered { Inf, Inf, -Inf, -Inf };
            bool inFront = false;
            const auto cover = [&](const Vector4& point) {
                inFront = true;
                covered.minX = (std::min)(covered.minX, point.x / point.w);
                covered.minY = (std::min)(covered.minY, point.y / point.w);
                covered.maxX = (std::max)(covered.maxX, point.x / point.w);
                covered.maxY = (std::max)(covered.maxY, point.y / point.w);
            };
            for (size_t from = 0; from < clip.size(); ++from) {
                if (clip[from].w >= NearW)
                    cover(clip[from]);
                for (size_t to = from + 1; to < clip.size(); ++to) {
                    if ((clip[from].w < NearW) != (clip[to].w < NearW))
                        cover(clip[from] + (clip[to] - clip[from]) * ((NearW - clip[from].w) / (clip[to].w - clip[from].w)));
                }
            }
            return inFront ? std::optional { covered } : std::nullopt;
        }

        /// Moves the node along world Y so its bottom sits on the surface below it (or y = 0 if nothing beneath).
        bool SnapNodeToGround(Scene& scene, const ModelCatalog& models, NodeId node)
        {
            const auto found = NodeBounds(scene, models, node);
            if (!found)
                return false;
            const AABB& nodeBounds = *found;
            const auto* nodeModels = scene.FindComponents<ModelComponent>();
            const float currentBottomY = nodeBounds.minBounds.y;

            float groundY = 0.0f;
            bool foundSurface = false;
            float highestUnderneath = -1e9f;

            const auto nodes = scene.Nodes();
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].id == node)
                    continue;
                const ModelComponent* otherComp = nodeModels ? nodeModels->Find(nodes[i].id) : nullptr;
                const Model* otherModel = otherComp && otherComp->visible ? models.Get(otherComp->model) : nullptr;
                if (!otherModel || otherModel->submeshes.empty())
                    continue;
                const AABB otherBounds = otherModel->GetLocalBounds().Transformed(scene.WorldTransforms()[i]);
                const bool overlapX = otherBounds.minBounds.x <= nodeBounds.maxBounds.x && otherBounds.maxBounds.x >= nodeBounds.minBounds.x;
                const bool overlapZ = otherBounds.minBounds.z <= nodeBounds.maxBounds.z && otherBounds.maxBounds.z >= nodeBounds.minBounds.z;
                if (overlapX && overlapZ && otherBounds.maxBounds.y <= currentBottomY + 0.05f) {
                    if (otherBounds.maxBounds.y > highestUnderneath) {
                        highestUnderneath = otherBounds.maxBounds.y;
                        foundSurface = true;
                    }
                }
            }
            if (foundSurface)
                groundY = highestUnderneath;

            const float dy = groundY - currentBottomY;
            if (std::abs(dy) < 1e-4f)
                return false;

            math::Matrix4x4 world = scene.WorldTransforms()[scene.IndexOf(node)];
            world(1, 3) += dy;
            return scene.SetWorldTransform(node, world);
        }

    } // namespace

    Model MakePrimitiveModel(ModelPrimitive primitive)
    {
        Model model;
        model.name = ModelPrimitiveName(primitive);
        switch (primitive) {
        case ModelPrimitive::Cube:
            model.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            break;
        case ModelPrimitive::Wall:
            model.AddSubmesh(GeometryPrimitives::CreateWall(2.0f, 2.0f, 0.2f));
            break;
        case ModelPrimitive::GroundPlane:
            model.AddSubmesh(GeometryPrimitives::CreateGroundPlane(10.0f, 10.0f));
            break;
        }
        return model;
    }

    NodeId AddPrimitiveNode(Scene& scene, ModelCatalog& models, ModelPrimitive primitive, NodeId parent)
    {
        math::Vector4 color = DefaultModelColor;
        math::Vector3 position { 0.0f, 0.0f, 0.0f };
        switch (primitive) {
        case ModelPrimitive::Cube:
            color = math::Vector4 { 0.7f, 0.75f, 0.8f, 1.0f };
            position = math::Vector3 { 0.0f, 0.5f, 0.0f };
            break;
        case ModelPrimitive::Wall:
            color = math::Vector4 { 0.6f, 0.6f, 0.65f, 1.0f };
            position = math::Vector3 { 0.0f, 1.0f, 0.0f };
            break;
        case ModelPrimitive::GroundPlane:
            color = math::Vector4 { 0.4f, 0.5f, 0.4f, 1.0f };
            break;
        }
        const ModelHandler model = models.Insert(MakePrimitiveModel(primitive));
        const NodeId id = scene.AddNode(ModelPrimitiveName(primitive), math::Matrix4x4::Translation(position), parent);
        scene.Components<ModelComponent>().Emplace(id, ModelComponent { .model = model, .color = color, .visible = true });
        return id;
    }

    EngineResult<ModelHandler> ModelFromFile(ModelCatalog& models, const Path& file, const Path& assetsFolder)
    {
        if (const ModelHandler kept = models.FindByFile(file))
            return kept;
        auto imported = resource::AssetImport::ImportModel(file, assetsFolder);
        if (!imported)
            return MakeUnexpected(imported.error());
        return models.Insert(std::move(imported->model), std::move(imported->file));
    }

    NodeId AddModelNode(Scene& scene, ModelHandler model, const String& name, NodeId parent)
    {
        const NodeId id = scene.AddNode(name.empty() ? String("Model") : name, math::Matrix4x4::Identity(), parent);
        scene.Components<ModelComponent>().Emplace(id, ModelComponent { .model = model, .color = DefaultModelColor, .visible = true });
        return id;
    }

    Vector<NodeId> DuplicateNodes(Scene& scene, std::span<const NodeId> nodes, const math::Vector3& offset)
    {
        // A parent comes before its children in the scene: its copy is there when theirs are made
        Vector<NodeId> sources;
        std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(sources), [&](NodeId node) { return scene.Find(node) != nullptr; });
        std::sort(sources.begin(), sources.end(), [&](NodeId left, NodeId right) { return scene.IndexOf(left) < scene.IndexOf(right); });
        UnorderedMap<uint32_t, NodeId> copies;
        for (const NodeId source : sources) {
            const Scene::Node& found = *scene.Find(source);
            const auto parentCopy = copies.find(found.parent.Raw());
            const bool underCopy = parentCopy != copies.end();
            auto transform = found.localTransform;
            // Under the parent's copy the place is the parent's offset already
            if (!underCopy) {
                transform(0, 3) += offset.x;
                transform(1, 3) += offset.y;
                transform(2, 3) += offset.z;
            }
            const String name = found.name + " Copy";
            const NodeId parent = underCopy ? parentCopy->second : found.parent;
            const NodeId copy = scene.AddNode(name, transform, parent);
            scene.CopyComponents(source, copy);
            copies.emplace(source.Raw(), copy);
        }
        Vector<NodeId> ordered;
        for (const NodeId node : nodes) {
            if (const auto copy = copies.find(node.Raw()); copy != copies.end())
                ordered.push_back(copy->second);
        }
        return ordered;
    }

    Vector<NodeId> DuplicateNodeArray(Scene& scene, NodeId source, int countX, int countZ, float stepX, float stepZ)
    {
        Vector<NodeId> created;
        const Scene::Node* found = scene.Find(source);
        if (!found || countX < 1 || countZ < 1)
            return created;

        const auto baseTransform = found->localTransform;
        const String baseName = found->name;
        const NodeId parent = found->parent;

        for (int z = 0; z < countZ; ++z) {
            for (int x = 0; x < countX; ++x) {
                if (x == 0 && z == 0)
                    continue;
                auto transform = baseTransform;
                transform(0, 3) += static_cast<float>(x) * stepX;
                transform(2, 3) += static_cast<float>(z) * stepZ;
                const String name = baseName + "_" + std::to_string(z).c_str() + "_" + std::to_string(x).c_str();
                const NodeId copy = scene.AddNode(name, transform, parent);
                scene.CopyComponents(source, copy);
                created.push_back(copy);
            }
        }
        return created;
    }

    bool SnapNodesToGround(Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes)
    {
        bool snapped = false;
        for (const NodeId node : TopmostNodes(scene, nodes))
            snapped = SnapNodeToGround(scene, models, node) || snapped;
        return snapped;
    }

    NodeId RemoveNodesFromPlace(Scene& scene, std::span<const NodeId> nodes)
    {
        NodeId inPlace;
        for (const NodeId node : nodes) {
            const size_t removedIndex = scene.IndexOf(node);
            if (removedIndex == Scene::NoNode)
                continue;
            (void)scene.RemoveNode(node);
            inPlace = scene.IdOf((std::min)(removedIndex, scene.NodeCount() - 1));
        }
        return inPlace;
    }

    std::optional<AABB> NodeBounds(const Scene& scene, const ModelCatalog& models, NodeId node)
    {
        const size_t index = scene.IndexOf(node);
        if (index == Scene::NoNode)
            return std::nullopt;
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        const ModelComponent* component = nodeModels ? nodeModels->Find(node) : nullptr;
        const Model* model = component ? models.Get(component->model) : nullptr;
        const Vector3 extent { NodeWithoutModelExtent, NodeWithoutModelExtent, NodeWithoutModelExtent };
        const AABB local = model && !model->submeshes.empty() ? model->GetLocalBounds() : AABB { Vector3 {} - extent, extent };
        return local.Transformed(scene.WorldTransforms()[index]);
    }

    std::optional<AABB> GroupBounds(const Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes)
    {
        AABB group;
        bool found = false;
        for (const NodeId node : nodes) {
            if (const auto bounds = NodeBounds(scene, models, node)) {
                group = group.Merged(*bounds);
                found = true;
            }
        }
        return found ? std::optional { group } : std::nullopt;
    }

    Vector<NodeId> TopmostNodes(const Scene& scene, std::span<const NodeId> nodes)
    {
        Set<uint32_t> among;
        for (const NodeId node : nodes)
            among.insert(node.Raw());
        const auto isAmong = [&](NodeId node) { return among.contains(node.Raw()); };
        Vector<NodeId> topmost;
        for (const NodeId node : nodes) {
            const Scene::Node* found = scene.Find(node);
            if (!found)
                continue;
            bool underAnother = false;
            for (const Scene::Node* ancestor = scene.Find(found->parent); ancestor && !underAnother; ancestor = scene.Find(ancestor->parent))
                underAnother = isAmong(ancestor->id);
            if (!underAnother)
                topmost.push_back(node);
        }
        return topmost;
    }

    Vector<NodeId> PickNodesInRect(const Scene& scene, const ModelCatalog& models, const Matrix4x4& viewProjection, const ViewRect& rect)
    {
        Vector<NodeId> picked;
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        if (!nodeModels)
            return picked;
        const auto nodes = scene.Nodes();
        for (size_t index = 0; index < nodes.size(); ++index) {
            const ModelComponent* component = nodeModels->Find(nodes[index].id);
            const Model* model = component && component->visible ? models.Get(component->model) : nullptr;
            if (!model || model->submeshes.empty())
                continue;
            const auto covered = ProjectBounds(viewProjection, model->GetLocalBounds().Transformed(scene.WorldTransforms()[index]));
            if (covered && covered->minX <= rect.maxX && covered->maxX >= rect.minX && covered->minY <= rect.maxY && covered->maxY >= rect.minY)
                picked.push_back(nodes[index].id);
        }
        return picked;
    }

    NodeId PickNode(const Scene& scene, const ModelCatalog& models, const ViewRay& ray)
    {
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        if (!nodeModels)
            return {};
        NodeId picked;
        float nearest = std::numeric_limits<float>::max();
        const auto nodes = scene.Nodes();
        for (size_t index = 0; index < nodes.size(); ++index) {
            const ModelComponent* component = nodeModels->Find(nodes[index].id);
            const Model* model = component && component->visible ? models.Get(component->model) : nullptr;
            if (!model || model->submeshes.empty())
                continue;
            const auto distance = EnterDistance(ray, model->GetLocalBounds().Transformed(scene.WorldTransforms()[index]));
            if (distance && *distance < nearest) {
                nearest = *distance;
                picked = nodes[index].id;
            }
        }
        return picked;
    }

    void FrameNodes(Camera& camera, const Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes)
    {
        const auto bounds = GroupBounds(scene, models, nodes);
        if (!bounds)
            return;
        const float distance = bounds->GetExtent().Length() / std::sin(camera.fovRadians * 0.5f);
        camera.SetPosition(bounds->GetCenter() - camera.GetForwardDirection() * distance);
    }

    Collider FitColliderToModel(const Model* model)
    {
        Collider collider;
        if (!model || model->submeshes.empty())
            return collider;
        const auto bounds = model->GetLocalBounds();
        if (bounds.minBounds.x > bounds.maxBounds.x)
            return collider;

        math::Vector3 half = (bounds.maxBounds - bounds.minBounds) * 0.5f;
        math::Vector3 center = (bounds.maxBounds + bounds.minBounds) * 0.5f;
        constexpr float FlatThickness = 0.25f;
        if (half.x < 0.01f) {
            half.x = FlatThickness;
            center.x -= FlatThickness;
        }
        if (half.y < 0.01f) {
            half.y = FlatThickness;
            center.y -= FlatThickness;
        }
        if (half.z < 0.01f) {
            half.z = FlatThickness;
            center.z -= FlatThickness;
        }

        collider.halfExtents = half;
        collider.offset = center;
        collider.radius = (std::max)(half.x, half.z);
        collider.halfHeight = (std::max)(half.y - collider.radius, 0.0f);
        return collider;
    }

    const ScriptValue* FindOverride(const ScriptComponent& script, StringView name)
    {
        for (const auto& [key, value] : script.props) {
            if (key == name)
                return &value;
        }
        return nullptr;
    }

    void SetOverride(ScriptComponent& script, const String& name, ScriptValue value)
    {
        const auto position = std::lower_bound(script.props.begin(), script.props.end(), name,
            [](const auto& prop, const String& key) { return prop.first < key; });
        if (position != script.props.end() && position->first == name)
            position->second = std::move(value);
        else
            script.props.emplace(position, name, std::move(value));
    }

    void RemoveOverride(ScriptComponent& script, StringView name)
    {
        std::erase_if(script.props, [&](const auto& prop) { return prop.first == name; });
    }

    Path ScriptModuleFile(const Path& scriptsFolder, StringView module)
    {
        if (module.empty())
            return {};
        String relative(module);
        std::replace(relative.begin(), relative.end(), '.', '/');
        const Path file = scriptsFolder / (relative + ".lua");
        if (FileSystem::Exists(file))
            return file;
        const Path init = scriptsFolder / relative / "init.lua";
        return FileSystem::Exists(init) ? init : Path {};
    }

    Vector<String> ListScriptModules(const Path& scriptsFolder)
    {
        Vector<String> modules;
        Vector<Path> folders { scriptsFolder };
        while (!folders.empty()) {
            const Path folder = std::move(folders.back());
            folders.pop_back();
            std::error_code error;
            for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path path = it.CurrentPath();
                std::error_code typeError;
                if (it.IsDirectory(typeError)) {
                    if (!path.Filename().string().starts_with("."))
                        folders.push_back(path);
                    continue;
                }
                const String relative(path.LexicallyRelative(scriptsFolder).generic_string().c_str());
                if (!relative.ends_with(".lua") || relative.starts_with(".."))
                    continue;
                String module = relative.substr(0, relative.size() - 4);
                if (module == "init")
                    continue;
                if (module.ends_with("/init"))
                    module.resize(module.size() - 5);
                std::replace(module.begin(), module.end(), '/', '.');
                if (!module.empty())
                    modules.push_back(std::move(module));
            }
        }
        std::sort(modules.begin(), modules.end());
        return modules;
    }

} // namespace elm
