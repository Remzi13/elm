#include "physics/PhysicsSystem.hpp"

#include "Scene/Scene.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"

// Jolt includes
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace elm  {

namespace {

    constexpr const char* LogSource = "PhysicsSystem";
    /// Smallest shape size: Jolt rejects empty shapes, a flat model still gets a thin body.
    constexpr float MinExtent = 0.01f;

    JPH::Vec3 ToJolt(const math::Vector3& v) { return { v.x, v.y, v.z }; }
    JPH::RVec3 ToJoltPosition(const math::Vector3& v) { return JPH::RVec3(ToJolt(v)); }
    /// Jolt wants unit quaternions, decomposed node rotations are off by rounding.
    JPH::Quat ToJolt(const math::Quaternion& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }

    template <typename V>
    math::Vector3 FromJolt(const V& v)
    {
        return { static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()) };
    }
    math::Quaternion FromJolt(JPH::QuatArg q) { return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() }; }

    bool Near(const math::Vector3& a, const math::Vector3& b)
    {
        constexpr float Epsilon = 1e-4f;
        return std::abs(a.x - b.x) <= Epsilon && std::abs(a.y - b.y) <= Epsilon && std::abs(a.z - b.z) <= Epsilon;
    }
    bool SameRotation(const math::Quaternion& a, const math::Quaternion& b) { return std::abs(math::Dot(a, b)) >= 1.0f - 1e-6f; }

    JPH::EMotionType MotionOf(BodyType type)
    {
        switch (type) {
        case BodyType::Static: return JPH::EMotionType::Static;
        case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
        case BodyType::Dynamic: return JPH::EMotionType::Dynamic;
        }
        return JPH::EMotionType::Static;
    }

    /// The shape of a collider with the node's world scale baked in (Jolt bodies have no scale).
    JPH::ShapeRefC MakeShape(const Collider& collider, const math::Vector3& scale, String& error)
    {
        const math::Vector3 size { std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };
        JPH::ShapeSettings::ShapeResult result;
        switch (collider.shape) {
        case ColliderShape::Box: {
            const JPH::Vec3 half(std::max(collider.halfExtents.x * size.x, MinExtent), std::max(collider.halfExtents.y * size.y, MinExtent),
                std::max(collider.halfExtents.z * size.z, MinExtent));
            // The convex radius rounds the corners, it has to fit into the box
            result = JPH::BoxShapeSettings(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f)).Create();
            break;
        }
        case ColliderShape::Sphere:
            result = JPH::SphereShapeSettings(std::max(collider.radius * std::max({ size.x, size.y, size.z }), MinExtent)).Create();
            break;
        case ColliderShape::Capsule: {
            const float radius = std::max(collider.radius * std::max(size.x, size.z), MinExtent);
            const float halfHeight = collider.halfHeight * size.y;
            result = halfHeight > MinExtent ? JPH::CapsuleShapeSettings(halfHeight, radius).Create() : JPH::SphereShapeSettings(radius).Create();
            break;
        }
        }
        if (result.HasError()) {
            error = result.GetError().c_str();
            return nullptr;
        }

        const math::Vector3 offset { collider.offset.x * scale.x, collider.offset.y * scale.y, collider.offset.z * scale.z };
        if (offset == math::Vector3 { })
            return result.Get();
        auto translated = JPH::RotatedTranslatedShapeSettings(ToJolt(offset), JPH::Quat::sIdentity(), result.Get()).Create();
        if (translated.HasError()) {
            error = translated.GetError().c_str();
            return nullptr;
        }
        return translated.Get();
    }

    /// Rays see solid bodies only, and not the body they start from.
    class RayFilter final : public JPH::BodyFilter {
    public:
        explicit RayFilter(uint64_t ignore) noexcept : m_ignore(ignore) { }

        bool ShouldCollideLocked(const JPH::Body& body) const override
        {
            return !body.IsSensor() && (m_ignore == 0 || body.GetUserData() != m_ignore);
        }

    private:
        uint64_t m_ignore;
    };

} // namespace

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

/// Collects the contacts Jolt reports during a step. The callbacks come from Jolt's worker threads.
class PhysicsSystem::ContactQueue final : public JPH::ContactListener {
public:
    struct Contact {
        bool added { false };
        uint32_t body1 { InvalidBody };
        uint32_t body2 { InvalidBody };
        math::Vector3 point;
        math::Vector3 normal;
    };

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        Contact contact;
        contact.added = true;
        contact.body1 = body1.GetID().GetIndexAndSequenceNumber();
        contact.body2 = body2.GetID().GetIndexAndSequenceNumber();
        contact.point = FromJolt(manifold.mRelativeContactPointsOn1.empty() ? manifold.mBaseOffset : manifold.GetWorldSpaceContactPointOn1(0));
        contact.normal = FromJolt(manifold.mWorldSpaceNormal);
        const std::lock_guard lock(m_mutex);
        m_contacts.push_back(contact);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        Contact contact;
        contact.body1 = pair.GetBody1ID().GetIndexAndSequenceNumber();
        contact.body2 = pair.GetBody2ID().GetIndexAndSequenceNumber();
        const std::lock_guard lock(m_mutex);
        m_contacts.push_back(contact);
    }

    /// The contacts reported since the last call.
    [[nodiscard]] Vector<Contact> Take()
    {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_contacts, { });
    }

private:
    std::mutex m_mutex;
    Vector<Contact> m_contacts;
};

PhysicsSystem::PhysicsSystem() = default;

PhysicsSystem::~PhysicsSystem() {
    Shutdown();
}

auto PhysicsSystem::Init() -> elm::EngineResult<void> {
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
    m_tempAllocator = MakeUnique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    const uint32_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    m_jobSystem = MakeUnique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(numThreads));

    // Create BroadPhase and Object filter implementations
    m_broadPhaseLayerInterface = MakeUnique<BPLayerInterfaceImpl>();
    m_objectVsBroadPhaseFilter = MakeUnique<ObjectVsBroadPhaseLayerFilterImpl>();
    m_objectLayerPairFilter = MakeUnique<ObjectLayerPairFilterImpl>();

    // Init physics system settings
    constexpr uint32_t cMaxBodies = 8192;
    constexpr uint32_t cNumBodyMutexes = 0;
    constexpr uint32_t cMaxBodyPairs = 8192;
    constexpr uint32_t cMaxContactConstraints = 4096;

    m_physicsSystem = MakeUnique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *m_broadPhaseLayerInterface,
        *m_objectVsBroadPhaseFilter,
        *m_objectLayerPairFilter
    );
    m_contacts = MakeUnique<ContactQueue>();
    m_physicsSystem->SetContactListener(m_contacts.get());

    m_initialized = true;
    LOG_MESSAGE( log::Category::Physics, LogSource, "Initialized Jolt Physics engine successfully." );

    return {};
}

void PhysicsSystem::Step(Scene& scene, float deltaTime) {
    if (!m_initialized) return;
    ELM_PROFILE_SCOPE_N("Physics Step");

    // Scripts moved nodes since the last step
    scene.UpdateWorldTransforms();
    const auto* colliders = scene.FindComponents<Collider>();
    for (auto it = m_links.begin(); it != m_links.end();) {
        const NodeId node = it->second.node;
        if (colliders && colliders->Contains(node) && scene.IndexOf(node) != Scene::NoNode) {
            ++it;
            continue;
        }
        // The node or its Collider went away
        DestroyBody(it->second);
        it = m_links.erase(it);
    }
    if (colliders) {
        for (const NodeId node : colliders->Ids())
            SyncBody(scene, node, deltaTime);
    }

    // Fixed timestep step: 1 collision step per physics call
    constexpr int cCollisionSteps = 1;
    m_physicsSystem->Update(deltaTime, cCollisionSteps, m_tempAllocator.get(), m_jobSystem.get());

    WriteBack(scene);
    CollectEvents();
}

bool PhysicsSystem::SyncNode(Scene& scene, NodeId node) {
    if (!m_initialized) return false;
    scene.UpdateWorldTransforms();
    return SyncBody(scene, node, std::nullopt);
}

bool PhysicsSystem::SyncBody(Scene& scene, NodeId node, std::optional<float> deltaTime) {
    const size_t index = scene.IndexOf(node);
    const auto* colliders = scene.FindComponents<Collider>();
    const Collider* collider = index != Scene::NoNode && colliders ? colliders->Find(node) : nullptr;
    auto found = m_links.find(node.Raw());
    if (!collider) {
        if (found != m_links.end()) {
            DestroyBody(found->second);
            m_links.erase(found);
        }
        return false;
    }

    const auto* rigidBodies = scene.FindComponents<RigidBody>();
    const RigidBody* rigidBody = rigidBodies ? rigidBodies->Find(node) : nullptr;
    const RigidBody settings = rigidBody ? *rigidBody : RigidBody { .type = BodyType::Static };
    const auto trs = math::DecomposeTRS(scene.WorldTransforms()[index]);

    if (found == m_links.end())
        found = m_links.emplace(node.Raw(), BodyLink { .node = node }).first;
    BodyLink& link = found->second;

    if (!link.built || link.collider != *collider || link.rigidBody != settings || !Near(link.scale, trs.scale)) {
        link.built = true;
        link.collider = *collider;
        link.rigidBody = settings;
        link.scale = trs.scale;
        BuildBody(link, scene.Nodes()[index].name, trs.translation, trs.rotation);
        return link.body != InvalidBody;
    }
    if (link.body == InvalidBody)
        return false;

    JPH::BodyInterface& bodies = m_physicsSystem->GetBodyInterface();
    const JPH::BodyID id(link.body);
    const bool moved = !Near(link.position, trs.translation) || !SameRotation(link.rotation, trs.rotation);
    if (settings.type == BodyType::Kinematic && deltaTime) {
        // Moves over the step with the velocity that takes it there, pushing what is in the way
        if (moved) {
            bodies.MoveKinematic(id, ToJoltPosition(trs.translation), ToJolt(trs.rotation), *deltaTime);
            link.kinematicMoving = true;
        } else if (link.kinematicMoving) {
            bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            link.kinematicMoving = false;
        }
    } else if (moved) {
        // Somebody moved the node (a script, the editor): the body is put there
        bodies.SetPositionAndRotation(id, ToJoltPosition(trs.translation), ToJolt(trs.rotation),
            settings.type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    }
    link.position = trs.translation;
    link.rotation = trs.rotation;
    return true;
}

void PhysicsSystem::BuildBody(BodyLink& link, const String& nodeName, const math::Vector3& position, const math::Quaternion& rotation) {
    JPH::BodyInterface& bodies = m_physicsSystem->GetBodyInterface();

    // A body built again keeps moving
    JPH::Vec3 linearVelocity = JPH::Vec3::sZero();
    JPH::Vec3 angularVelocity = JPH::Vec3::sZero();
    if (link.body != InvalidBody) {
        const JPH::BodyID previous(link.body);
        linearVelocity = bodies.GetLinearVelocity(previous);
        angularVelocity = bodies.GetAngularVelocity(previous);
    }
    DestroyBody(link);
    link.position = position;
    link.rotation = rotation;
    link.kinematicMoving = false;

    String error;
    const JPH::ShapeRefC shape = MakeShape(link.collider, link.scale, error);
    if (!shape) {
        ERROR_MESSAGE(log::Category::Physics, LogSource, "No body for the collider of '%s': %s", nodeName.c_str(), error.c_str());
        return;
    }

    const BodyType type = link.rigidBody.type;
    JPH::BodyCreationSettings settings(shape, ToJoltPosition(position), ToJolt(rotation), MotionOf(type),
        type == BodyType::Static ? Layers::NON_MOVING : Layers::MOVING);
    settings.mUserData = link.node.Raw();
    settings.mIsSensor = link.collider.trigger;
    settings.mFriction = link.rigidBody.friction;
    settings.mRestitution = link.rigidBody.restitution;
    settings.mLinearDamping = link.rigidBody.linearDamping;
    settings.mAngularDamping = link.rigidBody.angularDamping;
    settings.mGravityFactor = link.rigidBody.gravityScale;
    if (type == BodyType::Dynamic) {
        // The mass as given, the inertia from the shape
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(link.rigidBody.mass, 0.001f);
    }

    JPH::Body* body = bodies.CreateBody(settings);
    if (!body) {
        if (!m_reportedFull) {
            m_reportedFull = true;
            ERROR_MESSAGE(log::Category::Physics, LogSource, "The physics body limit is reached: '%s' and the next colliders get no body",
                nodeName.c_str());
        }
        return;
    }
    bodies.AddBody(body->GetID(), type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    link.body = body->GetID().GetIndexAndSequenceNumber();
    if (type == BodyType::Dynamic)
        bodies.SetLinearAndAngularVelocity(body->GetID(), linearVelocity, angularVelocity);
}

void PhysicsSystem::DestroyBody(BodyLink& link) {
    if (link.body == InvalidBody)
        return;
    JPH::BodyInterface& bodies = m_physicsSystem->GetBodyInterface();
    const JPH::BodyID id(link.body);
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
    link.body = InvalidBody;
}

void PhysicsSystem::WriteBack(Scene& scene) {
    const JPH::BodyInterface& bodies = m_physicsSystem->GetBodyInterface();
    for (auto& entry : m_links) {
        BodyLink& link = entry.second;
        if (link.body == InvalidBody || link.rigidBody.type != BodyType::Dynamic)
            continue;
        const JPH::BodyID id(link.body);
        // A sleeping body did not move
        if (!bodies.IsActive(id))
            continue;
        const size_t index = scene.IndexOf(link.node);
        if (index == Scene::NoNode)
            continue;

        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(id, position, rotation);
        link.position = FromJolt(position);
        link.rotation = FromJolt(rotation);

        const math::Matrix4x4 world = math::ComposeTRS(link.position, link.rotation, link.scale);
        const size_t parent = scene.IndexOf(scene.Nodes()[index].parent);
        if (parent == Scene::NoNode) {
            scene.SetLocalTransform(link.node, world);
        } else if (const auto parentInverse = math::Inverse(scene.WorldTransforms()[parent])) {
            // Relative to the parent as it was before the step
            scene.SetLocalTransform(link.node, *parentInverse * world);
        }
    }
}

void PhysicsSystem::CollectEvents() {
    m_events.clear();
    const JPH::BodyInterface& bodies = m_physicsSystem->GetBodyInterface();
    for (const auto& contact : m_contacts->Take()) {
        CollisionEvent event;
        event.kind = contact.added ? CollisionEvent::Kind::Enter : CollisionEvent::Kind::Exit;
        event.first = NodeId::FromRaw(static_cast<uint32_t>(bodies.GetUserData(JPH::BodyID(contact.body1))));
        event.second = NodeId::FromRaw(static_cast<uint32_t>(bodies.GetUserData(JPH::BodyID(contact.body2))));
        if (!event.first || !event.second)
            continue;
        const BodyLink* first = FindLink(event.first);
        const BodyLink* second = FindLink(event.second);
        event.trigger = (first && first->collider.trigger) || (second && second->collider.trigger);
        event.point = contact.point;
        event.normal = contact.normal;
        m_events.push_back(event);
        if (event.kind == CollisionEvent::Kind::Enter && m_frameLog.contacts.size() < PhysicsFrameLog::MaxContacts)
            m_frameLog.contacts.push_back(event);
    }
}

void PhysicsSystem::Clear() {
    if (!m_initialized) return;
    for (auto& entry : m_links)
        DestroyBody(entry.second);
    m_links.clear();
    m_events.clear();
    (void)m_contacts->Take();
}

auto PhysicsSystem::FindLink(NodeId node) const -> const BodyLink* {
    const auto found = m_links.find(node.Raw());
    return found == m_links.end() ? nullptr : &found->second;
}

uint32_t PhysicsSystem::DynamicBodyOf(NodeId node) const {
    const BodyLink* link = FindLink(node);
    return link && link->rigidBody.type == BodyType::Dynamic ? link->body : InvalidBody;
}

bool PhysicsSystem::HasBody(NodeId node) const {
    const BodyLink* link = FindLink(node);
    return link && link->body != InvalidBody;
}

std::optional<math::Vector3> PhysicsSystem::GetLinearVelocity(NodeId node) const {
    const BodyLink* link = FindLink(node);
    if (!link || link->body == InvalidBody)
        return std::nullopt;
    return FromJolt(m_physicsSystem->GetBodyInterface().GetLinearVelocity(JPH::BodyID(link->body)));
}

std::optional<math::Vector3> PhysicsSystem::GetAngularVelocity(NodeId node) const {
    const BodyLink* link = FindLink(node);
    if (!link || link->body == InvalidBody)
        return std::nullopt;
    return FromJolt(m_physicsSystem->GetBodyInterface().GetAngularVelocity(JPH::BodyID(link->body)));
}

bool PhysicsSystem::SetLinearVelocity(NodeId node, const math::Vector3& velocity) {
    const uint32_t body = DynamicBodyOf(node);
    if (body == InvalidBody)
        return false;
    m_physicsSystem->GetBodyInterface().SetLinearVelocity(JPH::BodyID(body), ToJolt(velocity));
    return true;
}

bool PhysicsSystem::SetAngularVelocity(NodeId node, const math::Vector3& velocity) {
    const uint32_t body = DynamicBodyOf(node);
    if (body == InvalidBody)
        return false;
    m_physicsSystem->GetBodyInterface().SetAngularVelocity(JPH::BodyID(body), ToJolt(velocity));
    return true;
}

bool PhysicsSystem::AddForce(NodeId node, const math::Vector3& force) {
    const uint32_t body = DynamicBodyOf(node);
    if (body == InvalidBody)
        return false;
    m_physicsSystem->GetBodyInterface().AddForce(JPH::BodyID(body), ToJolt(force));
    return true;
}

bool PhysicsSystem::AddImpulse(NodeId node, const math::Vector3& impulse) {
    const uint32_t body = DynamicBodyOf(node);
    if (body == InvalidBody)
        return false;
    m_physicsSystem->GetBodyInterface().AddImpulse(JPH::BodyID(body), ToJolt(impulse));
    return true;
}

std::optional<RayHit> PhysicsSystem::CastRay(const math::Vector3& origin, const math::Vector3& direction, float maxDistance,
    NodeId ignore) const {
    const math::Vector3 unit = direction.Normalized();
    if (!m_initialized || !(maxDistance > 0.0f) || unit.Length() == 0.0f)
        return std::nullopt;

    std::optional<RayHit> hit = FindRayHit(origin, unit, maxDistance, ignore);
    if (m_frameLog.rayQueries.size() < PhysicsFrameLog::MaxRayQueries)
        m_frameLog.rayQueries.push_back({ origin, unit, maxDistance, hit });
    return hit;
}

std::optional<RayHit> PhysicsSystem::FindRayHit(const math::Vector3& origin, const math::Vector3& unit, float maxDistance,
    NodeId ignore) const {
    const JPH::RRayCast ray { ToJoltPosition(origin), ToJolt(unit * maxDistance) };
    JPH::RayCastResult hit;
    const RayFilter filter(ignore.Raw());
    if (!m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, filter))
        return std::nullopt;

    const JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hit.mBodyID);
    if (!lock.Succeeded())
        return std::nullopt;
    const JPH::Body& body = lock.GetBody();
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    RayHit result;
    result.node = NodeId::FromRaw(static_cast<uint32_t>(body.GetUserData()));
    result.point = FromJolt(point);
    result.normal = FromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    result.distance = hit.mFraction * maxDistance;
    return result;
}

math::Vector3 PhysicsSystem::GetGravity() const {
    return m_physicsSystem ? FromJolt(m_physicsSystem->GetGravity()) : math::Vector3 { };
}

void PhysicsSystem::SetGravity(const math::Vector3& gravity) {
    if (m_physicsSystem)
        m_physicsSystem->SetGravity(ToJolt(gravity));
}

uint32_t PhysicsSystem::GetNumBodies() const {
    return m_physicsSystem ? m_physicsSystem->GetNumBodies() : 0;
}

uint32_t PhysicsSystem::GetNumActiveBodies() const {
    return m_physicsSystem ? m_physicsSystem->GetNumActiveBodies(JPH::EBodyType::RigidBody) : 0;
}

void PhysicsSystem::Shutdown() {
    if (!m_initialized) return;

    Clear();
    m_physicsSystem->SetContactListener(nullptr);
    m_physicsSystem = nullptr;
    m_contacts = nullptr;

    m_jobSystem = nullptr;
    m_tempAllocator = nullptr;

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    JPH::UnregisterTypes();

    m_initialized = false;
    LOG_MESSAGE( log::Category::Physics, LogSource, "Shutdown completed." );
}

} // namespace Engine
