#pragma once

#include <cstdint>
#include <memory>

namespace elm {

class RenderSystem;

class RenderTexture {
public:
    RenderTexture() = default;
    virtual ~RenderTexture() = default;

    RenderTexture(const RenderTexture&) = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;

    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] void* GetNativeHandle() const noexcept { return m_native; }

protected:
    void SetDescription(uint32_t width, uint32_t height, void* native) noexcept {
        m_width = width;
        m_height = height;
        m_native = native;
    }

private:
    friend class RenderSystem;
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    void* m_native{ nullptr };
};

class RenderResourceManager {
public:
    virtual ~RenderResourceManager() = default;
    [[nodiscard]] virtual RenderTexture* CreateTexture(uint32_t width, uint32_t height) = 0;
    virtual void DestroyTexture(RenderTexture* texture) = 0;
};

} // namespace elm
