#include "script/bindings/Bindings.hpp"

#include "script/entity/EntityScripts.hpp"

#include "project/Project.hpp"

#include "model/ModelCatalog.hpp"

#include "resource/Serializer.hpp"

namespace elm::script::bindings {

    namespace {

        EngineModuleContext& Context(lua_State* L) { return ContextOf<EngineModuleContext>(L); }
        Scene& SceneOf(lua_State* L) { return RequireSystem(L, Context(L).scene, "elm.scene"); }
        ModelCatalog& ModelsOf(lua_State* L) { return RequireSystem(L, Context(L).models, "elm.models"); }

        /// The scripts end while the scene they live in is still there.
        void EndEntityScripts(lua_State* L)
        {
            if (auto* entityScripts = Context(L).entityScripts)
                entityScripts->DestroyAll();
        }
      
        /// Removes every node and model; entity scripts get on_destroy first.
        void ClearScene(lua_State* L)
        {
            auto& scene = SceneOf(L);
            EndEntityScripts(L);
            scene.Clear();
            if (auto* models = Context(L).models)
                models->Clear();
        }

        const project::Project& ProjectOf(lua_State* L) { return RequireSystem(L, Context(L).project, "the project"); }

        /// A scene file of the project by its asset path, "scenes/level.scene".
        Path ScenePath(lua_State* L, StringView file)
        {
            const auto asset = project::AssetPath::FromString(file);
            if (!asset)
                luaL_error(L, "'%s' is not a path in the assets folder (like \"scenes/level.scene\")", String(file).c_str());
            return ProjectOf(L).Resolve(*asset);
        }

        /// The new models go to the project's models folder first: the scene references their files.
        EngineResult<void> SaveScene(lua_State* L, StringView file)
        {
            const Path path = ScenePath(L, file);
            const Path assets = ProjectOf(L).Assets();
            if (auto newModels = resource::Serializer::SaveNewModels(SceneOf(L), ModelsOf(L), assets); !newModels)
                return MakeUnexpected(newModels.error());
            return resource::Serializer::SaveScene(SceneOf(L), ModelsOf(L), assets, path);
        }

        /// Replaces the scene with a scene file; returns how many nodes it has.
        EngineResult<size_t> LoadScene(lua_State* L, StringView file)
        {
            auto& scene = SceneOf(L);
            auto& models = ModelsOf(L);
            auto loaded = resource::Serializer::LoadScene(ScenePath(L, file), ProjectOf(L).Assets());
            if (!loaded)
                return MakeUnexpected(loaded.error());
            EndEntityScripts(L);
            scene.Clear();
            models.Clear();
            const auto handlers = models.InsertAll(std::move(loaded->models), loaded->modelFiles);
            (void)scene.Append(std::move(*loaded), handlers);
            return scene.NodeCount();
        }

        // Play mode: requests, the mode changes at the start of the next update (PlayMode)

        void PlayScene(lua_State* L)
        {
            const auto& play = Context(L).play.playScene;
            if (!play)
                luaL_error(L, "elm.engine.play is not available here");
            play();
        }

        void StopScene(lua_State* L)
        {
            const auto& stop = Context(L).play.stop;
            if (!stop)
                luaL_error(L, "elm.engine.stop is not available here");
            stop();
        }

        bool IsPlaying(lua_State* L)
        {
            const auto& isPlaying = Context(L).play.isPlaying;
            return isPlaying && isPlaying();
        }

    } // namespace

    void RegisterEngine(lua::State& state, EngineModuleContext context)
    {
        state.SetContext(std::move(context));
        lua::Module(state, "elm.engine")            
            .Function<&ClearScene>("clear_scene")
            .Function<&SaveScene>("save_scene", { "file" })
            .Function<&LoadScene>("load_scene", { "file" })
            .Function<&PlayScene>("play")
            .Function<&StopScene>("stop")
            .Function<&IsPlaying>("is_playing");
    }

} // namespace elm::script::bindings
