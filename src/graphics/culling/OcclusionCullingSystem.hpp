#pragma once

#include "core/Std.hpp"

#include "render/api/RenderResources.hpp"

#include "graphics/culling/MathTypes.hpp"
#include "graphics/culling/SoftwareDepthBuffer.hpp"
#include "graphics/Settings.hpp"


#include "Scene/TestScenes.hpp"

namespace elm {

enum class VisualMode {
    HideCulled,
    HighlightCulled,
    OccludersOnly
};

struct CullingStats {
    uint32_t totalObjects { 0 };
    uint32_t visibleCount { 0 };
    uint32_t frustumCulledCount { 0 };
    uint32_t occlusionCulledCount { 0 };
    float rasterizeTimeUs { 0.0f };
    float queryTimeUs { 0.0f };
    float totalCullingTimeUs { 0.0f };
    float cullingRatioPercent { 0.0f };
};

struct OccludeeInstance {
    AABB localBounds;
    Matrix4x4 worldTransform;
    Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
    bool isVisible { true };
    bool isFrustumCulled { false };
    bool isOcclusionCulled { false };
};

class OcclusionCullingSystem {
public:
    static constexpr StringView ResolutionWidthSetting = "culling/Resolution/Width";
    static constexpr StringView ResolutionHeightSetting = "culling/Resolution/Height";
    static constexpr StringView EnableFrustumCullingSetting = "culling/EnableFrustumCulling";
    static constexpr StringView EnableOcclusionCullingSetting = "culling/EnableOcclusionCulling";
    static constexpr StringView DepthBiasSetting = "culling/DepthBias";
    static constexpr StringView VisualModeSetting = "culling/VisualMode";
    static constexpr StringView DepthFalseColorSetting = "culling/DepthFalseColor";

    OcclusionCullingSystem(uint32_t width = 256, uint32_t height = 144);
    ~OcclusionCullingSystem() = default;

    void Init(render::RenderResources& resources, const Settings& settings);
    void Shutdown();
    void RegisterSettings(Settings& settings) const;
    void SetResolution(uint32_t width, uint32_t height);
    void CreateDepthPreviewTexture(uint32_t width, uint32_t height);
    void UpdateDepthPreviewTexture(const Settings& settings);

    void ExecuteCulling(Scene& scene, const Matrix4x4& cullingViewProj, const Settings& settings);

    [[nodiscard]] const CullingStats& GetStats() const noexcept { return m_stats; }
    [[nodiscard]] const Vector<OccludeeInstance>& GetOccludees() const noexcept { return m_occludees; }
    [[nodiscard]] const render::Texture& GetDepthPreviewTexture() const { return m_depthPreviewTexture; }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_depthBuffer.GetWidth(); }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_depthBuffer.GetHeight(); }
    [[nodiscard]] float GetDepth(uint32_t x, uint32_t y) const { return m_depthBuffer.GetDepth(x, y); }

private:
    void ApplySettings(const Settings& settings);

    // Settings
    bool enableFrustumCulling { true };
    bool enableOcclusionCulling { true };
    bool freezeCullingCamera { false };
    float depthBias { 0.001f };
    VisualMode visualMode { VisualMode::HideCulled };

    SoftwareDepthBuffer m_depthBuffer;
    CullingStats m_stats;
    Vector<OccludeeInstance> m_occludees;
    render::RenderResources* m_resources { nullptr };
    render::Texture m_depthPreviewTexture;
};

} // namespace elm
