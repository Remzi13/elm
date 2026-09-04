#pragma once

#include "core/Std.hpp"

#include "graphics/culling/MathTypes.hpp"

#include <cstdint>

namespace elm {

	class SoftwareDepthBuffer {
	public:
		SoftwareDepthBuffer(uint32_t width = 256, uint32_t height = 144);
		~SoftwareDepthBuffer() = default;

		void Resize(uint32_t width, uint32_t height);
		void Clear(float clearDepth = 1.0f);

		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }
		[[nodiscard]] const Vector<float>& GetBuffer() const noexcept { return m_depthBuffer; }
		[[nodiscard]] float GetDepth(uint32_t x, uint32_t y) const noexcept {
			if (x < m_width && y < m_height) {
				return m_depthBuffer[y * m_width + x];
			}
			return 1.0f;
		}

		// Rasterizes a triangle in clip space coordinates (Vulkan clip space: x,y in [-w, w], z in [0, w])
		void RasterizeTriangleClipSpace(const Vector4& p0, const Vector4& p1, const Vector4& p2);

		// Rasterizes an indexed mesh given vertices and indices transformed by World-View-Projection matrix
		void RasterizeMesh(const Vector<Vector3>& positions,
			const Vector<uint32_t>& indices,
			const Matrix4x4& worldViewProj);

		// Conservative AABB occlusion query. Returns true if AABB is fully occluded.
		[[nodiscard]] bool TestAABB(const AABB& worldAABB, const Matrix4x4& viewProj, float depthBias = 0.001f) const;

		// Generates an RGBA8 texture image for visual debug display in ImGui
		void GenerateVisualTexture(Vector<uint32_t>& outRgba, bool falseColor = true) const;

	private:
		void RasterizeTriangleScreen(const Vector3& s0, const Vector3& s1, const Vector3& s2,
			float invW0, float invW1, float invW2);

		uint32_t m_width{ 256 };
		uint32_t m_height{ 144 };
		Vector<float> m_depthBuffer;
	};

} // namespace Engine
