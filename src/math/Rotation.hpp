#pragma once

#include "math/Matrix.hpp"
#include "math/Vector.hpp"

#include <optional>

namespace math {

    // Column vectors (p' = M * p), a transform is T * R * S. Angles are in radians.
    // Euler angles: pitch around X, yaw around Y, roll around Z, applied as yaw * pitch * roll.

    [[nodiscard]] Quaternion Multiply(const Quaternion& a, const Quaternion& b) noexcept;
    [[nodiscard]] Quaternion Conjugate(const Quaternion& q) noexcept;
    [[nodiscard]] Quaternion Normalize(const Quaternion& q) noexcept;
    [[nodiscard]] float Dot(const Quaternion& a, const Quaternion& b) noexcept;
    /// Spherical interpolation along the shorter arc.
    [[nodiscard]] Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept;
    [[nodiscard]] Vector3 Rotate(const Quaternion& q, const Vector3& v) noexcept;

    [[nodiscard]] Quaternion QuaternionFromAxisAngle(const Vector3& axis, float radians) noexcept;
    [[nodiscard]] Quaternion QuaternionFromEuler(const Vector3& pitchYawRoll) noexcept;
    [[nodiscard]] Vector3 ToEuler(const Quaternion& q) noexcept;

    [[nodiscard]] Matrix4x4 RotationMatrix(const Quaternion& q) noexcept;
    /// Rotation of the upper 3x3 block; the columns may carry scale, it is divided out.
    [[nodiscard]] Quaternion QuaternionFromMatrix(const Matrix4x4& m) noexcept;

    [[nodiscard]] Matrix4x4 ComposeTRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) noexcept;

    struct DecomposedTRS {
        Vector3 translation;
        Quaternion rotation;
        Vector3 scale { 1.0f, 1.0f, 1.0f };
    };
    /// Inverse of ComposeTRS for matrices without shear or projection.
    [[nodiscard]] DecomposedTRS DecomposeTRS(const Matrix4x4& m) noexcept;

    /// Gauss-Jordan inverse, std::nullopt for a singular matrix.
    [[nodiscard]] std::optional<Matrix4x4> Inverse(const Matrix4x4& m) noexcept;

} // namespace math
