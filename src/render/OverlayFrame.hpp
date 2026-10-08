#pragma once

#include "core/Std.hpp"

#include "render/api/Handles.hpp"
#include "render/api/NativeWindow.hpp"

namespace elm::render {

struct OverlaySurfaceEvent {
    enum class Type {
        Create,
        Destroy,
        Resize
    };

    Type type { Type::Create };
    uint32_t id { 0 };
    void* nativeHandle { nullptr };
    void* nativeDisplay { nullptr };
    NativeWindow::Platform nativePlatform { NativeWindow::Platform::Unknown };
    uint32_t width { 1 };
    uint32_t height { 1 };
};

struct OverlayVertex {
    float position[2];
    float uv[2];
    float color[4];
};

struct OverlayDrawCommand {
    float clipRect[4];
    uint32_t indexOffset { 0 };
    uint32_t vertexOffset { 0 };
    uint32_t elementCount { 0 };
    TextureHandle texture;
    bool hasUserCallback { false };
};

struct OverlayDrawList {
    Vector<OverlayVertex> vertices;
    Vector<uint32_t> indices;
    Vector<OverlayDrawCommand> commands;
};

struct OverlayViewport {
    uint32_t id { 0 };
    bool isMain { false };
    uint32_t framebufferWidth { 1 };
    uint32_t framebufferHeight { 1 };
    Vector<OverlayDrawList> drawLists;
};

/// Engine-owned overlay data captured by the UI integration and consumed by render passes.
struct OverlayFrame {
    Vector<OverlaySurfaceEvent> surfaceEvents;
    Vector<OverlayViewport> viewports;
    TextureHandle fallbackTexture;

    void Clear()
    {
        surfaceEvents.clear();
        viewports.clear();
        fallbackTexture = {};
    }
};

} // namespace elm::render
