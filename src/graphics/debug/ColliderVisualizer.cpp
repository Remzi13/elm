#include "graphics/debug/ColliderVisualizer.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"

namespace elm::graphics {

namespace {

    constexpr StringView CollidersChannel = "Physics/Colliders";
    constexpr StringView TriggersChannel = "Physics/Triggers";
    constexpr math::Vector4 ColliderColor { 0.35f, 0.95f, 0.45f, 1.0f };
    constexpr math::Vector4 TriggerColor { 1.0f, 0.65f, 0.15f, 1.0f };

    /// transform: the collider frame, the node with the offset of the shape applied.
    void DrawShape(DebugDraw& debugDraw, DebugChannel channel, const Collider& collider,
        const math::Matrix4x4& transform)
    {
        const DebugPen pen { .color = collider.trigger ? TriggerColor : ColliderColor };
        switch (collider.shape) {
        case ColliderShape::Box:
            debugDraw.Box(channel, transform, collider.halfExtents, pen);
            break;
        case ColliderShape::Sphere:
            debugDraw.Sphere(channel, transform, collider.radius, pen);
            break;
        case ColliderShape::Capsule:
            debugDraw.Capsule(channel, transform, collider.radius, collider.halfHeight, pen);
            break;
        }
    }

} // namespace

ColliderVisualizer::ColliderVisualizer(const Scene& scene, DebugDraw& debugDraw)
    : m_scene(scene)
    , m_collidersChannel(debugDraw.RegisterChannel(CollidersChannel, "Colliders", true))
    , m_triggersChannel(debugDraw.RegisterChannel(TriggersChannel, "Triggers", true))
{
}

void ColliderVisualizer::Draw(DebugDraw& debugDraw)
{
    const auto* colliders = m_scene.FindComponents<Collider>();
    if (!colliders || (!debugDraw.IsEnabled(m_collidersChannel) && !debugDraw.IsEnabled(m_triggersChannel)))
        return;

    const auto ids = colliders->Ids();
    const auto components = colliders->Components();
    const auto worldTransforms = m_scene.WorldTransforms();
    for (size_t index = 0; index < components.size(); ++index) {
        const size_t nodeIndex = m_scene.IndexOf(ids[index]);
        if (nodeIndex == Scene::NoNode)
            continue;
        const Collider& collider = components[index];
        const math::Matrix4x4 transform = worldTransforms[nodeIndex] * math::Matrix4x4::Translation(collider.offset);
        DrawShape(debugDraw, collider.trigger ? m_triggersChannel : m_collidersChannel, collider, transform);
    }
}

} // namespace elm::graphics
