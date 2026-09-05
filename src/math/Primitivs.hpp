#pragma once

#include "core/Std.hpp"

#include "math/Vector.hpp"

namespace elm {
	using namespace math;

	struct AABB {
		Vector3 minBounds{ 1e30f, 1e30f, 1e30f };
		Vector3 maxBounds{ -1e30f, -1e30f, -1e30f };

		constexpr AABB() = default;
		constexpr AABB(const Vector3& inMin, const Vector3& inMax) : minBounds(inMin), maxBounds(inMax) {}

		[[nodiscard]] constexpr Vector3 GetCenter() const noexcept {
			return (minBounds + maxBounds) * 0.5f;
		}

		[[nodiscard]] constexpr Vector3 GetExtent() const noexcept {
			return (maxBounds - minBounds) * 0.5f;
		}

		[[nodiscard]] std::array<Vector3, 8> GetCorners() const noexcept {
			return {
				Vector3{minBounds.x, minBounds.y, minBounds.z},
				Vector3{maxBounds.x, minBounds.y, minBounds.z},
				Vector3{minBounds.x, maxBounds.y, minBounds.z},
				Vector3{maxBounds.x, maxBounds.y, minBounds.z},
				Vector3{minBounds.x, minBounds.y, maxBounds.z},
				Vector3{maxBounds.x, minBounds.y, maxBounds.z},
				Vector3{minBounds.x, maxBounds.y, maxBounds.z},
				Vector3{maxBounds.x, maxBounds.y, maxBounds.z}
			};
		}

		[[nodiscard]] AABB Transformed(const Matrix4x4& mat) const noexcept {
			const auto corners = GetCorners();
			AABB result;
			for (const auto& c : corners) {
				const Vector4 transformed = mat.TransformPoint(c);
				const Vector3 p{ transformed.x, transformed.y, transformed.z };
				result.minBounds.x = std::min(result.minBounds.x, p.x);
				result.minBounds.y = std::min(result.minBounds.y, p.y);
				result.minBounds.z = std::min(result.minBounds.z, p.z);
				result.maxBounds.x = std::max(result.maxBounds.x, p.x);
				result.maxBounds.y = std::max(result.maxBounds.y, p.y);
				result.maxBounds.z = std::max(result.maxBounds.z, p.z);
			}
			return result;
		}
	};

	struct Vertex {
		Vector3 position;
		Vector3 normal;
		float u{ 0.0f };
		float v{ 0.0f };
	};

	struct MeshData {
		Vector<Vertex> vertices;
		Vector<uint32_t> indices;
		AABB localBounds;
	};

	class GeometryPrimitives {
	public:
		static MeshData CreateBox(const Vector3& halfExtents) {
			MeshData mesh;
			const float hx = halfExtents.x;
			const float hy = halfExtents.y;
			const float hz = halfExtents.z;

			mesh.localBounds = AABB{ Vector3{-hx, -hy, -hz}, Vector3{hx, hy, hz} };

			// 6 faces * 4 vertices = 24 vertices
			mesh.vertices = {
				// Front face (+Z)
				{{-hx, -hy,  hz}, {0.0f, 0.0f, 1.0f}, 0.0f, 1.0f},
				{{ hx, -hy,  hz}, {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f},
				{{ hx,  hy,  hz}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.0f},
				{{-hx,  hy,  hz}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f},

				// Back face (-Z)
				{{ hx, -hy, -hz}, {0.0f, 0.0f, -1.0f}, 0.0f, 1.0f},
				{{-hx, -hy, -hz}, {0.0f, 0.0f, -1.0f}, 1.0f, 1.0f},
				{{-hx,  hy, -hz}, {0.0f, 0.0f, -1.0f}, 1.0f, 0.0f},
				{{ hx,  hy, -hz}, {0.0f, 0.0f, -1.0f}, 0.0f, 0.0f},

				// Top face (+Y)
				{{-hx,  hy,  hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f},
				{{ hx,  hy,  hz}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f},
				{{ hx,  hy, -hz}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f},
				{{-hx,  hy, -hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f},

				// Bottom face (-Y)
				{{-hx, -hy, -hz}, {0.0f, -1.0f, 0.0f}, 0.0f, 1.0f},
				{{ hx, -hy, -hz}, {0.0f, -1.0f, 0.0f}, 1.0f, 1.0f},
				{{ hx, -hy,  hz}, {0.0f, -1.0f, 0.0f}, 1.0f, 0.0f},
				{{-hx, -hy,  hz}, {0.0f, -1.0f, 0.0f}, 0.0f, 0.0f},

				// Right face (+X)
				{{ hx, -hy,  hz}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
				{{ hx, -hy, -hz}, {1.0f, 0.0f, 0.0f}, 1.0f, 1.0f},
				{{ hx,  hy, -hz}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f},
				{{ hx,  hy,  hz}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.0f},

				// Left face (-X)
				{{-hx, -hy, -hz}, {-1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
				{{-hx, -hy,  hz}, {-1.0f, 0.0f, 0.0f}, 1.0f, 1.0f},
				{{-hx,  hy,  hz}, {-1.0f, 0.0f, 0.0f}, 1.0f, 0.0f},
				{{-hx,  hy, -hz}, {-1.0f, 0.0f, 0.0f}, 0.0f, 0.0f},
			};

			mesh.indices.reserve(36);
			for (uint32_t i = 0; i < 6; ++i) {
				const uint32_t base = i * 4;
				mesh.indices.push_back(base + 0);
				mesh.indices.push_back(base + 1);
				mesh.indices.push_back(base + 2);
				mesh.indices.push_back(base + 0);
				mesh.indices.push_back(base + 2);
				mesh.indices.push_back(base + 3);
			}

			return mesh;
		}

		static MeshData CreateCube(float sideLength = 1.0f) {
			const float h = sideLength * 0.5f;
			return CreateBox(Vector3{ h, h, h });
		}

		static MeshData CreateWall(float width, float height, float thickness) {
			return CreateBox(Vector3{ width * 0.5f, height * 0.5f, thickness * 0.5f });
		}

		static MeshData CreateGroundPlane(float sizeX, float sizeZ) {
			MeshData mesh;
			const float hx = sizeX * 0.5f;
			const float hz = sizeZ * 0.5f;
			mesh.localBounds = AABB{ Vector3{-hx, -0.05f, -hz}, Vector3{hx, 0.05f, hz} };

			mesh.vertices = {
				{{-hx, 0.0f,  hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 10.0f},
				{{ hx, 0.0f,  hz}, {0.0f, 1.0f, 0.0f}, 10.0f, 10.0f},
				{{ hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, 10.0f, 0.0f},
				{{-hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f},
			};

			mesh.indices = { 0, 1, 2, 0, 2, 3 };
			return mesh;
		}
	};

}