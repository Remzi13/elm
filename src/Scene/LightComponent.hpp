#pragma once

#include "math/Vector.hpp"
#include "Scene/ComponentTypes.hpp"

#include <optional>

namespace elm {

    enum class LightType : uint8_t {
        Directional,
        Point,
        Spot
    };

    [[nodiscard]] constexpr const char* LightTypeName(LightType type) noexcept
    {
        switch (type) {
        case LightType::Directional: return "directional";
        case LightType::Point: return "point";
        case LightType::Spot: return "spot";
        }
        return "?";
    }

    [[nodiscard]] constexpr std::optional<LightType> LightTypeFromName(StringView name) noexcept
    {
        for (const LightType type : { LightType::Directional, LightType::Point, LightType::Spot }) {
            if (name == LightTypeName(type))
                return type;
        }
        return std::nullopt;
    }

    /// A light source on a scene node: its position and direction follow the node's world transform.
    struct LightComponent {
        static constexpr ComponentType Type = ComponentType::Light;

        LightType type { LightType::Directional };
        math::Vector4 color { 1.0f, 1.0f, 0.9f, 1.0f };
        float intensity { 1.0f };
        float range { 10.0f };
        float innerConeAngle { 30.0f };
        float outerConeAngle { 45.0f };

        bool operator==(const LightComponent&) const = default;
    };

} // namespace elm
