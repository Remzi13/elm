#include "graphics/culling/OcclusionCullingSystem.hpp"

#include <chrono>

namespace elm {

	OcclusionCullingSystem::OcclusionCullingSystem(uint32_t width, uint32_t height)
		: m_depthBuffer(width, height) {
	}

	void OcclusionCullingSystem::SetResolution(uint32_t width, uint32_t height) {
		m_depthBuffer.Resize(width, height);
	}

	void OcclusionCullingSystem::ExecuteCulling(const Scene& scene, const Matrix4x4& cullingViewProj, Vector<OccludeeInstance>& occludees) {
		const auto tStart = std::chrono::high_resolution_clock::now();

		occludees.clear();

		// 1. Clear depth buffer
		m_depthBuffer.Clear(1.0f);

		// 2. Rasterize occluders to software depth buffer
		const auto tRasterStart = std::chrono::high_resolution_clock::now();
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
		const auto tRasterEnd = std::chrono::high_resolution_clock::now();

		// 3. Test occludees against frustum and software depth buffer
		const auto tQueryStart = std::chrono::high_resolution_clock::now();

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
					m_stats.occlusionCulledCount++;
					occludees.emplace_back(occInst);
					continue;
				}
			}

			occInst.isVisible = true;
			occludees.emplace_back(occInst);
			m_stats.visibleCount++;
		}

		const auto tQueryEnd = std::chrono::high_resolution_clock::now();
		const auto tEnd = std::chrono::high_resolution_clock::now();

		m_stats.rasterizeTimeUs = std::chrono::duration<float, std::micro>(tRasterEnd - tRasterStart).count();
		m_stats.queryTimeUs = std::chrono::duration<float, std::micro>(tQueryEnd - tQueryStart).count();
		m_stats.totalCullingTimeUs = std::chrono::duration<float, std::micro>(tEnd - tStart).count();

		const uint32_t culledTotal = m_stats.frustumCulledCount + m_stats.occlusionCulledCount;
		m_stats.cullingRatioPercent = (m_stats.totalObjects > 0)
			? (static_cast<float>(culledTotal) / static_cast<float>(m_stats.totalObjects)) * 100.0f
			: 0.0f;
	}

} // namespace Engine
