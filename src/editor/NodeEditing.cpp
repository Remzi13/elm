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

    NodeId DuplicateNode(Scene& scene, NodeId source)
    {
        const Scene::Node* found = scene.Find(source);
        if (!found)
            return {};
        auto transform = found->localTransform;
        transform(0, 3) += 1.0f;
        const String name = found->name + " Copy";
        const NodeId parent = found->parent;
        const NodeId copy = scene.AddNode(name, transform, parent);
        scene.CopyComponents(source, copy);
        return copy;
    }

    NodeId RemoveNodeFromPlace(Scene& scene, NodeId node)
    {
        const size_t removedIndex = scene.IndexOf(node);
        if (removedIndex == Scene::NoNode)
            return {};
        (void)scene.RemoveNode(node);
        return scene.IdOf((std::min)(removedIndex, scene.NodeCount() - 1));
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

    void FrameNode(Camera& camera, const Scene& scene, const ModelCatalog& models, NodeId node)
    {
        const size_t index = scene.IndexOf(node);
        if (index == Scene::NoNode)
            return;
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        const ModelComponent* component = nodeModels ? nodeModels->Find(node) : nullptr;
        const Model* model = component ? models.Get(component->model) : nullptr;
        const Vector3 extent { NodeWithoutModelExtent, NodeWithoutModelExtent, NodeWithoutModelExtent };
        const AABB local = model && !model->submeshes.empty() ? model->GetLocalBounds() : AABB { Vector3 {} - extent, extent };
        const AABB bounds = local.Transformed(scene.WorldTransforms()[index]);
        const float distance = bounds.GetExtent().Length() / std::sin(camera.fovRadians * 0.5f);
        camera.SetPosition(bounds.GetCenter() - camera.GetForwardDirection() * distance);
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
