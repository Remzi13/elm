#include "script/bindings/Bindings.hpp"

#include "script/values/Entity.hpp"

#include "core/AssetPaths.hpp"
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

        ModelRef Insert(lua_State* L, graphics::Model model) { return { ModelsOf(L).Insert(std::move(model)) }; }

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

        /// A .model or .gltf file; a relative path starts at the assets folder.
        EngineResult<ModelRef> Load(lua_State* L, StringView file)
        {
            ModelsOf(L);
            const Path relative(file);
            const Path path = relative.IsAbsolute() ? relative : AssetPaths::Root() / relative;
            const bool gltf = path.Extension().string() == ".gltf";
            auto model = gltf ? resmgr::GltfExporter::LoadModel(path) : resmgr::Serializer::LoadModel(path);
            if (!model)
                return MakeUnexpected(model.error());
            return Insert(L, std::move(*model));
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

    void RegisterModels(lua::State& state, graphics::ModelManager* models)
    {
        state.SetContext(ModelsContext { models });
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
            .Meta<[](lua_State* L, const ModelRef& model) {
                return IsModelValid(L, model) ? "Model(" + ModelOf(L, model).name + ")" : String("Model(removed)");
            }>("__tostring");
    }

} // namespace elm::script::bindings
