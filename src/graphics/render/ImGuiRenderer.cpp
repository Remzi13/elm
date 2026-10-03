#include "graphics/render/ImGuiRenderer.hpp"

#include "graphics/ImGuiSystem.hpp"
#include "graphics/RenderSystem.hpp"
#include "graphics/render/Command.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/TextureManager.hpp"

#include "imgui.h"

#include <algorithm>
#include <utility>

namespace elm::render {

namespace {

    core::Handler GetTextureHandler(ImTextureID textureId, core::Handler fallback)
    {
        const auto value = reinterpret_cast<uintptr_t>(textureId);
        if ((value & 1) == 0)
            return fallback;
        return core::Handler(static_cast<core::Handler::ValueType>(value >> 1), core::Handler::Render);
    }

    RenderVertex ConvertVertex(const ImDrawVert& vertex, const ImVec2& displayPosition, const ImVec2& displaySize)
    {
        const auto color = static_cast<uint32_t>(vertex.col);
        constexpr float colorScale = 1.0f / 255.0f;
        return {
            {
                ((vertex.pos.x - displayPosition.x) / displaySize.x) * 2.0f - 1.0f,
                1.0f - ((vertex.pos.y - displayPosition.y) / displaySize.y) * 2.0f
            },
            { vertex.uv.x, vertex.uv.y },
            {
                static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xff) * colorScale,
                static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xff) * colorScale,
                static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xff) * colorScale,
                static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xff) * colorScale
            }
        };
    }

    void AppendDrawCommands(CommandList& commands, RenderSurfaceId surface,
        const ImGuiViewportSnapshot& snapshot, core::Handler fallbackTexture)
    {
        const auto& drawData = snapshot.drawData;
        const float displayWidth = drawData.DisplaySize.x;
        const float displayHeight = drawData.DisplaySize.y;
        if (displayWidth <= 0.0f || displayHeight <= 0.0f)
            return;

        commands.Push(command::BeginRenderPass {
            surface,
            snapshot.framebufferWidth,
            snapshot.framebufferHeight,
            !snapshot.isMain
        });

        for (const ImDrawList* list : snapshot.drawLists) {
            for (const ImDrawCmd& drawCommand : list->CmdBuffer) {
                if (drawCommand.UserCallback || drawCommand.ElemCount == 0)
                    continue;

                const float clipLeft = (drawCommand.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
                const float clipTop = (drawCommand.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
                const float clipRight = (drawCommand.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
                const float clipBottom = (drawCommand.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
                const auto left = static_cast<uint32_t>(std::clamp(clipLeft, 0.0f, static_cast<float>(snapshot.framebufferWidth)));
                const auto top = static_cast<uint32_t>(std::clamp(clipTop, 0.0f, static_cast<float>(snapshot.framebufferHeight)));
                const auto right = static_cast<uint32_t>(std::clamp(clipRight, 0.0f, static_cast<float>(snapshot.framebufferWidth)));
                const auto bottom = static_cast<uint32_t>(std::clamp(clipBottom, 0.0f, static_cast<float>(snapshot.framebufferHeight)));
                if (right <= left || bottom <= top)
                    continue;

                command::DrawIndexed draw;
                draw.surface = surface;
                draw.texture = GetTextureHandler(drawCommand.TextureId, fallbackTexture);
                draw.scissor = { left, top, right, bottom };
                draw.vertices.reserve(static_cast<size_t>(list->VtxBuffer.Size));
                for (const ImDrawVert& vertex : list->VtxBuffer)
                    draw.vertices.push_back(ConvertVertex(vertex, drawData.DisplayPos, drawData.DisplaySize));

                draw.indices.reserve(static_cast<size_t>(drawCommand.ElemCount));
                for (uint32_t index = 0; index < drawCommand.ElemCount; ++index) {
                    const auto sourceIndex = drawCommand.IdxOffset + index;
                    draw.indices.push_back(static_cast<uint32_t>(list->IdxBuffer[sourceIndex]) + drawCommand.VtxOffset);
                }
                commands.Push(std::move(draw));
            }
        }

        commands.Push(command::EndRenderPass { surface, !snapshot.isMain });
    }
}

ImGuiRenderer::ImGuiRenderer()
{
    InitializeTextures();
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

bool ImGuiRenderer::InitializeTextures()
{
    auto& textureManager = TextureManager::Get();
    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return false;

    TextureInfo fontInfo;
    fontInfo.name = "ImGui Font Atlas";
    fontInfo.width = static_cast<uint32_t>(width);
    fontInfo.height = static_cast<uint32_t>(height);
    fontInfo.format = TextureFormat::RGBA8_UNORM;
    fontInfo.bindFlags = TextureBindFlags::BindShaderResource;
    m_fontTexture = std::make_unique<Texture>(textureManager.CreateTexture(fontInfo));
    if (!m_fontTexture->IsValid())
        return false;

    TextureData fontData;
    fontData.stride = static_cast<size_t>(width) * 4;
    fontData.data.assign(pixels, pixels + fontData.stride * static_cast<size_t>(height));
    m_fontTexture->Update(fontData);
    io.Fonts->SetTexID(ImGuiSystem::ToTextureId(m_fontTexture->GetHandler()));

    TextureInfo whiteInfo;
    whiteInfo.name = "ImGui White Texture";
    whiteInfo.width = 1;
    whiteInfo.height = 1;
    whiteInfo.format = TextureFormat::RGBA8_UNORM;
    whiteInfo.bindFlags = TextureBindFlags::BindShaderResource;
    m_whiteTexture = std::make_unique<Texture>(textureManager.CreateTexture(whiteInfo));
    if (!m_whiteTexture->IsValid())
        return false;

    TextureData whiteData;
    whiteData.stride = 4;
    whiteData.data = { 0xff, 0xff, 0xff, 0xff };
    m_whiteTexture->Update(whiteData);
    return true;
}

bool ImGuiRenderer::IsInitialized() const noexcept
{
    return m_fontTexture && m_fontTexture->IsValid() &&
        m_whiteTexture && m_whiteTexture->IsValid();
}

uint64_t ImGuiRenderer::RenderFrame(RenderSystem& renderSystem, ImGuiFrame& frame)
{
    if (!IsInitialized())
        return 0;

    CommandList commands;
    uint64_t releasedViewportCount = 0;
    for (const auto& event : frame.viewportEvents) {
        switch (event.type) {
        case ImGuiViewportEvent::Type::Create:
            commands.Push(command::CreateRenderSurface {
                event.id, event.width, event.height, event.nativeHandle, event.nativeDisplay
            });
            m_viewportSurfaceIds.insert(event.id);
            break;
        case ImGuiViewportEvent::Type::Destroy:
            commands.Push(command::DestroyRenderSurface { event.id });
            m_viewportSurfaceIds.erase(event.id);
            ++releasedViewportCount;
            break;
        case ImGuiViewportEvent::Type::Resize:
            commands.Push(command::ResizeRenderSurface { event.id, event.width, event.height });
            break;
        }
    }

    for (const auto& snapshot : frame.viewports) {
        const RenderSurfaceId surface = snapshot.isMain ? 0 : snapshot.id;
        AppendDrawCommands(commands, surface, snapshot, m_whiteTexture->GetHandler());
    }

    renderSystem.ExecuteCommands(commands);
    return releasedViewportCount;
}

void ImGuiRenderer::ReleaseViewportSurfaces(RenderSystem& renderSystem)
{
    CommandList commands;
    for (const auto id : m_viewportSurfaceIds)
        commands.Push(command::DestroyRenderSurface { id });
    m_viewportSurfaceIds.clear();
    renderSystem.ExecuteCommands(commands);
}

void ImGuiRenderer::Shutdown()
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        ImGui::GetIO().Fonts->SetTexID(nullptr);
    m_fontTexture.reset();
    m_whiteTexture.reset();
}

} // namespace elm::render
