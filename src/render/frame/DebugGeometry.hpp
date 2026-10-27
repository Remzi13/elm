#pragma once

#include "core/Std.hpp"

#include "math/Vector.hpp"

#include <array>
#include <cstdint>

namespace elm::render {

    /// How a debug primitive meets the scene depth.
    enum class DebugDepth : uint8_t {
        /// Hidden behind scene geometry.
        Tested,
        /// Drawn over the scene.
        OnTop,
    };

    enum class DebugTopology : uint8_t {
        /// Every two vertices are one line.
        Lines,
        /// Every three vertices are one triangle.
        Triangles,
    };

    /// Render style of a debug batch; DebugDrawPass has one pipeline per style.
    struct DebugStyle {
        DebugTopology topology { DebugTopology::Lines };
        DebugDepth depth { DebugDepth::Tested };

        bool operator==(const DebugStyle&) const = default;
    };

    /// Every style once, in the order of DebugStyleIndex: tested before on-top.
    inline constexpr std::array<DebugStyle, 4> AllDebugStyles {
        DebugStyle { DebugTopology::Triangles, DebugDepth::Tested },
        DebugStyle { DebugTopology::Lines, DebugDepth::Tested },
        DebugStyle { DebugTopology::Triangles, DebugDepth::OnTop },
        DebugStyle { DebugTopology::Lines, DebugDepth::OnTop },
    };

    /// Position of the style in AllDebugStyles.
    [[nodiscard]] constexpr size_t DebugStyleIndex(DebugStyle style) noexcept
    {
        constexpr size_t TopologyCount = 2;
        return static_cast<size_t>(style.depth) * TopologyCount + (style.topology == DebugTopology::Lines ? 1 : 0);
    }

    static_assert(DebugStyleIndex(AllDebugStyles[1]) == 1 && DebugStyleIndex(AllDebugStyles[2]) == 2,
        "AllDebugStyles must follow DebugStyleIndex");

    /// World-space debug vertex. The layout is uploaded as-is into the debug vertex stream.
    struct DebugVertex {
        math::Vector3 position;
        /// RGBA8, red in the lowest byte (PackDebugColor).
        uint32_t color { 0 };
    };

    static_assert(sizeof(DebugVertex) == 16, "DebugVertex layout must match the debug vertex stream");

    /// Vertices [firstVertex, firstVertex + vertexCount) of a DebugGeometry drawn in one style.
    struct DebugBatch {
        DebugStyle style;
        uint32_t firstVertex { 0 };
        uint32_t vertexCount { 0 };
    };

    /// Debug primitives in world space, grouped into batches by style.
    struct DebugGeometry {
        Vector<DebugVertex> vertices;
        Vector<DebugBatch> batches;

        [[nodiscard]] bool IsEmpty() const noexcept { return batches.empty(); }
        /// Keeps the allocated capacity.
        void Clear() noexcept { vertices.clear(); batches.clear(); }
    };

    /// Converts a color with components in [0, 1] to the DebugVertex format.
    [[nodiscard]] constexpr uint32_t PackDebugColor(const math::Vector4& color) noexcept
    {
        const auto toByte = [](float value) {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
        };
        return toByte(color.x) | (toByte(color.y) << 8) | (toByte(color.z) << 16) | (toByte(color.w) << 24);
    }

} // namespace elm::render
