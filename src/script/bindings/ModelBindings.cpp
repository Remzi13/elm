#include "script/bindings/Bindings.hpp"

#include "script/values/Entity.hpp"

#include "project/Project.hpp"
#include "math/Primitivs.hpp"
#include "graphics/ModelManager.hpp"
#include "resmgr/GltfExporter.hpp"
#include "resmgr/Serializer.hpp"

namespace elm::script::bindings {

    namespace {

        using math::Vector3;

        /// What elm.models and the Model class reach.
        struct ModelsContext {
            graphics::ModelManager* models { nullptr };
            const project::Project* project { nullptr };
        };

        graphics::ModelManager& ModelsOf(lua_State* L) { return RequireSystem(L, ContextOf<ModelsContext>(L).models, "elm.models"); }

        const graphics::Model& ModelOf(lua_State* L, const ModelRef& model)
        {
            const auto* found = ModelsOf(L).Get(model.handle);
            if (!found)
                luaL_error(L, "the model was removed");
            return *found;
        }

        bool IsModelValid(lua_State* L, const ModelRef& model)
        {
            const auto* models = ContextOf<ModelsContext>(L).models;
            return models && models->Get(model.handle);
        }

        ModelRef Insert(lua_State* L, graphics::Model model, Path file = { })
        {
            return { ModelsOf(L).Insert(std::move(model), std::move(file)) };
        }

        ModelRef MakePrimitive(lua_State* L, const char* name, MeshData mesh)
        {
            graphics::Model model { String(name) };
            model.AddSubmesh(std::move(mesh));
            return Insert(L, std::move(model));
        }

        ModelRef Cube(lua_State* L, std::optional<float> size)
        {
            return MakePrimitive(L, "Cube", GeometryPrimitives::CreateCube(size.value_or(1.0f)));
        }
        ModelRef Box(lua_State* L, const Vector3& halfExtents) { return MakePrimitive(L, "Box", GeometryPrimitives::CreateBox(halfExtents)); }
        ModelRef Plane(lua_State* L, float sizeX, float sizeZ)
        {
            return MakePrimitive(L, "Plane", GeometryPrimitives::CreateGroundPlane(sizeX, sizeZ));
        }

        /// A .model or .gltf file of the project by its asset path, "models/crate.model". A .model keeps
        /// its file (ModelManager::FileOf); a glTF model is new and gets a .model file when the scene is saved.
        EngineResult<ModelRef> Load(lua_State* L, StringView file)
        {
            ModelsOf(L);
            const auto asset = project::AssetPath::FromString(file);
            if (!asset)
                luaL_error(L, "'%s' is not a path in the assets folder (like \"models/crate.model\")", String(file).c_str());
            const Path path = RequireSystem(L, ContextOf<ModelsContext>(L).project, "the project").Resolve(*asset);
            const bool gltf = path.Extension().string() == ".gltf";
            auto model = gltf ? resmgr::GltfExporter::LoadModel(path) : resmgr::Serializer::LoadModel(path);
            if (!model)
                return MakeUnexpected(model.error());
            return Insert(L, std::move(*model), gltf ? Path { } : path);
        }

        /// The asset path of the model's file, nil for a model without one.
        std::optional<String> FileOf(lua_State* L, const ModelRef& model)
        {
            ModelOf(L, model);
            const Path* file = ModelsOf(L).FileOf(model.handle);
            const auto* project = ContextOf<ModelsContext>(L).project;
            const auto asset = file && project ? project->AssetPathOf(*file) : std::nullopt;
            return asset ? std::optional<String>(asset->Text()) : std::nullopt;
        }

        std::optional<ModelRef> FindModel(lua_State* L, StringView name)
        {
            std::optional<ModelRef> found;
            ModelsOf(L).ForEach([&](graphics::ModelHandler handle, const graphics::Model& model) {
                if (!found && model.name == name)
                    found = ModelRef { handle };
            });
            return found;
        }

    } // namespace

    void RegisterModels(lua::State& state, graphics::ModelManager* models, const project::Project* project)
    {
        state.SetContext(ModelsContext { models, project });
        lua::Module module(state, "elm.models");
        module
            .Function<&Cube>("cube", { "size" })
            .Function<&Box>("box", { "half_extents" })
            .Function<&Plane>("plane", { "size_x", "size_z" })
            .Function<&Load>("load", { "path" })
            .Function<&FindModel>("find", { "name" });

        lua::Class<ModelRef>(module, "Model")
            .Property<&IsModelValid>("valid")
            .Property<[](lua_State* L, const ModelRef& model) { return ModelOf(L, model).name; }>("name")
            .Property<[](lua_State* L, const ModelRef& model) { return ModelOf(L, model).submeshes.size(); }>("submesh_count")
            .Property<&FileOf>("file")
            .Meta<[](lua_State* L, const ModelRef& model) {
                return IsModelValid(L, model) ? "Model(" + ModelOf(L, model).name + ")" : String("Model(removed)");
            }>("__tostring");
    }

} // namespace elm::script::bindings
