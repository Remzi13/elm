#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Std.hpp"

#include "model/ModelHandler.hpp"

#include "Scene/NodeId.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include <cstdint>

namespace elm {

class Model;
class ModelCatalog;
class Scene;

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
/// A copy of the node with its components next to it, 1 along x; invalid for a stale node.
[[nodiscard]] NodeId DuplicateNode(Scene& scene, NodeId source);
/// Removes the node. Returns the node that took its place in the scene, for the selection.
[[nodiscard]] NodeId RemoveNodeFromPlace(Scene& scene, NodeId node);

/// A collider around the model, for a flat model a thin one under it; the default one without a model.
[[nodiscard]] Collider FitColliderToModel(const Model* model);

/// The value this node sets for a prop of its script, nullptr: the script's own.
[[nodiscard]] const ScriptValue* FindOverride(const ScriptComponent& script, StringView name);
/// Props stay sorted by name, as they come from Lua and from scene files.
void SetOverride(ScriptComponent& script, const String& name, ScriptValue value);
void RemoveOverride(ScriptComponent& script, StringView name);

/// The Lua modules under the scripts folder by their require names, sorted: game/init.lua is game.
[[nodiscard]] Vector<String> ListScriptModules(const Path& scriptsFolder);

} // namespace elm
