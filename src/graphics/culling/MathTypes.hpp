#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Engine {

struct Vector3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vector3() = default;
    constexpr Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

    [[nodiscard]] constexpr Vector3 operator+(const Vector3& o) const noexcept {
        return {x + o.x, y + o.y, z + o.z};
    }
    [[nodiscard]] constexpr Vector3 operator-(const Vector3& o) const noexcept {
        return {x - o.x, y - o.y, z - o.z};
    }
    [[nodiscard]] constexpr Vector3 operator*(float s) const noexcept {
        return {x * s, y * s, z * s};
    }
    [[nodiscard]] constexpr Vector3 operator/(float s) const noexcept {
        const float inv = 1.0f / s;
        return {x * inv, y * inv, z * inv};
    }
    constexpr Vector3& operator+=(const Vector3& o) noexcept {
        x += o.x; y += o.y; z += o.z;
        return *this;
    }
    constexpr Vector3& operator-=(const Vector3& o) noexcept {
        x -= o.x; y -= o.y; z -= o.z;
        return *this;
    }
    constexpr Vector3& operator*=(float s) noexcept {
        x *= s; y *= s; z *= s;
        return *this;
    }
    [[nodiscard]] constexpr float Dot(const Vector3& o) const noexcept {
        return x * o.x + y * o.y + z * o.z;
    }
    [[nodiscard]] constexpr Vector3 Cross(const Vector3& o) const noexcept {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        };
    }
    [[nodiscard]] float Length() const noexcept {
        return std::sqrt(Dot(*this));
    }
    [[nodiscard]] Vector3 Normalized() const noexcept {
        const float len = Length();
        return (len > 1e-6f) ? (*this / len) : Vector3{0.0f, 0.0f, 0.0f};
    }
};

struct Quaternion {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};

    constexpr Quaternion() = default;
    constexpr Quaternion(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
};

struct Vector4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};

    constexpr Vector4() = default;
    constexpr Vector4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
    constexpr Vector4(const Vector3& v, float inW) : x(v.x), y(v.y), z(v.z), w(inW) {}

    [[nodiscard]] constexpr Vector4 operator+(const Vector4& o) const noexcept {
        return {x + o.x, y + o.y, z + o.z, w + o.w};
    }
    [[nodiscard]] constexpr Vector4 operator-(const Vector4& o) const noexcept {
        return {x - o.x, y - o.y, z - o.z, w - o.w};
    }
    [[nodiscard]] constexpr Vector4 operator*(float s) const noexcept {
        return {x * s, y * s, z * s, w * s};
    }
    [[nodiscard]] constexpr float Dot(const Vector4& o) const noexcept {
        return x * o.x + y * o.y + z * o.z + w * o.w;
    }
};

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

    [[nodiscard]] constexpr float operator()(size_t row, size_t col) const noexcept {
        return m[row * 4 + col];
    }
    constexpr float& operator()(size_t row, size_t col) noexcept {
        return m[row * 4 + col];
    }

    [[nodiscard]] Matrix4x4 operator*(const Matrix4x4& o) const noexcept {
        Matrix4x4 res{};
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (size_t k = 0; k < 4; ++k) {
                    sum += (*this)(r, k) * o(k, c);
                }
                res(r, c) = sum;
            }
        }
        return res;
    }

    [[nodiscard]] Vector4 Transform(const Vector4& v) const noexcept {
        return {
            m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w,
            m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7] * v.w,
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11] * v.w,
            m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15] * v.w
        };
    }

    [[nodiscard]] Vector4 TransformPoint(const Vector3& p) const noexcept {
        return Transform(Vector4{p.x, p.y, p.z, 1.0f});
    }

    [[nodiscard]] Matrix4x4 Transposed() const noexcept {
        Matrix4x4 res;
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                res(r, c) = (*this)(c, r);
            }
        }
        return res;
    }

    static Matrix4x4 Translation(const Vector3& t) noexcept {
        Matrix4x4 res = Identity();
        res(0, 3) = t.x;
        res(1, 3) = t.y;
        res(2, 3) = t.z;
        return res;
    }

    static Matrix4x4 Scaling(const Vector3& s) noexcept {
        Matrix4x4 res = Identity();
        res(0, 0) = s.x;
        res(1, 1) = s.y;
        res(2, 2) = s.z;
        return res;
    }

    static Matrix4x4 RotationY(float radians) noexcept {
        Matrix4x4 res = Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        res(0, 0) = c;  res(0, 2) = s;
        res(2, 0) = -s; res(2, 2) = c;
        return res;
    }

    static Matrix4x4 RotationX(float radians) noexcept {
        Matrix4x4 res = Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        res(1, 1) = c; res(1, 2) = -s;
        res(2, 1) = s; res(2, 2) = c;
        return res;
    }

    static Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept {
        const Vector3 f = (target - eye).Normalized();
        const Vector3 r = up.Cross(f).Normalized();
        const Vector3 u = f.Cross(r);

        Matrix4x4 res = Identity();
        res(0, 0) = r.x; res(0, 1) = r.y; res(0, 2) = r.z; res(0, 3) = -r.Dot(eye);
        res(1, 0) = u.x; res(1, 1) = u.y; res(1, 2) = u.z; res(1, 3) = -u.Dot(eye);
        res(2, 0) = f.x; res(2, 1) = f.y; res(2, 2) = f.z; res(2, 3) = -f.Dot(eye);
        res(3, 0) = 0.0f; res(3, 1) = 0.0f; res(3, 2) = 0.0f; res(3, 3) = 1.0f;
        return res;
    }

    // Perspective projection for Vulkan: Z in [0, 1]
    static Matrix4x4 PerspectiveVk(float fovRadians, float aspect, float zNear, float zFar) noexcept {
        const float tanHalfFov = std::tan(fovRadians * 0.5f);
        Matrix4x4 res{};
        res(0, 0) = 1.0f / (aspect * tanHalfFov);
        res(1, 1) = 1.0f / tanHalfFov;
        res(2, 2) = zFar / (zFar - zNear);
        res(2, 3) = -(zFar * zNear) / (zFar - zNear);
        res(3, 2) = 1.0f;
        res(3, 3) = 0.0f;
        return res;
    }
};

struct AABB {
    Vector3 minBounds{1e30f, 1e30f, 1e30f};
    Vector3 maxBounds{-1e30f, -1e30f, -1e30f};

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
            const Vector3 p{transformed.x, transformed.y, transformed.z};
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

struct FrustumPlane {
    Vector3 normal{0.0f, 0.0f, 0.0f};
    float distance{0.0f};

    [[nodiscard]] float SignedDistance(const Vector3& point) const noexcept {
        return normal.Dot(point) + distance;
    }
};

class Frustum {
public:
    std::array<FrustumPlane, 6> planes{}; // Left, Right, Bottom, Top, Near, Far

    // Extract frustum planes from view-projection matrix (Gribb-Hartmann algorithm)
    static Frustum FromViewProj(const Matrix4x4& vp) noexcept {
        Frustum f;

        // Row vectors of VP matrix
        const Vector4 r0{vp(0, 0), vp(0, 1), vp(0, 2), vp(0, 3)};
        const Vector4 r1{vp(1, 0), vp(1, 1), vp(1, 2), vp(1, 3)};
        const Vector4 r2{vp(2, 0), vp(2, 1), vp(2, 2), vp(2, 3)};
        const Vector4 r3{vp(3, 0), vp(3, 1), vp(3, 2), vp(3, 3)};

        auto makePlane = [](const Vector4& v) -> FrustumPlane {
            const Vector3 norm{v.x, v.y, v.z};
            const float len = norm.Length();
            const float invLen = (len > 1e-6f) ? (1.0f / len) : 0.0f;
            return FrustumPlane{norm * invLen, v.w * invLen};
        };

        f.planes[0] = makePlane(r3 + r0); // Left
        f.planes[1] = makePlane(r3 - r0); // Right
        f.planes[2] = makePlane(r3 + r1); // Bottom
        f.planes[3] = makePlane(r3 - r1); // Top
        f.planes[4] = makePlane(r2);      // Near (Vulkan z in [0, 1])
        f.planes[5] = makePlane(r3 - r2); // Far

        return f;
    }

    [[nodiscard]] bool IntersectsAABB(const AABB& aabb) const noexcept {
        const Vector3 center = aabb.GetCenter();
        const Vector3 extent = aabb.GetExtent();

        for (const auto& plane : planes) {
            // Compute projection interval radius of AABB onto plane normal
            const float radius = extent.x * std::abs(plane.normal.x) +
                                 extent.y * std::abs(plane.normal.y) +
                                 extent.z * std::abs(plane.normal.z);

            if (plane.SignedDistance(center) < -radius) {
                return false; // Box is completely behind this plane
            }
        }
        return true;
    }
};

} // namespace Engine
