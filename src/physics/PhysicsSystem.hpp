#pragma once

#include "Scene/PhysicsComponents.hpp"
#include "core/Error.hpp"
#include "core/Std.hpp"

#include "math/Rotation.hpp"

#include <memory>
#include <optional>
#include <span>

// Jolt Physics forward declarations
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
}

namespace elm  {

class Scene;

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

/// Jolt Physics bodies of the scene nodes that have a Collider (with a RigidBody; without one the
/// body is static). Step makes the bodies follow the components and the node transforms, simulates,
/// then moves the nodes of the dynamic bodies. Bodies are keyed by NodeId: a removed node or
/// component loses its body on the next step. Update thread only.
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;
    PhysicsSystem(PhysicsSystem&&) noexcept = delete;
    PhysicsSystem& operator=(PhysicsSystem&&) noexcept = delete;

    [[nodiscard]] auto Init() -> elm::EngineResult<void>;
    void Shutdown();

    /// Brings the bodies in line with the scene, simulates deltaTime, writes the dynamic bodies
    /// back into their nodes (local transforms: world transforms follow on UpdateWorldTransforms).
    void Step(Scene& scene, float deltaTime);
    /// Brings one node's body up to date now, e.g. for a script that sets a velocity right after
    /// adding the components. False when the node has no body: no Collider, or it could not be made.
    bool SyncNode(Scene& scene, NodeId node);
    /// Contacts that began or ended during the last Step.
    [[nodiscard]] std::span<const CollisionEvent> CollisionEvents() const noexcept { return m_events; }
    /// Removes every body.
    void Clear();

    // ── Bodies of nodes: a node without a body gives nullopt or false ──

    [[nodiscard]] bool HasBody(NodeId node) const;
    [[nodiscard]] std::optional<math::Vector3> GetLinearVelocity(NodeId node) const;
    [[nodiscard]] std::optional<math::Vector3> GetAngularVelocity(NodeId node) const;
    /// Dynamic bodies only: a kinematic one moves with its node.
    bool SetLinearVelocity(NodeId node, const math::Vector3& velocity);
    bool SetAngularVelocity(NodeId node, const math::Vector3& velocity);
    /// Dynamic bodies: a force acts during the next step, an impulse changes the velocity at once.
    bool AddForce(NodeId node, const math::Vector3& force);
    bool AddImpulse(NodeId node, const math::Vector3& impulse);

    /// Closest body along the ray, triggers and the body of `ignore` skipped. A valid call (non-zero
    /// direction, positive maxDistance) is recorded in the frame log.
    [[nodiscard]] std::optional<RayHit> CastRay(const math::Vector3& origin, const math::Vector3& direction,
        float maxDistance, NodeId ignore = { }) const;

    /// Rays and begun contacts since the last ClearFrameLog. The reference stays valid for the
    /// life of the system, so a debug view may keep it.
    [[nodiscard]] const PhysicsFrameLog& FrameLog() const noexcept { return m_frameLog; }
    /// Starts the log of a new frame.
    void ClearFrameLog() noexcept { m_frameLog.Clear(); }

    [[nodiscard]] math::Vector3 GetGravity() const;
    void SetGravity(const math::Vector3& gravity);

    [[nodiscard]] uint32_t GetNumBodies() const;
    [[nodiscard]] uint32_t GetNumActiveBodies() const;

private:
    class ContactQueue;

    /// JPH::BodyID::cInvalidBodyID
    static constexpr uint32_t InvalidBody = 0xFFFFFFFF;

    /// A node's body and what it was built from.
    struct BodyLink {
        NodeId node;
        /// JPH::BodyID value, InvalidBody when the body could not be made (until a component changes).
        uint32_t body { InvalidBody };
        bool built { false };
        Collider collider;
        /// The node's RigidBody, a static one when it has none.
        RigidBody rigidBody;
        /// World scale of the node, baked into the shape.
        math::Vector3 scale { 1.0f, 1.0f, 1.0f };
        /// Pose the node had at the last sync or write-back: a different one means it was moved.
        math::Vector3 position;
        math::Quaternion rotation;
        /// A kinematic body got a velocity from MoveKinematic and has to be stopped once its node rests.
        bool kinematicMoving { false };
    };

    /// deltaTime is the step a kinematic body moves in; without it a moved node teleports its body.
    bool SyncBody(Scene& scene, NodeId node, std::optional<float> deltaTime);
    void BuildBody(BodyLink& link, const String& nodeName, const math::Vector3& position, const math::Quaternion& rotation);
    void DestroyBody(BodyLink& link);
    void WriteBack(Scene& scene);
    void CollectEvents();
    [[nodiscard]] const BodyLink* FindLink(NodeId node) const;
    /// The body of a node when it is dynamic, InvalidBody otherwise.
    [[nodiscard]] uint32_t DynamicBodyOf(NodeId node) const;
    /// CastRay without recording; unitDirection has unit length.
    [[nodiscard]] std::optional<RayHit> FindRayHit(const math::Vector3& origin, const math::Vector3& unitDirection,
        float maxDistance, NodeId ignore) const;

    UniquePtr<JPH::PhysicsSystem> m_physicsSystem;
    UniquePtr<JPH::TempAllocatorImpl> m_tempAllocator;
    UniquePtr<JPH::JobSystemThreadPool> m_jobSystem;

    UniquePtr<JPH::BroadPhaseLayerInterface> m_broadPhaseLayerInterface;
    UniquePtr<JPH::ObjectVsBroadPhaseLayerFilter> m_objectVsBroadPhaseFilter;
    UniquePtr<JPH::ObjectLayerPairFilter> m_objectLayerPairFilter;
    UniquePtr<ContactQueue> m_contacts;

    /// NodeId::Raw() -> the node's body.
    UnorderedMap<uint32_t, BodyLink> m_links;
    Vector<CollisionEvent> m_events;
    /// A log for inspection, not simulation state: the const CastRay appends to it.
    mutable PhysicsFrameLog m_frameLog;
    /// The body limit was hit: reported once.
    bool m_reportedFull { false };

    bool m_initialized{false};
};

} // namespace Engine
