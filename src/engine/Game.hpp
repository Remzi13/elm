#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "engine/FrameClock.hpp"

#include "graphics/GraphicsSystem.hpp"
#include "graphics/debug/DebugDrawing.hpp"

#include "physics/PhysicsSystem.hpp"

#include "platform/PlatformSystem.hpp"

#include "resource/ResourceSystem.hpp"

#include "Scene/SceneSystem.hpp"
#include "Scene/TestScenes.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

namespace elm {

    /// What the game is started with.
    struct GameDesc {
        Size window { 1280, 720 };
        StringView title { "elm" };
    };

    /// The program elm (docs/project/Проекты.md#Плеер): the root of the composition (docs/core/Системы.md).
    /// Owns the systems as fields, gives them their dependencies and runs the frame: Play Project of one
    /// project, its scene in the whole window, until the window closes or the scripts stop the game.
    /// Owner: main. Thread: update (the main thread).
    class Game {
    public:
        Game();
        /// Shuts down if Shutdown was not called.
        ~Game();

        Game(const Game&) = delete;
        Game& operator=(const Game&) = delete;
        Game(Game&&) = delete;
        Game& operator=(Game&&) = delete;

        /// Reads .settings, opens the window, starts rendering and physics; debug drawing with ELM_DEBUG_DRAW.
        [[nodiscard]] auto Init(const GameDesc& desc) -> EngineResult<void>;
        /// Opens the project to play from its folder or its .elmproject file: its scripts start.
        [[nodiscard]] auto OpenProject(const Path& folderOrFile) -> EngineResult<void>;
        /// A test scene in the scene, added to what is there.
        void LoadTestScene(ScenePreset preset, uint32_t instanceCount);
        /// Plays the project until the game ends.
        void Run();
        /// Ends the scripts and every system.
        void Shutdown();

    private:
        /// False when the game ends: the window closed, the scripts stopped it or Play could not start.
        [[nodiscard]] bool RunFrame();
        /// The fixed physics steps with the scripts, the frame's input and scripts, culling.
        void Simulate(float deltaTime);
        void Draw();
        /// The physics visualizers (channels Physics/* in .settings) and elm.debug of the scripts.
        void AddDebugDraw();

        Settings m_settings;
        platform::PlatformSystem m_platform;
        resource::ResourceSystem m_resources;
        SceneSystem m_scene;
        PhysicsSystem m_physics;
        script::ScriptSystem m_scripts;
        graphics::GraphicsSystem m_graphics;
        script::PlayMode m_playMode;
        FrameClock m_clock;
        /// Only in a build with ELM_DEBUG_DRAW; without it elm.debug draws nothing.
        UniquePtr<graphics::DebugDrawing> m_debugDraw;
        bool m_initialized { false };
    };

} // namespace elm
