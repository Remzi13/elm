#pragma once

#include "math/Vector.hpp"
#include "math/Matrix.hpp"

namespace elm  {

	using namespace math;
	struct Quaternion {
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float w{ 1.0f };

		constexpr Quaternion() = default;
		constexpr Quaternion( float inX, float inY, float inZ, float inW ) : x( inX ), y( inY ), z( inZ ), w( inW ) {}
	};

	struct AABB {
		Vector3 minBounds{ 1e30f, 1e30f, 1e30f };
		Vector3 maxBounds{ -1e30f, -1e30f, -1e30f };

		constexpr AABB() = default;
		constexpr AABB( const Vector3& inMin, const Vector3& inMax ) : minBounds( inMin ), maxBounds( inMax ) {}

		[[nodiscard]] constexpr Vector3 GetCenter() const noexcept {
			return ( minBounds + maxBounds ) * 0.5f;
		}

		[[nodiscard]] constexpr Vector3 GetExtent() const noexcept {
			return ( maxBounds - minBounds ) * 0.5f;
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

		[[nodiscard]] AABB Transformed( const Matrix4x4& mat ) const noexcept {
			const auto corners = GetCorners();
			AABB result;
			for ( const auto& c : corners ) {
				const Vector4 transformed = mat.TransformPoint( c );
				const Vector3 p{ transformed.x, transformed.y, transformed.z };
				result.minBounds.x = std::min( result.minBounds.x, p.x );
				result.minBounds.y = std::min( result.minBounds.y, p.y );
				result.minBounds.z = std::min( result.minBounds.z, p.z );
				result.maxBounds.x = std::max( result.maxBounds.x, p.x );
				result.maxBounds.y = std::max( result.maxBounds.y, p.y );
				result.maxBounds.z = std::max( result.maxBounds.z, p.z );
			}
			return result;
		}
	};

	struct FrustumPlane {
		Vector3 normal{ 0.0f, 0.0f, 0.0f };
		float distance{ 0.0f };

		[[nodiscard]] float SignedDistance( const Vector3& point ) const noexcept {
			return normal.Dot( point ) + distance;
		}
	};

	class Frustum {
	public:
		std::array<FrustumPlane, 6> planes{}; // Left, Right, Bottom, Top, Near, Far

		// Extract frustum planes from view-projection matrix (Gribb-Hartmann algorithm)
		static Frustum FromViewProj( const Matrix4x4& vp ) noexcept {
			Frustum f;

			// Row vectors of VP matrix
			const Vector4 r0{ vp( 0, 0 ), vp( 0, 1 ), vp( 0, 2 ), vp( 0, 3 ) };
			const Vector4 r1{ vp( 1, 0 ), vp( 1, 1 ), vp( 1, 2 ), vp( 1, 3 ) };
			const Vector4 r2{ vp( 2, 0 ), vp( 2, 1 ), vp( 2, 2 ), vp( 2, 3 ) };
			const Vector4 r3{ vp( 3, 0 ), vp( 3, 1 ), vp( 3, 2 ), vp( 3, 3 ) };

			auto makePlane = []( const Vector4& v ) -> FrustumPlane {
				const Vector3 norm{ v.x, v.y, v.z };
				const float len = norm.Length();
				const float invLen = ( len > 1e-6f ) ? ( 1.0f / len ) : 0.0f;
				return FrustumPlane{ norm * invLen, v.w * invLen };
				};

			f.planes[0] = makePlane( r3 + r0 ); // Left
			f.planes[1] = makePlane( r3 - r0 ); // Right
			f.planes[2] = makePlane( r3 + r1 ); // Bottom
			f.planes[3] = makePlane( r3 - r1 ); // Top
			f.planes[4] = makePlane( r2 );      // Near (Vulkan z in [0, 1])
			f.planes[5] = makePlane( r3 - r2 ); // Far

			return f;
		}

		[[nodiscard]] bool IntersectsAABB( const AABB& aabb ) const noexcept {
			const Vector3 center = aabb.GetCenter();
			const Vector3 extent = aabb.GetExtent();

			for ( const auto& plane : planes ) {
				// Compute projection interval radius of AABB onto plane normal
				const float radius = extent.x * std::abs( plane.normal.x ) +
					extent.y * std::abs( plane.normal.y ) +
					extent.z * std::abs( plane.normal.z );

				if ( plane.SignedDistance( center ) < -radius ) {
					return false; // Box is completely behind this plane
				}
			}
			return true;
		}
	};

} // namespace Engine
