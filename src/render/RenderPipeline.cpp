#include "render/RenderPipeline.hpp"

#include "core/Log.hpp"
#include "core/Profiling.hpp"

#include "render/frame/FrameSnapshot.hpp"
#include "render/passes/OverlayPass.hpp"
#include "render/passes/ScenePass.hpp"
#include "render/rhi/IRenderBackend.hpp"

namespace elm::render {

RenderPipeline::RenderPipeline() = default;
RenderPipeline::~RenderPipeline() = default;

bool RenderPipeline::Init(rhi::IRenderBackend& backend)
{
    m_uploads = MakeUnique<rhi::UploadAllocator>();

    // Order of features is the order their passes are declared in
    m_features.push_back(MakeUnique<ScenePass>());
    m_features.push_back(MakeUnique<OverlayPass>());

    bool result = true;
    for (const auto& feature : m_features) {
        if (!feature->Init(backend)) {
            ERROR_MESSAGE(log::Category::Render, "RenderPipeline", "Failed to initialize feature '%s'", String(feature->GetName()).c_str());
            result = false;
        }
    }
    return result;
}

void RenderPipeline::Release(rhi::IRenderBackend& backend)
{
    m_graph.Reset();
    m_transientTextures.Clear(backend);
    for (const auto& feature : m_features)
        feature->Release(backend);
    m_features.clear();
    m_uploads.reset();
}

void RenderPipeline::Render(rhi::IRenderBackend& backend, const FrameSnapshot& frame)
{
    if (!m_uploads)
        return;

    m_graph.Reset();
    m_uploads->Reset();

    {
        ELM_PROFILE_SCOPE_N("Render Graph Setup");
        FrameBlackboard blackboard;
        for (const auto& feature : m_features)
            feature->Setup(m_graph, frame, backend, blackboard);
    }

    m_graph.Compile();
    m_graph.Execute(backend, m_transientTextures, *m_uploads, frame, m_parallelRecording);
    m_transientTextures.EndFrame(backend);
}

} // namespace elm::render
