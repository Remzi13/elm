#pragma once

#include "graphics/culling/MathTypes.hpp"
#include "graphics/culling/SoftwareDepthBuffer.hpp"
#include "graphics/GeometryPrimitives.hpp"
#include <vector>
#include <chrono>

namespace elm  {

enum class VisualMode {
    HideCulled,
    HighlightCulled,
    OccludersOnly
};

struct CullingStats {
    uint32_t totalObjects{0};
    uint32_t visibleCount{0};
    uint32_t frustumCulledCount{0};
    uint32_t occlusionCulledCount{0};
    float rasterizeTimeUs{0.0f};
    float queryTimeUs{0.0f};
    float totalCullingTimeUs{0.0f};
    float cullingRatioPercent{0.0f};
};

struct OccluderInstance {
    MeshData mesh;
    Matrix4x4 worldTransform;
};

struct OccludeeInstance {
    uint32_t id{0};
    AABB localBounds;
    Matrix4x4 worldTransform;
    Vector4 color{0.8f, 0.8f, 0.8f, 1.0f};
    bool isVisible{true};
    bool isFrustumCulled{false};
    bool isOcclusionCulled{false};
};

class OcclusionCullingSystem {
public:
    OcclusionCullingSystem(uint32_t width = 256, uint32_t height = 144);
    ~OcclusionCullingSystem() = default;

    void SetResolution(uint32_t width, uint32_t height);

    void ExecuteCulling(const std::vector<OccluderInstance>& occluders,
                        std::vector<OccludeeInstance>& occludees,
                        const Matrix4x4& cullingViewProj);

    [[nodiscard]] const CullingStats& GetStats() const noexcept { return m_stats; }
    [[nodiscard]] const SoftwareDepthBuffer& GetDepthBuffer() const noexcept { return m_depthBuffer; }

    // Settings
    bool enableFrustumCulling{true};
    bool enableOcclusionCulling{true};
    bool freezeCullingCamera{false};
    float depthBias{0.001f};
    VisualMode visualMode{VisualMode::HideCulled};

private:
    SoftwareDepthBuffer m_depthBuffer;
    CullingStats m_stats;
};

} // namespace Engine
