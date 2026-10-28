#include "graphics/GameView.hpp"

#include <iterator>
#include <utility>

namespace elm::graphics {

    namespace {

        /// Positions in normalized device coordinates, the texture's top left at the top left of the window.
        constexpr render::OverlayVertex FullScreenQuad[] = {
            { { -1.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { 1.0f, -1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { -1.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        };
        constexpr uint32_t FullScreenIndices[] = { 0, 1, 2, 0, 2, 3 };

    } // namespace

    void FitGameView(Size framebuffer, render::ViewPort& viewPort, Camera& camera)
    {
        if (framebuffer.IsEmpty())
            return;
        viewPort.SetSize(framebuffer);
        camera.SetAspect(static_cast<float>(framebuffer.width) / static_cast<float>(framebuffer.height));
    }

    void WriteGameOverlay(const render::ViewPort& viewPort, Size framebuffer, render::OverlayFrame& overlay)
    {
        overlay.Clear();
        if (framebuffer.IsEmpty() || !viewPort.IsValid())
            return;
        render::OverlayDrawList list;
        list.vertices.assign(std::begin(FullScreenQuad), std::end(FullScreenQuad));
        list.indices.assign(std::begin(FullScreenIndices), std::end(FullScreenIndices));
        render::OverlayDrawCommand command;
        command.clipRect[0] = 0.0f;
        command.clipRect[1] = 0.0f;
        command.clipRect[2] = static_cast<float>(framebuffer.width);
        command.clipRect[3] = static_cast<float>(framebuffer.height);
        command.elementCount = static_cast<uint32_t>(list.indices.size());
        command.texture = viewPort.GetColorTexture();
        list.commands.push_back(command);

        render::OverlayViewport main;
        main.isMain = true;
        main.framebufferWidth = framebuffer.width;
        main.framebufferHeight = framebuffer.height;
        main.drawLists.push_back(std::move(list));
        overlay.viewports.push_back(std::move(main));
    }

} // namespace elm::graphics
