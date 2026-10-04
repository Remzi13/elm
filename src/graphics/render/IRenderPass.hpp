#pragma once

#include "core/Handler.hpp"
#include "core/Std.hpp"
#include "graphics/render/ViewPort.hpp"

namespace Diligent {
struct IDeviceContext;
}

namespace elm {

struct FrameData;
struct ImGuiFrame;

namespace render {

class BufferManager;
class DynamicLinearAllocator;
class MeshManager;
class RenderResourceProvider;
class SwapChain;

/// GPU context and shared resources available to every render pass.
struct RenderPassContext {
    Diligent::IDeviceContext* deviceContext = nullptr;
    RenderResourceProvider* resourceProvider = nullptr;
    BufferManager* bufferManager = nullptr;
    MeshManager* meshManager = nullptr;
    DynamicLinearAllocator* dynamicInstanceBuffer = nullptr;
    DynamicLinearAllocator* dynamicUniformBuffer = nullptr;
    SwapChain* mainSwapChain = nullptr;
};

struct RenderFrameContext {
    RenderPassContext& resources;
    FrameData& frameData;
    const ViewPort::Snapshot& viewPort;
    ImGuiFrame* uiFrame = nullptr;
    core::Handler fallbackTexture;
};

/// A render pass draws directly from frame data and shared GPU resources.
/// RenderSystem owns pass lifetime and execution order; passes do not enqueue draw commands.
class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    [[nodiscard]] virtual StringView GetName() const = 0;
    virtual void ReleaseResources() = 0;
    virtual void Execute(RenderFrameContext& context) = 0;
};

} // namespace render
} // namespace elm
