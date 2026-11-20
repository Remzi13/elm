#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Std.hpp"

#include "model/ModelHandler.hpp"

#include "Scene/NodeId.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace math {
    struct Matrix4x4;
} // namespace math

namespace elm {

    struct AABB;
    class Camera;
    class Model;
    class ModelCatalog;
    class Scene;
    struct ViewRay;

    /// What the editor's windows do to nodes and their components, without UI. Thread: update.

    /// The models the editor can make without a file.
    enum class ModelPrimitive : uint8_t {
        Cube,
        Wall,
        GroundPlane
    };

    [[nodiscard]] constexpr const char* ModelPrimitiveName(ModelPrimitive primitive)
    {
        switch (primitive) {
        case ModelPrimitive::Cube: return "Cube";
        case ModelPrimitive::Wall: return "Wall";
        case ModelPrimitive::GroundPlane: return "Ground Plane";
        }
        return "";
    }

    inline constexpr ModelPrimitive ModelPrimitives[] { ModelPrimitive::Cube, ModelPrimitive::Wall, ModelPrimitive::GroundPlane };

    [[nodiscard]] Model MakePrimitiveModel(ModelPrimitive primitive);
    /// A node with a new primitive model, standing on the ground of its parent. parent invalid: a root.
    [[nodiscard]] NodeId AddPrimitiveNode(Scene& scene, ModelCatalog& models, ModelPrimitive primitive, NodeId parent);
    /// The model of a file the user chose: the one the catalog has, otherwise the file is imported into the project
    /// (resource::AssetImport) and its model joins the catalog.
    [[nodiscard]] EngineResult<ModelHandler> ModelFromFile(ModelCatalog& models, const Path& file, const Path& assetsFolder);
    /// A node that draws the model. parent invalid: a root.
    [[nodiscard]] NodeId AddModelNode(Scene& scene, ModelHandler model, const String& name, NodeId parent);
    /// Copies of the nodes with their components, in the order of nodes; stale ones give none. A copy goes under the
    /// copy of its parent when the parent is among the nodes, otherwise beside its node, offset along local axes.
    [[nodiscard]] Vector<NodeId> DuplicateNodes(Scene& scene, std::span<const NodeId> nodes, const math::Vector3& offset = { 1.0f, 0.0f, 0.0f });
    /// Duplicates source in an N x M grid (countX along local X with stepX, countZ along local Z with stepZ).
    /// Returns the list of created node IDs.
    [[nodiscard]] Vector<NodeId> DuplicateNodeArray(Scene& scene, NodeId source, int countX, int countZ, float stepX, float stepZ);
    /// Moves the topmost of the nodes (TopmostNodes) along world Y so the bottom of each sits on the surface below it
    /// (or y = 0 if nothing beneath). Returns true if a transform was changed.
    bool SnapNodesToGround(Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes);
    /// Removes the nodes; their children stay. Returns the node that took the place of the last one in the scene,
    /// for the selection.
    [[nodiscard]] NodeId RemoveNodesFromPlace(Scene& scene, std::span<const NodeId> nodes);

    /// A rectangle of the view in normalized device coordinates: x to the right, y up, from -1 to 1.
    struct ViewRect {
        float minX { 0.0f };
        float minY { 0.0f };
        float maxX { 0.0f };
        float maxY { 0.0f };
    };

    /// The node's world bounds: its model's, a cube of 1 for a node without one; nothing for a stale node.
    [[nodiscard]] std::optional<AABB> NodeBounds(const Scene& scene, const ModelCatalog& models, NodeId node);
    /// The world bounds of all the nodes together; nothing when none of them is alive.
    [[nodiscard]] std::optional<AABB> GroupBounds(const Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes);
    /// The live nodes without the ones under another of them: moving these moves all of them once.
    [[nodiscard]] Vector<NodeId> TopmostNodes(const Scene& scene, std::span<const NodeId> nodes);
    /// The nodes whose world bounds reach into the rectangle of the view, in the order of the scene. Like PickNode,
    /// only what the view draws: a node without a model or with a hidden one is passed, so are bounds behind the view.
    [[nodiscard]] Vector<NodeId> PickNodesInRect(const Scene& scene, const ModelCatalog& models,
        const math::Matrix4x4& viewProjection, const ViewRect& rect);
    /// The node the ray of the view reaches first, by the world bounds of the models; invalid when it reaches none.
    /// Only what the view draws is picked: a node without a model or with a hidden one is passed, and so are
    /// bounds the ray starts inside of.
    [[nodiscard]] NodeId PickNode(const Scene& scene, const ModelCatalog& models, const ViewRay& ray);
    /// Moves the camera back along its view until the nodes' world bounds (GroupBounds) fit in it; the direction
    /// stays. Stale nodes move nothing.
    void FrameNodes(Camera& camera, const Scene& scene, const ModelCatalog& models, std::span<const NodeId> nodes);

    /// A collider around the model, for a flat model a thin one under it; the default one without a model.
    [[nodiscard]] Collider FitColliderToModel(const Model* model);

    /// The value this node sets for a prop of its script, nullptr: the script's own.
    [[nodiscard]] const ScriptValue* FindOverride(const ScriptComponent& script, StringView name);
    /// Props stay sorted by name, as they come from Lua and from scene files.
    void SetOverride(ScriptComponent& script, const String& name, ScriptValue value);
    void RemoveOverride(ScriptComponent& script, StringView name);

    /// The Lua modules under the scripts folder by their require names, sorted: game/init.lua is game.
    [[nodiscard]] Vector<String> ListScriptModules(const Path& scriptsFolder);
    /// The file of a module of that list: game.npc is game/npc.lua, or game/npc/init.lua. Empty when there is none.
    [[nodiscard]] Path ScriptModuleFile(const Path& scriptsFolder, StringView module);

} // namespace elm
