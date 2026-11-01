#pragma once

#include <cstdint>
#include <functional>

namespace elm {
    class Camera;
    class Input;
    class PhysicsSystem;
    class Scene;
    class SceneSystem;
    class Settings;
    class ModelCatalog;
    namespace graphics {
        class DebugDraw;
    }
    namespace project {
        class Project;
    }
    namespace resource {
        class ResourceSystem;
    }
}

namespace elm::script {

    /// Edit/Play switches for the scripts and the editor windows, given by PlayMode. They are
    /// requests: the program applies them at the start of its next frame.
    struct PlayRequests {
        /// Play as the game starts: the project's startup scene and main module.
        std::function<void()> playProject;
        /// Play the edited scene with the project's main module.
        std::function<void()> playScene;
        std::function<void()> stop;
        std::function<bool()> isPlaying;
    };

    /// The systems a ScriptSystem works with, given at construction: they outlive it. Any of them may be
    /// missing (tools, tests): the bindings that need it raise a Lua error then, without the scene a Play
    /// keeps no scene of its own.
    struct ScriptDeps {
        SceneSystem* scene { nullptr };
        PhysicsSystem* physics { nullptr };
        /// The models and the open project (its folders): the project opened when ScriptSystem::Init runs.
        resource::ResourceSystem* resources { nullptr };
        Input* input { nullptr };
        Camera* camera { nullptr };
        Settings* settings { nullptr };
        /// Debug drawing of the engine view (elm.debug).
        graphics::DebugDraw* debugDraw { nullptr };
    };

    /// What the bindings work with, made by ScriptSystem::Init from its ScriptDeps and its PlayMode.
    /// Any of them may be missing: the bindings that need it raise a Lua error then.
    struct EngineContext {
        Scene* scene { nullptr };
        ModelCatalog* models { nullptr };
        Input* input { nullptr };
        Camera* camera { nullptr };
        Settings* settings { nullptr };
        PhysicsSystem* physics { nullptr };
        PlayRequests play;
        /// Debug drawing of the engine view (elm.debug).
        graphics::DebugDraw* debugDraw { nullptr };
        /// The open project: the folders scene and model files are taken from.
        const project::Project* project { nullptr };
    };

    /// Frame timing as scripts see it (elm.time).
    struct ScriptTime {
        double time { 0.0 };
        float deltaTime { 0.0f };
        float fixedDeltaTime { 0.0f };
        uint64_t frame { 0 };
    };

} // namespace elm::script
