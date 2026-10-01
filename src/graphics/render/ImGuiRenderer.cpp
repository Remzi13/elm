#include "graphics/render/ImGuiRenderer.hpp"

#include "graphics/ImGuiSystem.hpp"
#include "graphics/render/RenderResourceProvider.hpp"
#include "graphics/render/SwapChain.hpp"

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "ImGuiDiligentRenderer.hpp"
#include "ImGuiImplDiligent.hpp"
#include "imgui.h"

#include <utility>
#include <vector>

namespace Diligent {

class ImGuiImplDiligentViewport : public ImGuiImplDiligent {
public:
    using ImGuiImplDiligent::ImGuiImplDiligent;

    void SetRenderSurface(Uint32 width, Uint32 height, SURFACE_TRANSFORM transform)
    {
        m_pRenderer->NewFrame(width, height, transform);
    }

    void RenderDrawData(IDeviceContext* context, ImDrawData* drawData)
    {
        m_pRenderer->RenderDrawData(context, drawData);
    }
};

} // namespace Diligent

namespace elm::render {

ImGuiRenderer::ImGuiRenderer(RenderSystem& renderSystem, RenderResourceProvider& resourceProvider)
    : m_renderSystem(renderSystem)
    , m_resourceProvider(resourceProvider)
    , m_mainSwapChain(resourceProvider.GetMainSwapChain())
{
    if (!m_resourceProvider.GetRenderDevice() || !m_resourceProvider.GetDeviceContext() || !m_mainSwapChain)
        return;

    Diligent::ImGuiDiligentCreateInfo createInfo;
    createInfo.pDevice = m_resourceProvider.GetRenderDevice();
    createInfo.BackBufferFmt = m_mainSwapChain->GetDesc().ColorBufferFormat;
    createInfo.DepthBufferFmt = Diligent::TEX_FORMAT_UNKNOWN;
    m_imguiRenderer = std::make_unique<Diligent::ImGuiImplDiligentViewport>(createInfo);
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

bool ImGuiRenderer::IsInitialized() const noexcept
{
    return m_imguiRenderer != nullptr;
}

SwapChain ImGuiRenderer::CreateViewportSwapChain(const ImGuiViewportEvent& event)
{
    return m_renderSystem.CreateSwapChain(event.width, event.height, event.nativeHandle, event.nativeDisplay, false);
}

void ImGuiRenderer::ApplyViewportEvents(const ImGuiFrame& frame, uint64_t& releasedViewportCount)
{
    for (const auto& event : frame.viewportEvents) {
        m_viewportSwapChains.erase(event.id);
        if (event.type == ImGuiViewportEvent::Type::Destroy) {
            ++releasedViewportCount;
            continue;
        }

        auto swapChain = CreateViewportSwapChain(event);
        if (swapChain)
            m_viewportSwapChains.emplace(event.id, std::move(swapChain));
    }
}

void ImGuiRenderer::ResolveTextureIds(ImGuiViewportSnapshot& snapshot)
{
    for (ImDrawList* list : snapshot.drawLists) {
        for (ImDrawCmd& cmd : list->CmdBuffer) {
            const auto value = reinterpret_cast<uintptr_t>(cmd.TextureId);
            if ((value & 1) == 0)
                continue;

            const core::Handler handler(static_cast<core::Handler::ValueType>(value >> 1), core::Handler::Render);
            cmd.TextureId = m_resourceProvider.GetTextureStore().GetTextureView(handler);
            if (!cmd.TextureId)
                cmd.ElemCount = 0;
        }
    }
}

uint64_t ImGuiRenderer::RenderFrame(ImGuiFrame& frame)
{
    if (!m_imguiRenderer || !m_resourceProvider.GetDeviceContext() || !m_mainSwapChain)
        return 0;

    uint64_t releasedViewportCount = 0;
    ApplyViewportEvents(frame, releasedViewportCount);

    const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
    std::vector<SwapChain*> presentList;

    for (auto& snapshot : frame.viewports) {
        SwapChain* viewportSwapChain = nullptr;
        SwapChain* swapChain = nullptr;
        if (snapshot.isMain) {
            swapChain = m_mainSwapChain;
        }
        else if (auto it = m_viewportSwapChains.find(snapshot.id); it != m_viewportSwapChains.end()) {
            viewportSwapChain = &it->second;
            viewportSwapChain->ResizeIfNeeded(snapshot.framebufferWidth, snapshot.framebufferHeight);
            swapChain = viewportSwapChain;
        }
        if (!swapChain)
            continue;

        const auto& description = swapChain->GetDesc();
        auto* renderTarget = swapChain->GetCurrentBackBufferRTV();
        if (!renderTarget)
            continue;

        ResolveTextureIds(snapshot);
        snapshot.drawData.CmdLists = snapshot.drawLists.data();

        m_imguiRenderer->SetRenderSurface(snapshot.framebufferWidth, snapshot.framebufferHeight, description.PreTransform);
        m_resourceProvider.GetDeviceContext()->SetRenderTargets(1, &renderTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (!snapshot.isMain) {
            m_resourceProvider.GetDeviceContext()->ClearRenderTarget(renderTarget, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            presentList.push_back(viewportSwapChain);
        }
        m_imguiRenderer->RenderDrawData(m_resourceProvider.GetDeviceContext(), &snapshot.drawData);
    }

    for (auto* viewportSwapChain : presentList)
        viewportSwapChain->Present();

    return releasedViewportCount;
}

void ImGuiRenderer::ReleaseViewportSwapChains()
{
    m_viewportSwapChains.clear();
}

void ImGuiRenderer::Shutdown()
{
    ReleaseViewportSwapChains();
    m_imguiRenderer.reset();
}

} // namespace elm::render
