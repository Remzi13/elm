#pragma once

#include <span>
#include <cmath>

// Forward declarations or lightweight math wrappers
namespace JPH {
    class Vec3;
    class Quat;
    class Mat44;
}

namespace Diligent {
    struct float3;
    struct float4;
    struct float4x4;
}

#include "graphics/culling/MathTypes.hpp"

namespace Engine {

class Transform {
public:
    Vector3 position{0.0f, 0.0f, 0.0f};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    Transform(Vector3 pos, Quaternion rot, Vector3 scl = {1.0f, 1.0f, 1.0f})
        : position(pos), rotation(rot), scale(scl) {}

    [[nodiscard]] auto GetLocalToWorldMatrix() const -> std::array<float, 16> {
        // Compute TRS matrix in column-major order
        const float qx = rotation.x, qy = rotation.y, qz = rotation.z, qw = rotation.w;
        const float sx = scale.x, sy = scale.y, sz = scale.z;

        const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        const float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        const float wx = qw * qx, wy = qw * qy, wz = qw * qz;

        return std::array<float, 16>{
            (1.0f - 2.0f * (yy + zz)) * sx, (2.0f * (xy + wz)) * sx,        (2.0f * (xz - wy)) * sx,        0.0f,
            (2.0f * (xy - wz)) * sy,        (1.0f - 2.0f * (xx + zz)) * sy, (2.0f * (yz + wx)) * sy,        0.0f,
            (2.0f * (xz + wy)) * sz,        (2.0f * (yz - wx)) * sz,        (1.0f - 2.0f * (xx + yy)) * sz, 0.0f,
            position.x,                 position.y,                 position.z,                 1.0f
        };
    }
};

} // namespace Engine
