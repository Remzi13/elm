#pragma once

#include "math/Vector.hpp"

namespace math {
	struct Matrix4x4 {
		// Stored as 16 floats. For CPU-side math, we use row-major arrays [row][col] or linear array m[16].
		// m[row * 4 + col]
		std::array<float, 16> m{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		static constexpr Matrix4x4 Identity() noexcept {
			return Matrix4x4{};
		}

		[[nodiscard]] constexpr float operator()( size_t row, size_t col ) const noexcept {
			return m[row * 4 + col];
		}
		constexpr float& operator()( size_t row, size_t col ) noexcept {
			return m[row * 4 + col];
		}

		[[nodiscard]] Matrix4x4 operator*( const Matrix4x4& o ) const noexcept {
			Matrix4x4 res{};
			for ( size_t r = 0; r < 4; ++r ) {
				for ( size_t c = 0; c < 4; ++c ) {
					float sum = 0.0f;
					for ( size_t k = 0; k < 4; ++k ) {
						sum += ( *this )( r, k ) * o( k, c );
					}
					res( r, c ) = sum;
				}
			}
			return res;
		}

		[[nodiscard]] Vector4 Transform( const Vector4& v ) const noexcept {
			return {
				m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w,
				m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7] * v.w,
				m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11] * v.w,
				m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15] * v.w
			};
		}

		[[nodiscard]] Vector4 TransformPoint( const Vector3& p ) const noexcept {
			return Transform( Vector4{ p.x, p.y, p.z, 1.0f } );
		}

		[[nodiscard]] Matrix4x4 Transposed() const noexcept {
			Matrix4x4 res;
			for ( size_t r = 0; r < 4; ++r ) {
				for ( size_t c = 0; c < 4; ++c ) {
					res( r, c ) = ( *this )( c, r );
				}
			}
			return res;
		}

		static Matrix4x4 Translation( const Vector3& t ) noexcept {
			Matrix4x4 res = Identity();
			res( 0, 3 ) = t.x;
			res( 1, 3 ) = t.y;
			res( 2, 3 ) = t.z;
			return res;
		}

		static Matrix4x4 Scaling( const Vector3& s ) noexcept {
			Matrix4x4 res = Identity();
			res( 0, 0 ) = s.x;
			res( 1, 1 ) = s.y;
			res( 2, 2 ) = s.z;
			return res;
		}

		static Matrix4x4 RotationY( float radians ) noexcept {
			Matrix4x4 res = Identity();
			const float c = std::cos( radians );
			const float s = std::sin( radians );
			res( 0, 0 ) = c;  res( 0, 2 ) = s;
			res( 2, 0 ) = -s; res( 2, 2 ) = c;
			return res;
		}

		static Matrix4x4 RotationX( float radians ) noexcept {
			Matrix4x4 res = Identity();
			const float c = std::cos( radians );
			const float s = std::sin( radians );
			res( 1, 1 ) = c; res( 1, 2 ) = -s;
			res( 2, 1 ) = s; res( 2, 2 ) = c;
			return res;
		}

		static Matrix4x4 LookAt( const Vector3& eye, const Vector3& target, const Vector3& up ) noexcept {
			const Vector3 f = ( target - eye ).Normalized();
			const Vector3 r = up.Cross( f ).Normalized();
			const Vector3 u = f.Cross( r );

			Matrix4x4 res = Identity();
			res( 0, 0 ) = r.x; res( 0, 1 ) = r.y; res( 0, 2 ) = r.z; res( 0, 3 ) = -r.Dot( eye );
			res( 1, 0 ) = u.x; res( 1, 1 ) = u.y; res( 1, 2 ) = u.z; res( 1, 3 ) = -u.Dot( eye );
			res( 2, 0 ) = f.x; res( 2, 1 ) = f.y; res( 2, 2 ) = f.z; res( 2, 3 ) = -f.Dot( eye );
			res( 3, 0 ) = 0.0f; res( 3, 1 ) = 0.0f; res( 3, 2 ) = 0.0f; res( 3, 3 ) = 1.0f;
			return res;
		}

		// Perspective projection for Vulkan: Z in [0, 1]
		static Matrix4x4 PerspectiveVk( float fovRadians, float aspect, float zNear, float zFar ) noexcept {
			const float tanHalfFov = std::tan( fovRadians * 0.5f );
			Matrix4x4 res{};
			res( 0, 0 ) = 1.0f / ( aspect * tanHalfFov );
			res( 1, 1 ) = 1.0f / tanHalfFov;
			res( 2, 2 ) = zFar / ( zFar - zNear );
			res( 2, 3 ) = -( zFar * zNear ) / ( zFar - zNear );
			res( 3, 2 ) = 1.0f;
			res( 3, 3 ) = 0.0f;
			return res;
		}
	};

}