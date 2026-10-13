#include "math/Rotation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace math {

    Quaternion Multiply(const Quaternion& a, const Quaternion& b) noexcept
    {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    }

    Quaternion Conjugate(const Quaternion& q) noexcept
    {
        return { -q.x, -q.y, -q.z, q.w };
    }

    float Dot(const Quaternion& a, const Quaternion& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    Quaternion Normalize(const Quaternion& q) noexcept
    {
        const float length = std::sqrt(Dot(q, q));
        if (length <= 1e-8f)
            return { };
        const float inverse = 1.0f / length;
        return { q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse };
    }

    Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        Quaternion end = b;
        float cosine = Dot(a, b);
        if (cosine < 0.0f) {
            end = { -b.x, -b.y, -b.z, -b.w };
            cosine = -cosine;
        }
        float fromWeight = 1.0f - t;
        float toWeight = t;
        // Nearly parallel: the linear blend is accurate and avoids dividing by sin(0)
        if (cosine < 0.9995f) {
            const float angle = std::acos(cosine);
            const float sine = std::sin(angle);
            fromWeight = std::sin(fromWeight * angle) / sine;
            toWeight = std::sin(toWeight * angle) / sine;
        }
        return Normalize({
            a.x * fromWeight + end.x * toWeight,
            a.y * fromWeight + end.y * toWeight,
            a.z * fromWeight + end.z * toWeight,
            a.w * fromWeight + end.w * toWeight
        });
    }

    Vector3 Rotate(const Quaternion& q, const Vector3& v) noexcept
    {
        // v' = v + 2w (u x v) + 2 u x (u x v), u = vector part
        const Vector3 u { q.x, q.y, q.z };
        const Vector3 t = u.Cross(v) * 2.0f;
        return v + t * q.w + u.Cross(t);
    }

    Quaternion QuaternionFromAxisAngle(const Vector3& axis, float radians) noexcept
    {
        const Vector3 n = axis.Normalized();
        const float half = radians * 0.5f;
        const float sine = std::sin(half);
        return { n.x * sine, n.y * sine, n.z * sine, std::cos(half) };
    }

    Quaternion QuaternionFromEuler(const Vector3& pitchYawRoll) noexcept
    {
        const Quaternion pitch = QuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, pitchYawRoll.x);
        const Quaternion yaw = QuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, pitchYawRoll.y);
        const Quaternion roll = QuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, pitchYawRoll.z);
        return Multiply(Multiply(yaw, pitch), roll);
    }

    Vector3 ToEuler(const Quaternion& q) noexcept
    {
        // R = Ry * Rx * Rz: element (1,2) = -sin(pitch)
        const Matrix4x4 m = RotationMatrix(Normalize(q));
        const float sinPitch = std::clamp(-m(1, 2), -1.0f, 1.0f);
        const float pitch = std::asin(sinPitch);
        if (std::abs(sinPitch) < 0.9999f)
            return { pitch, std::atan2(m(0, 2), m(2, 2)), std::atan2(m(1, 0), m(1, 1)) };
        // Gimbal lock: yaw and roll turn around the same axis, all of it goes to yaw
        return { pitch, std::atan2(-m(2, 0), m(0, 0)), 0.0f };
    }

    Matrix4x4 RotationMatrix(const Quaternion& q) noexcept
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

        Matrix4x4 m = Matrix4x4::Identity();
        m(0, 0) = 1.0f - 2.0f * (yy + zz);
        m(0, 1) = 2.0f * (xy - wz);
        m(0, 2) = 2.0f * (xz + wy);
        m(1, 0) = 2.0f * (xy + wz);
        m(1, 1) = 1.0f - 2.0f * (xx + zz);
        m(1, 2) = 2.0f * (yz - wx);
        m(2, 0) = 2.0f * (xz - wy);
        m(2, 1) = 2.0f * (yz + wx);
        m(2, 2) = 1.0f - 2.0f * (xx + yy);
        return m;
    }

    Quaternion QuaternionFromMatrix(const Matrix4x4& source) noexcept
    {
        // Divide the scale out of the columns first
        float r[3][3];
        for (size_t column = 0; column < 3; ++column) {
            const Vector3 axis { source(0, column), source(1, column), source(2, column) };
            const float length = axis.Length();
            const float inverse = length > 1e-8f ? 1.0f / length : 0.0f;
            for (size_t row = 0; row < 3; ++row)
                r[row][column] = source(row, column) * inverse;
        }

        Quaternion q;
        const float trace = r[0][0] + r[1][1] + r[2][2];
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q = { (r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s };
        } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
            const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
            q = { 0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s };
        } else if (r[1][1] > r[2][2]) {
            const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
            q = { (r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s };
        } else {
            const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
            q = { (r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s };
        }
        return Normalize(q);
    }

    Matrix4x4 ComposeTRS(const Vector3& translation, const Quaternion& rotation, const Vector3& scale) noexcept
    {
        Matrix4x4 m = RotationMatrix(rotation);
        for (size_t row = 0; row < 3; ++row) {
            m(row, 0) *= scale.x;
            m(row, 1) *= scale.y;
            m(row, 2) *= scale.z;
        }
        m(0, 3) = translation.x;
        m(1, 3) = translation.y;
        m(2, 3) = translation.z;
        return m;
    }

    DecomposedTRS DecomposeTRS(const Matrix4x4& m) noexcept
    {
        DecomposedTRS result;
        result.translation = { m(0, 3), m(1, 3), m(2, 3) };
        result.scale = {
            Vector3 { m(0, 0), m(1, 0), m(2, 0) }.Length(),
            Vector3 { m(0, 1), m(1, 1), m(2, 1) }.Length(),
            Vector3 { m(0, 2), m(1, 2), m(2, 2) }.Length()
        };
        // A mirrored basis: put the reflection into the x scale so the rotation stays proper
        const Vector3 x { m(0, 0), m(1, 0), m(2, 0) };
        const Vector3 y { m(0, 1), m(1, 1), m(2, 1) };
        const Vector3 z { m(0, 2), m(1, 2), m(2, 2) };
        Matrix4x4 rotation = m;
        if (x.Cross(y).Dot(z) < 0.0f) {
            result.scale.x = -result.scale.x;
            for (size_t row = 0; row < 3; ++row)
                rotation(row, 0) = -rotation(row, 0);
        }
        result.rotation = QuaternionFromMatrix(rotation);
        return result;
    }

    std::optional<Matrix4x4> Inverse(const Matrix4x4& matrix) noexcept
    {
        float augmented[4][8] {};
        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column)
                augmented[row][column] = matrix(row, column);
            augmented[row][row + 4] = 1.0f;
        }

        for (size_t column = 0; column < 4; ++column) {
            size_t pivot = column;
            for (size_t row = column + 1; row < 4; ++row) {
                if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
                    pivot = row;
            }
            if (std::abs(augmented[pivot][column]) <= std::numeric_limits<float>::epsilon())
                return std::nullopt;

            if (pivot != column) {
                for (size_t entry = 0; entry < 8; ++entry)
                    std::swap(augmented[pivot][entry], augmented[column][entry]);
            }
            const float divisor = augmented[column][column];
            for (float& entry : augmented[column])
                entry /= divisor;

            for (size_t row = 0; row < 4; ++row) {
                if (row == column)
                    continue;
                const float factor = augmented[row][column];
                for (size_t entry = 0; entry < 8; ++entry)
                    augmented[row][entry] -= factor * augmented[column][entry];
            }
        }

        Matrix4x4 inverse;
        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column)
                inverse(row, column) = augmented[row][column + 4];
        }
        return inverse;
    }

} // namespace math
