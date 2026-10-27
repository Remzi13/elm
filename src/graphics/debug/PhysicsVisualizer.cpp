#include "graphics/debug/PhysicsVisualizer.hpp"

#include "Scene/PhysicsComponents.hpp"

namespace elm::graphics {

namespace {

    constexpr StringView RaysChannel = "Physics/Raycasts";
    constexpr StringView ContactsChannel = "Physics/Contacts";
    constexpr math::Vector4 RayHitColor { 0.3f, 0.95f, 0.3f, 1.0f };
    constexpr math::Vector4 RayMissColor { 0.95f, 0.3f, 0.3f, 1.0f };
    constexpr math::Vector4 ContactColor { 1.0f, 0.9f, 0.2f, 1.0f };
    constexpr math::Vector4 TriggerContactColor { 0.3f, 0.85f, 1.0f, 1.0f };
    /// World units.
    constexpr float MarkerSize = 0.1f;
    constexpr float NormalLength = 0.4f;

} // namespace

PhysicsVisualizer::PhysicsVisualizer(const PhysicsFrameLog& log, DebugDraw& debugDraw)
    : m_log(log)
    , m_raysChannel(debugDraw.RegisterChannel(RaysChannel, "Raycasts", true))
    , m_contactsChannel(debugDraw.RegisterChannel(ContactsChannel, "Contacts", true))
{
}

void PhysicsVisualizer::Draw(DebugDraw& debugDraw)
{
    DrawRayQueries(debugDraw);
    DrawContacts(debugDraw);
}

void PhysicsVisualizer::DrawRayQueries(DebugDraw& debugDraw) const
{
    if (!debugDraw.IsEnabled(m_raysChannel))
        return;
    for (const RayQuery& query : m_log.rayQueries) {
        if (!query.hit) {
            debugDraw.Line(m_raysChannel, query.origin, query.origin + query.direction * query.maxDistance, { .color = RayMissColor });
            continue;
        }
        const DebugPen pen { .color = RayHitColor };
        debugDraw.Line(m_raysChannel, query.origin, query.hit->point, pen);
        debugDraw.Point(m_raysChannel, query.hit->point, MarkerSize, pen);
        debugDraw.Arrow(m_raysChannel, query.hit->point, query.hit->point + query.hit->normal * NormalLength, pen);
    }
}

void PhysicsVisualizer::DrawContacts(DebugDraw& debugDraw) const
{
    if (!debugDraw.IsEnabled(m_contactsChannel))
        return;
    for (const CollisionEvent& event : m_log.contacts) {
        // Contacts lie inside the touching shapes, so they are drawn over them
        const DebugPen pen {
            .color = event.trigger ? TriggerContactColor : ContactColor,
            .depth = render::DebugDepth::OnTop,
            .duration = ContactLifetime,
        };
        debugDraw.Point(m_contactsChannel, event.point, MarkerSize, pen);
        if (!event.trigger)
            debugDraw.Arrow(m_contactsChannel, event.point, event.point + event.normal * NormalLength, pen);
    }
}

} // namespace elm::graphics
