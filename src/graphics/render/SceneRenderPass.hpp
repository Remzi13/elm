#pragma once

#include "graphics/Camera.hpp"
#include "graphics/render/IRenderPass.hpp"
#include "graphics/render/ResourceView.hpp"
#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"

namespace Diligent {
struct IBuffer;
struct IRenderDevice;
struct IPipelineState;
struct IShaderResourceBinding;
}

namespace elm {

struct FrameData;
struct RenderObject;

namespace render {

/// Draws the 3D scene (opaque meshes with instancing) into the engine viewport
/// off-screen texture when this pass is executed by the render graph.
class SceneRenderPass final : public IRenderPass {
public:
    SceneRenderPass() = default;
    ~SceneRenderPass();

    SceneRenderPass(const SceneRenderPass&) = delete;
    SceneRenderPass& operator=(const SceneRenderPass&) = delete;

    void Initialize(Diligent::IRenderDevice* device,
        Diligent::IBuffer* dynamicUniformBuffer,
        Diligent::TEXTURE_FORMAT colorFormat,
        Diligent::TEXTURE_FORMAT depthFormat);

    void SetPipelineState(Diligent::IPipelineState* opaquePSO,
        Diligent::IPipelineState* highlightPSO,
        Diligent::IShaderResourceBinding* srb) noexcept;

    [[nodiscard]] StringView GetName() const override { return "Scene"; }
    void Execute(RenderFrameContext& context) override;

    void ReleaseResources() override;
    void ReleasePipelineState();

private:
    void EnsureViewportTarget(RenderPassContext& ctx, const ViewPort::Snapshot& viewPort);
    void DrawObjects(RenderPassContext& ctx,
        const UnorderedMap<core::Handler, Vector<RenderObject>>& objects);

    Diligent::IPipelineState* m_pOpaquePSO { nullptr };
    Diligent::IPipelineState* m_pHighlightPSO { nullptr };
    Diligent::IShaderResourceBinding* m_pSRB { nullptr };

    core::Handler m_viewportColorHandler;
    core::Handler m_viewportDepthHandler;
    ResourceView m_viewportRenderTarget;
    ResourceView m_viewportDepthStencil;
    Size m_viewportTargetSize {};
    bool m_viewportIsShaderResource { false };
};

} // namespace render
} // namespace elm
