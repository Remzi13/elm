#pragma once


#include <cmath>
#include <algorithm>
#include <cstdint>
#include <array>

namespace math {
	struct Vector3 {
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };

		constexpr Vector3() = default;
		constexpr Vector3( float inX, float inY, float inZ ) : x( inX ), y( inY ), z( inZ ) {}

		[[nodiscard]] constexpr Vector3 operator+( const Vector3& o ) const noexcept {
			return { x + o.x, y + o.y, z + o.z };
		}
		[[nodiscard]] constexpr Vector3 operator-( const Vector3& o ) const noexcept {
			return { x - o.x, y - o.y, z - o.z };
		}
		[[nodiscard]] constexpr Vector3 operator*( float s ) const noexcept {
			return { x * s, y * s, z * s };
		}
		[[nodiscard]] constexpr Vector3 operator/( float s ) const noexcept {
			const float inv = 1.0f / s;
			return { x * inv, y * inv, z * inv };
		}
		constexpr Vector3& operator+=( const Vector3& o ) noexcept {
			x += o.x; y += o.y; z += o.z;
			return *this;
		}
		constexpr Vector3& operator-=( const Vector3& o ) noexcept {
			x -= o.x; y -= o.y; z -= o.z;
			return *this;
		}
		constexpr Vector3& operator*=( float s ) noexcept {
			x *= s; y *= s; z *= s;
			return *this;
		}
		[[nodiscard]] constexpr float Dot( const Vector3& o ) const noexcept {
			return x * o.x + y * o.y + z * o.z;
		}
		[[nodiscard]] constexpr Vector3 Cross( const Vector3& o ) const noexcept {
			return {
				y * o.z - z * o.y,
				z * o.x - x * o.z,
				x * o.y - y * o.x
			};
		}
		[[nodiscard]] float Length() const noexcept {
			return std::sqrt( Dot( *this ) );
		}
		[[nodiscard]] Vector3 Normalized() const noexcept {
			const float len = Length();
			return ( len > 1e-6f ) ? ( *this / len ) : Vector3{ 0.0f, 0.0f, 0.0f };
		}
	};

	struct Quaternion {
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float w{ 1.0f };

		constexpr Quaternion() = default;
		constexpr Quaternion( float inX, float inY, float inZ, float inW ) : x( inX ), y( inY ), z( inZ ), w( inW ) {}
	};

	struct Vector4 {
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float w{ 0.0f };

		constexpr Vector4() = default;
		constexpr Vector4( float inX, float inY, float inZ, float inW ) : x( inX ), y( inY ), z( inZ ), w( inW ) {}
		constexpr Vector4( const Vector3& v, float inW ) : x( v.x ), y( v.y ), z( v.z ), w( inW ) {}

		[[nodiscard]] constexpr Vector4 operator+( const Vector4& o ) const noexcept {
			return { x + o.x, y + o.y, z + o.z, w + o.w };
		}
		[[nodiscard]] constexpr Vector4 operator-( const Vector4& o ) const noexcept {
			return { x - o.x, y - o.y, z - o.z, w - o.w };
		}
		[[nodiscard]] constexpr Vector4 operator*( float s ) const noexcept {
			return { x * s, y * s, z * s, w * s };
		}
		[[nodiscard]] constexpr float Dot( const Vector4& o ) const noexcept {
			return x * o.x + y * o.y + z * o.z + w * o.w;
		}
	};	
}