#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "core/Timer.hpp"

namespace elm {

	OcclusionCullingSystem::OcclusionCullingSystem(uint32_t width, uint32_t height)
		: m_depthBuffer(width, height) {
	}

	void OcclusionCullingSystem::Init() {
		CreateDepthPreviewTexture(m_depthBuffer.GetWidth(), m_depthBuffer.GetHeight());
	}

	void OcclusionCullingSystem::SetResolution(uint32_t width, uint32_t height) {
		m_depthBuffer.Resize(width, height);
		CreateDepthPreviewTexture(width, height);
	}

	void OcclusionCullingSystem::CreateDepthPreviewTexture(uint32_t width, uint32_t height) {
		Vector<uint32_t> depthPreviewPixels;
		depthPreviewPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000);

		render::TextureInfo texInfo;
		texInfo.name = "Software Depth Buffer Preview Texture";
		texInfo.width = width;
		texInfo.height = height;
		texInfo.format = render::TextureFormat::RGBA8_UNORM;
		texInfo.usage = render::TextureUsage::Default;
		texInfo.bindFlags = render::TextureBindFlags::BindShaderResource;
		texInfo.data = depthPreviewPixels.data();
		texInfo.stride = width * sizeof(uint32_t);

		m_depthPreviewTexture = render::Texture(texInfo);
	}

	void OcclusionCullingSystem::UpdateDepthPreviewTexture(bool falseColor) {
		if (!m_depthPreviewTexture.IsValid()) return;
		Vector<uint32_t> depthPreviewPixels;
		m_depthBuffer.GenerateVisualTexture(depthPreviewPixels, falseColor);
		m_depthPreviewTexture.Update(depthPreviewPixels.data());
	}

	void OcclusionCullingSystem::ExecuteCulling(Scene& scene, const Matrix4x4& cullingViewProj, Vector<OccludeeInstance>& occludees) {
		const auto tStart = core::getTimeStamp();

		occludees.clear();

		// 1. Clear depth buffer
		m_depthBuffer.Clear(1.0f);

		// 2. Rasterize occluders to software depth buffer
		const auto tRasterStart = core::getTimeStamp();
		if (enableOcclusionCulling) {
			for (const auto& inst : scene.instances) {
				const Matrix4x4 wvp = cullingViewProj * inst.worldTransform;
				Vector<Vector3> positions;
				positions.reserve(inst.mesh.vertices.size());
				for (const auto& v : inst.mesh.vertices) {
					positions.push_back(v.position);
				}
				m_depthBuffer.RasterizeMesh(positions, inst.mesh.indices, wvp);
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
			occInst.localBounds = inst.mesh.localBounds;
			occInst.color = inst.color;

			const AABB worldBounds = occInst.localBounds.Transformed(occInst.worldTransform);

			// Frustum Culling
			if (enableFrustumCulling) {
				if (!frustum.IntersectsAABB(worldBounds)) {
					occInst.isFrustumCulled = true;
					occInst.isVisible = false;
					inst.visible = false;
					m_stats.frustumCulledCount++;
					occludees.emplace_back(occInst);
					continue;
				}
			}

			// Software Occlusion Culling
			if (enableOcclusionCulling) {
				if (m_depthBuffer.TestAABB(worldBounds, cullingViewProj, depthBias)) {
					occInst.isOcclusionCulled = true;
					occInst.isVisible = false;
					inst.visible = false;
					m_stats.occlusionCulledCount++;
					occludees.emplace_back(occInst);
					continue;
				}
			}

			occInst.isVisible = true;
			inst.visible = true;
			occludees.emplace_back(occInst);
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
