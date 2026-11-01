#include "graphics/GraphicsSystem.hpp"

#include "core/Profiling.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "graphics/GameView.hpp"

#include "model/ModelCatalog.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/SceneSystem.hpp"

namespace elm::graphics {

    GraphicsSystem::GraphicsSystem(Settings& settings, const SceneSystem& scene, const ModelCatalog& models)
        : m_settings(settings)
        , m_scene(scene)
        , m_models(models)
        , m_render(MakeUnique<render::Render>())
        , m_meshes(m_render->Resources())
    {
        // Registered before or after .settings is read, the keys get the file's values
        m_culling.RegisterSettings(m_settings);
    }

    GraphicsSystem::~GraphicsSystem()
    {
        Shutdown();
    }

    auto GraphicsSystem::Init(const render::NativeWindow& window, Size surfaceSize, Size viewSize) -> EngineResult<void>
    {
        if (auto renderInit = m_render->Init(window, surfaceSize); !renderInit)
            return renderInit;
        m_viewPort = render::ViewPort(m_render->Resources(), viewSize);
        m_viewCamera.SetAspect(static_cast<float>(viewSize.width) / static_cast<float>(viewSize.height));
        m_culling.Init(m_render->Resources(), m_settings);
        m_initialized = true;
        m_renderThread = std::thread(&GraphicsSystem::RenderThreadFunc, this);
        return { };
    }

    void GraphicsSystem::StopRendering()
    {
        m_render->StopRendering();
        if (m_renderThread.joinable())
            m_renderThread.join();
    }

    void GraphicsSystem::Shutdown()
    {
        if (!m_initialized)
            return;
        // The render thread is stopped before the resources it may still use go
        StopRendering();
        m_meshes.Clear();
        m_culling.Shutdown();
        m_viewPort = { };
        m_render->Shutdown();
        m_initialized = false;
    }

    void GraphicsSystem::Cull()
    {
        const Matrix4x4 cullingViewProjection = m_viewCamera.GetCullingViewProjection();
        m_culling.ExecuteCulling(m_scene.GetScene(), m_models, cullingViewProjection, m_settings);
    }

    render::FrameWriter GraphicsSystem::BeginFrame()
    {
        return m_render->BeginFrame();
    }

    void GraphicsSystem::DrawWorld(render::FrameWriter& frame, Size backbuffer)
    {
        ELM_PROFILE_SCOPE_N("Build Frame Snapshot");
        frame.SetBackbufferSize(backbuffer);
        const render::CameraData camera { m_viewCamera.GetViewProjectionMatrix(), m_viewCamera.GetPosition() };
        frame.BeginSceneView(camera, m_viewPort.GetColorTexture(), m_viewPort.GetSize());
        m_meshes.Sync(m_models);
        SubmitModels(frame);
    }

    void GraphicsSystem::PresentToWindow(render::FrameWriter& frame, Size framebuffer)
    {
        FitGameView(framebuffer, m_viewPort, m_viewCamera);
        WriteGameOverlay(m_viewPort, framebuffer, frame.Overlay());
    }

    void GraphicsSystem::EndFrame(render::FrameWriter& frame)
    {
        m_render->SubmitFrame(frame);
    }

    void GraphicsSystem::RenderThreadFunc()
    {
        ELM_PROFILE_THREAD("Render Thread");
        core::registerThread(core::ThreadRole::Render);

        render::RenderedFrameInfo info;
        while (m_render->RenderNextFrame(info)) {
        }

        core::unregisterThread(core::ThreadRole::Render);
    }

    void GraphicsSystem::SubmitModels(render::FrameWriter& frame)
    {
        const Scene& scene = m_scene.GetScene();
        const auto* drawn = scene.FindComponents<ModelComponent>();
        if (!drawn)
            return;
        const auto ids = drawn->Ids();
        const auto components = drawn->Components();
        const auto worldTransforms = scene.WorldTransforms();
        size_t drawCount = 0;
        for (const auto& component : components)
            drawCount += m_meshes.GetMeshes(component.model).size();
        frame.ReserveDraws(drawCount);
        for (size_t index = 0; index < components.size(); ++index) {
            const ModelComponent& component = components[index];
            const size_t nodeIndex = scene.IndexOf(ids[index]);
            if (!component.visible || nodeIndex == Scene::NoNode)
                continue;
            // A node culling did not see yet (added by the UI, a scene just loaded) is drawn
            const auto* occludee = m_culling.FindOccludee(ids[index]);
            if (occludee && !occludee->isVisible)
                continue;
            const Vector4& tint = occludee ? occludee->color : component.color;
            m_meshes.ForEachSubmesh(m_models, component.model, [&](const Model::Submesh& submesh, render::MeshHandle mesh) {
                const Vector4 color {
                    submesh.color.x * tint.x,
                    submesh.color.y * tint.y,
                    submesh.color.z * tint.z,
                    submesh.color.w * tint.w
                };
                frame.Draw(mesh, worldTransforms[nodeIndex], color);
            });
        }
    }

} // namespace elm::graphics
