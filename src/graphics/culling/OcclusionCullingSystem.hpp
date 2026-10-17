#pragma once

#include "core/Std.hpp"
#include "core/Settings.hpp"

#include "render/api/RenderResources.hpp"

#include "graphics/culling/SoftwareDepthBuffer.hpp"
#include "graphics/ModelManager.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/Scene.hpp"

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

/// Culling result of a scene node that is shown and has a model.
struct OccludeeInstance {
    /// The node's color, red for a culled node in VisualMode::HighlightCulled.
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

    /// Reads the scene only: ModelComponent::visible is the user's flag, the result stays here.
    void ExecuteCulling(const Scene& scene, const graphics::ModelManager& models,
        const Matrix4x4& cullingViewProj, const Settings& settings);

    [[nodiscard]] const CullingStats& GetStats() const noexcept { return m_stats; }
    /// Result of the last ExecuteCulling for a node; nullptr for a node it did not see (hidden, without
    /// a model, added or removed since).
    [[nodiscard]] const OccludeeInstance* FindOccludee(NodeId node) const noexcept { return m_occludees.Find(node); }
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
    /// Same sparse set by NodeId as scene components: a stale id finds nothing.
    ComponentStore<OccludeeInstance> m_occludees;
    render::RenderResources* m_resources { nullptr };
    render::Texture m_depthPreviewTexture;
};

} // namespace elm
