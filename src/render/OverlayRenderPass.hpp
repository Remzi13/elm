#pragma once

#include "core/Std.hpp"

#include "render/IRenderPass.hpp"
#include "render/SwapChain.hpp"

#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"

namespace Diligent {
struct IRenderDevice;
struct IPipelineState;
}

namespace elm::render {

using RenderSurfaceId = uint32_t;

/// Draws captured overlay viewports. RenderSystem controls when this pass runs.
class OverlayRenderPass final : public IRenderPass {
public:
    OverlayRenderPass() = default;
    ~OverlayRenderPass() override;

    void Initialize(Diligent::IRenderDevice* device, Diligent::TEXTURE_FORMAT colorFormat);
    void ReleaseResources() override;
    
    [[nodiscard]] StringView GetName() const override { return "Overlay"; }
    void Execute(RenderFrameContext& context) override;

private:
    void ReleaseViewportSurfaces();

private:
    Diligent::IPipelineState* m_pOverlayPSO { nullptr };
    UnorderedMap<RenderSurfaceId, SwapChain> m_viewportSurfaces;
};

} // namespace elm::render
