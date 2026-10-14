#pragma once

#include "Scene/ComponentStore.hpp"

#include "math/Vector.hpp"

#include <initializer_list>
#include <optional>

namespace elm {

/// How a body moves. A Collider without a RigidBody makes a static body.
enum class BodyType : uint8_t {
    /// Does not move by itself; moving its node teleports it.
    Static,
    /// Follows its node (scripts, animation) and pushes dynamic bodies, nothing pushes it.
    Kinematic,
    /// Simulated: its node follows the body.
    Dynamic,
};

/// Physical behaviour of a node; it becomes a body together with a Collider. PhysicsSystem builds
/// the body on the next step and builds it again when a field changes (the velocity is kept).
struct RigidBody {
    BodyType type { BodyType::Dynamic };
    /// Kilograms, dynamic bodies only.
    float mass { 1.0f };
    float friction { 0.5f };
    /// Bounciness, 0..1.
    float restitution { 0.0f };
    float linearDamping { 0.05f };
    float angularDamping { 0.05f };
    float gravityScale { 1.0f };

    bool operator==(const RigidBody&) const = default;
};

enum class ColliderShape : uint8_t { Box, Sphere, Capsule };

/// Collision shape of a node in its local space; the node's world scale applies to it.
struct Collider {
    ColliderShape shape { ColliderShape::Box };
    /// Box half size.
    math::Vector3 halfExtents { 0.5f, 0.5f, 0.5f };
    /// Sphere and capsule radius.
    float radius { 0.5f };
    /// Capsule: half the length of its cylinder, along local Y.
    float halfHeight { 0.5f };
    /// Center of the shape relative to the node.
    math::Vector3 offset { };
    /// A trigger reports the bodies that enter it (on_trigger_enter) and does not push them.
    bool trigger { false };

    bool operator==(const Collider&) const = default;
};

// Names of the enums in scene files and scripts

[[nodiscard]] constexpr const char* BodyTypeName(BodyType type) noexcept
{
    switch (type) {
    case BodyType::Static: return "static";
    case BodyType::Kinematic: return "kinematic";
    case BodyType::Dynamic: return "dynamic";
    }
    return "?";
}

[[nodiscard]] constexpr std::optional<BodyType> BodyTypeFromName(StringView name) noexcept
{
    for (const BodyType type : { BodyType::Static, BodyType::Kinematic, BodyType::Dynamic }) {
        if (name == BodyTypeName(type))
            return type;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr const char* ColliderShapeName(ColliderShape shape) noexcept
{
    switch (shape) {
    case ColliderShape::Box: return "box";
    case ColliderShape::Sphere: return "sphere";
    case ColliderShape::Capsule: return "capsule";
    }
    return "?";
}

[[nodiscard]] constexpr std::optional<ColliderShape> ColliderShapeFromName(StringView name) noexcept
{
    for (const ColliderShape shape : { ColliderShape::Box, ColliderShape::Sphere, ColliderShape::Capsule }) {
        if (name == ColliderShapeName(shape))
            return shape;
    }
    return std::nullopt;
}

/// A contact that began or ended during a physics step; the scripts of both entities hear of it.
struct CollisionEvent {
    enum class Kind : uint8_t { Enter, Exit };

    Kind kind { Kind::Enter };
    /// One of the two is a trigger.
    bool trigger { false };
    NodeId first;
    NodeId second;
    /// Enter only: a contact point and the contact normal pointing from first to second.
    math::Vector3 point;
    math::Vector3 normal;
};

} // namespace elm
