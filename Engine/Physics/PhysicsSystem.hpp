#pragma once

#include "Engine/Core/Error.hpp"
#include "Engine/Scene/Transform.hpp"

#include <memory>
#include <vector>

// Jolt Physics forward declarations
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
    class BodyID;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
}

namespace Engine {

// Object Layers
namespace Layers {
    constexpr uint16_t NON_MOVING = 0;
    constexpr uint16_t MOVING = 1;
    constexpr uint16_t NUM_LAYERS = 2;
}

// BroadPhase Layers
namespace BroadPhaseLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint32_t NUM_LAYERS = 2;
}

class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;
    PhysicsSystem(PhysicsSystem&&) noexcept = delete;
    PhysicsSystem& operator=(PhysicsSystem&&) noexcept = delete;

    [[nodiscard]] auto Init() -> EngineResult<void>;
    void Step(float deltaTime);
    void Shutdown();

    // Scene setup
    void CreateDefaultScene();

    // Stats & Querying
    [[nodiscard]] auto GetDynamicBoxTransform() const -> Transform;
    [[nodiscard]] auto GetGroundTransform() const -> Transform;
    [[nodiscard]] uint32_t GetNumBodies() const;

private:
    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;

    std::unique_ptr<JPH::BroadPhaseLayerInterface> m_broadPhaseLayerInterface;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> m_objectVsBroadPhaseFilter;
    std::unique_ptr<JPH::ObjectLayerPairFilter> m_objectLayerPairFilter;

    // Body IDs for our test scene
    uint32_t m_groundBodyID{0xFFFFFFFF};
    uint32_t m_dynamicBoxBodyID{0xFFFFFFFF};

    bool m_initialized{false};
};

} // namespace Engine
