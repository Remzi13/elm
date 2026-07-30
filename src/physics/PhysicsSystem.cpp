#include "physics/PhysicsSystem.hpp"

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <iostream>
#include <thread>

namespace Engine {

// BroadPhaseLayerInterface implementation
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::NON_MOVING);
        mObjectToBroadPhase[Layers::MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
    }

    uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case BroadPhaseLayers::MOVING: return "MOVING";
        default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

// ObjectVsBroadPhaseLayerFilter implementation
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
        case Layers::NON_MOVING:
            return inLayer2 == JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

// ObjectLayerPairFilter implementation
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
        case Layers::NON_MOVING:
            return inObject2 == Layers::MOVING; // Static collides only with moving
        case Layers::MOVING:
            return true; // Moving collides with everything
        default:
            return false;
        }
    }
};

PhysicsSystem::PhysicsSystem() = default;

PhysicsSystem::~PhysicsSystem() {
    Shutdown();
}

auto PhysicsSystem::Init() -> EngineResult<void> {
    if (m_initialized) {
        return {};
    }

    // Register Jolt allocators
    JPH::RegisterDefaultAllocator();

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    // Create TempAllocator & JobSystem
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    
    const uint32_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(numThreads));

    // Create BroadPhase and Object filter implementations
    m_broadPhaseLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
    m_objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    m_objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

    // Init physics system settings
    constexpr uint32_t cMaxBodies = 1024;
    constexpr uint32_t cNumBodyMutexes = 0;
    constexpr uint32_t cMaxBodyPairs = 1024;
    constexpr uint32_t cMaxContactConstraints = 1024;

    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *m_broadPhaseLayerInterface,
        *m_objectVsBroadPhaseFilter,
        *m_objectLayerPairFilter
    );

    m_initialized = true;
    std::cout << "[PhysicsSystem] Initialized Jolt Physics engine successfully." << std::endl;

    CreateDefaultScene();

    return {};
}

void PhysicsSystem::CreateDefaultScene() {
    if (!m_physicsSystem) return;

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

    // Create Ground (Static Box)
    JPH::BoxShapeSettings groundShapeSettings(JPH::Vec3(50.0f, 1.0f, 50.0f));
    JPH::ShapeSettings::ShapeResult groundShapeResult = groundShapeSettings.Create();

    if (groundShapeResult.HasError()) {
        std::cout << "[PhysicsSystem] Failed to create ground shape: " << groundShapeResult.GetError().c_str() << std::endl;
        return;
    }

    JPH::BodyCreationSettings groundSettings(
        groundShapeResult.Get(),
        JPH::RVec3(0.0, -1.0, 0.0),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING
    );

    JPH::Body* ground = bodyInterface.CreateBody(groundSettings);
    if (ground) {
        bodyInterface.AddBody(ground->GetID(), JPH::EActivation::DontActivate);
        m_groundBodyID = ground->GetID().GetIndexAndSequenceNumber();
    }

    // Create Dynamic Box
    JPH::BoxShapeSettings boxShapeSettings(JPH::Vec3(1.0f, 1.0f, 1.0f));
    JPH::ShapeSettings::ShapeResult boxShapeResult = boxShapeSettings.Create();

    if (boxShapeResult.HasError()) {
        std::cout << "[PhysicsSystem] Failed to create box shape: " << boxShapeResult.GetError().c_str() << std::endl;
        return;
    }

    JPH::BodyCreationSettings boxSettings(
        boxShapeResult.Get(),
        JPH::RVec3(0.0, 15.0, 0.0),
        JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.4f),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );

    JPH::Body* dynamicBox = bodyInterface.CreateBody(boxSettings);
    if (dynamicBox) {
        bodyInterface.AddBody(dynamicBox->GetID(), JPH::EActivation::Activate);
        m_dynamicBoxBodyID = dynamicBox->GetID().GetIndexAndSequenceNumber();
    }

    m_physicsSystem->OptimizeBroadPhase();
    std::cout << "[PhysicsSystem] Default scene initialized with ground and dynamic box." << std::endl;
}

void PhysicsSystem::Step(float deltaTime) {
    if (!m_physicsSystem || !m_initialized) return;

    // Fixed timestep step: 1 collision step per physics call
    constexpr int cCollisionSteps = 1;
    m_physicsSystem->Update(deltaTime, cCollisionSteps, m_tempAllocator.get(), m_jobSystem.get());
}

auto PhysicsSystem::GetDynamicBoxTransform() const -> Transform {
    Transform transform;
    if (!m_physicsSystem || m_dynamicBoxBodyID == 0xFFFFFFFF) return transform;

    const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(m_dynamicBoxBodyID);
    if (bodyInterface.IsAdded(id)) {
        JPH::RVec3 pos = bodyInterface.GetPosition(id);
        JPH::Quat rot = bodyInterface.GetRotation(id);

        transform.position = { static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()) };
        transform.rotation = { rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW() };
    }
    return transform;
}

auto PhysicsSystem::GetGroundTransform() const -> Transform {
    Transform transform;
    if (!m_physicsSystem || m_groundBodyID == 0xFFFFFFFF) return transform;

    const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(m_groundBodyID);
    if (bodyInterface.IsAdded(id)) {
        JPH::RVec3 pos = bodyInterface.GetPosition(id);
        JPH::Quat rot = bodyInterface.GetRotation(id);

        transform.position = { static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()) };
        transform.rotation = { rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW() };
    }
    return transform;
}

uint32_t PhysicsSystem::GetNumBodies() const {
    return m_physicsSystem ? m_physicsSystem->GetNumBodies() : 0;
}

void PhysicsSystem::Shutdown() {
    if (!m_initialized) return;

    if (m_physicsSystem) {
        m_physicsSystem = nullptr;
    }

    m_jobSystem = nullptr;
    m_tempAllocator = nullptr;

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    JPH::UnregisterTypes();

    m_initialized = false;
    std::cout << "[PhysicsSystem] Shutdown completed." << std::endl;
}

} // namespace Engine
