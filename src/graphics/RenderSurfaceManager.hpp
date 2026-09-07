#pragma once

#include <cstdint>

struct GLFWwindow;

namespace elm {

class RenderSystem;

class RenderViewport {
public:
    RenderViewport() = default;
    virtual ~RenderViewport() = default;

    RenderViewport(const RenderViewport&) = delete;
    RenderViewport& operator=(const RenderViewport&) = delete;

    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }

protected:
    void SetDescription(uint32_t width, uint32_t height) noexcept {
        m_width = width;
        m_height = height;
    }

private:
    friend class RenderSystem;
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint64_t m_id{ 0 };
};

class RenderSurfaceManager {
public:
    virtual ~RenderSurfaceManager() = default;
    [[nodiscard]] virtual RenderViewport* CreateViewport(GLFWwindow* window, uint32_t width, uint32_t height) = 0;
    virtual void DestroyViewport(RenderViewport* viewport) = 0;
    virtual void ResizeViewport(RenderViewport* viewport, uint32_t width, uint32_t height) = 0;
};

} // namespace elm
