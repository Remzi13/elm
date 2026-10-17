#pragma once

#include "core/Std.hpp"

#include <array>
#include <concepts>
#include <optional>

namespace elm {

/// Every kind of node component; the scene keeps one store per kind (ComponentStores). A new component
/// gets an enumerator here, its name below and a `Type` member that names the enumerator.
enum class ComponentType : uint8_t {
    Script,
    RigidBody,
    Collider,
    Model,
};

inline constexpr std::array AllComponentTypes { ComponentType::Script, ComponentType::RigidBody, ComponentType::Collider,
    ComponentType::Model };

// Names of the component types in scene files and scripts

[[nodiscard]] constexpr const char* ComponentTypeName(ComponentType type) noexcept
{
    switch (type) {
    case ComponentType::Script: return "Script";
    case ComponentType::RigidBody: return "RigidBody";
    case ComponentType::Collider: return "Collider";
    case ComponentType::Model: return "Model";
    }
    return "?";
}

[[nodiscard]] constexpr std::optional<ComponentType> ComponentTypeFromName(StringView name) noexcept
{
    for (const ComponentType type : AllComponentTypes) {
        if (name == ComponentTypeName(type))
            return type;
    }
    return std::nullopt;
}

/// A type the scene keeps as a node component: a plain copyable struct with
/// `static constexpr ComponentType Type`.
template <typename T>
concept SceneComponent = std::copy_constructible<T> && std::same_as<decltype(T::Type), const ComponentType>;

} // namespace elm
