#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "core/Timer.hpp"
#include "core/Profiling.hpp"


namespace elm {

OcclusionCullingSystem::OcclusionCullingSystem(uint32_t width, uint32_t height)
    : m_depthBuffer(width, height)
{
}

void OcclusionCullingSystem::RegisterSettings(Settings& settings) const
{
    settings.Register(Settings::Category::Render, ResolutionWidthSetting, "Width", uint32_t{ 256 });
    settings.Register(Settings::Category::Render, ResolutionHeightSetting, "Height", uint32_t{ 144 });
    settings.Register(Settings::Category::Render, EnableFrustumCullingSetting, "Frustum Culling", true);
    settings.Register(Settings::Category::Render, EnableOcclusionCullingSetting, "Occlusion Culling", true);
    settings.Register(Settings::Category::Render, DepthBiasSetting, "Depth Bias", 0.0f);
    settings.Register(Settings::Category::Render, VisualModeSetting, "Visualization Mode", uint32_t{ 0 });
    settings.Register(Settings::Category::Render, DepthFalseColorSetting, "Depth False Color", true);
}

void OcclusionCullingSystem::Init(render::RenderResources& resources, const Settings& settings)
{
    m_resources = &resources;
    ApplySettings(settings);
    if (!m_depthPreviewTexture.IsValid())
        CreateDepthPreviewTexture(m_depthBuffer.GetWidth(), m_depthBuffer.GetHeight());
}

void OcclusionCullingSystem::Shutdown()
{
    m_depthPreviewTexture.Reset();
    m_resources = nullptr;
}

void OcclusionCullingSystem::SetResolution(uint32_t width, uint32_t height)
{
    if (m_depthBuffer.GetWidth() == width && m_depthBuffer.GetHeight() == height)
        return;

    m_depthBuffer.Resize(width, height);
    CreateDepthPreviewTexture(width, height);
}

void OcclusionCullingSystem::ApplySettings(const Settings& settings)
{
    const uint32_t width = settings.Get<uint32_t>(Settings::Category::Render, ResolutionWidthSetting);
    const uint32_t height = settings.Get<uint32_t>(Settings::Category::Render, ResolutionHeightSetting);
    if (width > 0 && height > 0)
        SetResolution(width, height);

    enableFrustumCulling = settings.Get<bool>(Settings::Category::Render, EnableFrustumCullingSetting);
    enableOcclusionCulling = settings.Get<bool>(Settings::Category::Render, EnableOcclusionCullingSetting);
    depthBias = settings.Get<float>(Settings::Category::Render, DepthBiasSetting);
    const auto visualModeValue = settings.Get<uint32_t>(Settings::Category::Render, VisualModeSetting);
    visualMode = visualModeValue <= static_cast<uint32_t>(VisualMode::OccludersOnly)
        ? static_cast<VisualMode>(visualModeValue)
        : VisualMode::HideCulled;
}

void OcclusionCullingSystem::CreateDepthPreviewTexture(uint32_t width, uint32_t height)
{
    Vector<uint8_t> depthPreviewPixels;
    depthPreviewPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t), 0);

    if (!m_resources)
        return;

    render::TextureDesc texInfo;
    texInfo.name = "Software Depth Buffer Preview Texture";
    texInfo.width = width;
    texInfo.height = height;
    texInfo.format = render::TextureFormat::RGBA8_UNORM;
    texInfo.usage = render::ResourceUsage::Default;
    texInfo.bindFlags = render::TextureBind::ShaderResource;

    m_depthPreviewTexture = render::Texture(*m_resources, texInfo);
    m_depthPreviewTexture.Update(render::TextureData(std::move(depthPreviewPixels), width * sizeof(uint32_t)));
}

void OcclusionCullingSystem::UpdateDepthPreviewTexture(const Settings& settings)
{
    if (!m_depthPreviewTexture.IsValid())
        return;
    Vector<uint8_t> depthPreviewPixels;
    m_depthBuffer.GenerateVisualTexture(depthPreviewPixels,
        settings.Get<bool>(Settings::Category::Render, DepthFalseColorSetting));
    m_depthPreviewTexture.Update(render::TextureData(std::move(depthPreviewPixels), m_depthBuffer.GetWidth() * sizeof(uint32_t)));
}

void OcclusionCullingSystem::ExecuteCulling(Scene& scene, const Matrix4x4& cullingViewProj, const Settings& settings)
{
    ELM_PROFILE_SCOPE_N("Execute Culling");
    const auto tStart = core::getTimeStamp();

    ApplySettings(settings);

    m_occludees.clear();

    // 1. Clear depth buffer
    m_depthBuffer.Clear(1.0f);

    // 2. Rasterize occluders to software depth buffer
    const auto tRasterStart = core::getTimeStamp();
    if (enableOcclusionCulling) {
        for (const auto& inst : scene.instances) {
            const Matrix4x4 wvp = cullingViewProj * inst.worldTransform;

            if (!inst.meshData)
                continue;
            const auto& meshData = *inst.meshData;
            Vector<Vector3> positions;
            positions.reserve(meshData.vertices.size());
            for (const auto& v : meshData.vertices) {
                positions.push_back(v.position);
            }
            m_depthBuffer.RasterizeMesh(positions, meshData.indices, wvp);
        }
    }
    const auto tRasterEnd = core::getTimeStamp();

    // 3. Test occludees against frustum and software depth buffer
    const auto tQueryStart = core::getTimeStamp();

    const Frustum frustum = Frustum::FromViewProj(cullingViewProj);

    m_stats.totalObjects = static_cast<uint32_t>(scene.instances.size());
    m_stats.visibleCount = 0;
    m_stats.frustumCulledCount = 0;
    m_stats.occlusionCulledCount = 0;

    for (auto& inst : scene.instances) {

        OccludeeInstance occInst;
        occInst.isFrustumCulled = false;
        occInst.isOcclusionCulled = false;
        occInst.isVisible = true;
        occInst.worldTransform = inst.worldTransform;
        occInst.localBounds = inst.localBounds;
        occInst.color = inst.color;

        const AABB worldBounds = occInst.localBounds.Transformed(occInst.worldTransform);
        auto finishCulling = [&](bool isCulled) {
            if (visualMode == VisualMode::HighlightCulled && isCulled) {
                occInst.isVisible = true;
                occInst.color = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };
            }
            else if (visualMode == VisualMode::OccludersOnly) {
                occInst.isVisible = enableOcclusionCulling;
            }
            inst.visible = occInst.isVisible;
            m_occludees.emplace_back(occInst);
        };

        // Frustum Culling
        if (enableFrustumCulling) {
            if (!frustum.IntersectsAABB(worldBounds)) {
                occInst.isFrustumCulled = true;
                occInst.isVisible = false;
                m_stats.frustumCulledCount++;
                finishCulling(true);
                continue;
            }
        }

        // Software Occlusion Culling
        if (enableOcclusionCulling) {
            if (m_depthBuffer.TestAABB(worldBounds, cullingViewProj, depthBias)) {
                occInst.isOcclusionCulled = true;
                occInst.isVisible = false;
                m_stats.occlusionCulledCount++;
                finishCulling(true);
                continue;
            }
        }

        occInst.isVisible = true;
        finishCulling(false);
        m_stats.visibleCount++;
    }

    const auto tQueryEnd = core::getTimeStamp();
    const auto tEnd = core::getTimeStamp();

    m_stats.rasterizeTimeUs = static_cast<float>(core::getMicroseconds(tRasterStart, tRasterEnd));
    m_stats.queryTimeUs = static_cast<float>(core::getMicroseconds(tQueryStart, tQueryEnd));
    m_stats.totalCullingTimeUs = static_cast<float>(core::getMicroseconds(tStart, tEnd));

    const uint32_t culledTotal = m_stats.frustumCulledCount + m_stats.occlusionCulledCount;
    m_stats.cullingRatioPercent = (m_stats.totalObjects > 0)
        ? (static_cast<float>(culledTotal) / static_cast<float>(m_stats.totalObjects)) * 100.0f
        : 0.0f;
}

} // namespace elm
