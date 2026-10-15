#include "script/bindings/Bindings.hpp"

#include "script/entity/EntityScripts.hpp"

#include "core/AssetPaths.hpp"
#include "graphics/ModelManager.hpp"
#include "Scene/TestScenes.hpp"
#include "resmgr/Serializer.hpp"

namespace elm::script::bindings {

    namespace {

        EngineModuleContext& Context(lua_State* L) { return ContextOf<EngineModuleContext>(L); }
        Scene& SceneOf(lua_State* L) { return RequireSystem(L, Context(L).scene, "elm.scene"); }
        graphics::ModelManager& ModelsOf(lua_State* L) { return RequireSystem(L, Context(L).models, "elm.models"); }

        /// The scripts end while the scene they live in is still there.
        void EndEntityScripts(lua_State* L)
        {
            if (auto* entityScripts = Context(L).entityScripts)
                entityScripts->DestroyAll();
        }

        /// Replaces the scene and all models with a built-in test scene (0 Box, 1 WallAndGrid,
        /// 2 RoomsAndCorridors, 3 PhysicsSandbox).
        void LoadTestScene(lua_State* L, uint32_t preset, std::optional<uint32_t> instances)
        {
            if (preset > static_cast<uint32_t>(ScenePreset::PhysicsSandbox))
                luaL_argerror(L, 1, "preset must be 0..3");
            auto& scene = SceneOf(L);
            auto& models = ModelsOf(L);
            EndEntityScripts(L);
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), instances.value_or(1500), scene, models);
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

        /// A relative scene file is taken from assets/scenes.
        Path ScenePath(StringView file)
        {
            const Path path(file);
            return path.IsAbsolute() ? path : AssetPaths::Scenes() / path;
        }

        EngineResult<void> SaveScene(lua_State* L, StringView file)
        {
            return resmgr::Serializer::SaveScene(SceneOf(L), ModelsOf(L), ScenePath(file), AssetPaths::Models());
        }

        /// Replaces the scene with a scene file; returns how many nodes it has.
        EngineResult<size_t> LoadScene(lua_State* L, StringView file)
        {
            auto& scene = SceneOf(L);
            auto& models = ModelsOf(L);
            auto loaded = resmgr::Serializer::LoadScene(ScenePath(file));
            if (!loaded)
                return MakeUnexpected(loaded.error());
            EndEntityScripts(L);
            scene.Clear();
            models.Clear();
            scene.Append(std::move(*loaded), models);
            return scene.nodes.size();
        }

        // Play mode: requests, the mode changes at the start of the next update (PlayMode)

        void PlayScene(lua_State* L)
        {
            const auto& play = Context(L).play.play;
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
            .Function<&LoadTestScene>("load_test_scene", { "preset", "instances" })
            .Function<&ClearScene>("clear_scene")
            .Function<&SaveScene>("save_scene", { "file" })
            .Function<&LoadScene>("load_scene", { "file" })
            .Function<&PlayScene>("play")
            .Function<&StopScene>("stop")
            .Function<&IsPlaying>("is_playing");
    }

} // namespace elm::script::bindings
