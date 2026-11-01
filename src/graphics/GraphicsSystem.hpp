#pragma once

#include "core/Error.hpp"

#include "graphics/Camera.hpp"
#include "graphics/ModelMeshes.hpp"
#include "graphics/culling/OcclusionCulling.hpp"

#include "render/Render.hpp"
#include "render/ViewPort.hpp"

#include <thread>

namespace elm {
    class ModelCatalog;
    class SceneSystem;
    class Settings;
}

namespace elm::graphics {

    /// Everything shown (docs/core/Системы.md): the view of the scene (its camera and viewport), culling, the
    /// models of the scene with their GPU meshes into the frame, the game view in the whole window, the frame
    /// handed to the render thread. Reads the scene and the resources' models, never writes them: world
    /// transforms are the program's to update before DrawWorld.
    /// Owner: the program (Game, Editor). Thread: update; the render thread is its own between Init and
    /// StopRendering.
    class GraphicsSystem {
    public:
        /// Registers the culling settings.
        GraphicsSystem(Settings& settings, const SceneSystem& scene, const ModelCatalog& models);
        ~GraphicsSystem();

        GraphicsSystem(const GraphicsSystem&) = delete;
        GraphicsSystem& operator=(const GraphicsSystem&) = delete;
        GraphicsSystem(GraphicsSystem&&) = delete;
        GraphicsSystem& operator=(GraphicsSystem&&) = delete;

        /// Rendering into the window and its thread, the view of viewSize, culling.
        [[nodiscard]] auto Init(const render::NativeWindow& window, Size surfaceSize, Size viewSize) -> EngineResult<void>;
        /// Stops the render thread; the program then frees what it rendered with (UI) before Shutdown.
        void StopRendering();
        /// Frees the meshes, the view and rendering. Calls StopRendering if the program did not.
        void Shutdown();

        /// What of the scene the view camera sees, as the scene is now.
        void Cull();
        /// Blocks only while the render thread is two frames behind; invalid once rendering is stopped.
        [[nodiscard]] render::FrameWriter BeginFrame();
        /// The view of the scene: the shown models culling did not reject. It stays the current scene view of
        /// frame, so debug primitives drawn after go into it.
        void DrawWorld(render::FrameWriter& frame, Size backbuffer);
        /// The view in the whole window (the game alone); the editor shows it in its viewport window instead.
        void PresentToWindow(render::FrameWriter& frame, Size framebuffer);
        /// Hands the frame to the render thread.
        void EndFrame(render::FrameWriter& frame);

        [[nodiscard]] Camera& GetViewCamera() noexcept { return m_viewCamera; }
        [[nodiscard]] render::ViewPort& GetViewPort() noexcept { return m_viewPort; }
        [[nodiscard]] OcclusionCulling& GetCulling() noexcept { return m_culling; }
        [[nodiscard]] render::Render& GetRender() noexcept { return *m_render; }

    private:
        /// Render thread entry point: draws submitted frames until rendering is stopped.
        void RenderThreadFunc();
        void SubmitModels(render::FrameWriter& frame);

        Settings& m_settings;
        const SceneSystem& m_scene;
        const ModelCatalog& m_models;

        UniquePtr<render::Render> m_render;
        ModelMeshes m_meshes;
        render::ViewPort m_viewPort;
        Camera m_viewCamera;
        OcclusionCulling m_culling;
        bool m_initialized { false };
        // Update and render run in parallel: the update thread builds frame N+1 while the render
        // thread draws frame N; Render's frame ring hands snapshots over (see render/frame/FrameRing.hpp).
        std::thread m_renderThread;
    };

} // namespace elm::graphics
