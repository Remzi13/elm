#include "graphics/gizmo/TransformGizmo.hpp"

#include "math/Rotation.hpp"

#include <algorithm>
#include <cmath>

namespace elm::graphics {

namespace {

    using math::Matrix4x4;
    using math::Vector3;

    /// Handle length relative to the distance to the camera: the gizmo keeps its size on screen.
    constexpr float ScreenFraction = 0.15f;
    /// A handle is hovered within this distance of it, relative to the handle length.
    constexpr float PickRadius = 0.08f;
    /// Below this |cos| between the ray and a line or plane they count as parallel.
    constexpr float ParallelLimit = 1e-4f;
    /// Scale handles end in a cube of this half size, relative to the handle length.
    constexpr float ScaleCubeHalfSize = 0.06f;
    constexpr float CenterMarkerSize = 0.12f;
    /// A drag cannot scale below this.
    constexpr float MinScaleFactor = 0.01f;
    /// The mouse must start a scale drag at least this far along the handle, relative to its length.
    constexpr float MinScaleStart = 0.05f;

    constexpr std::array<math::Vector4, 3> AxisColors { {
        { 0.95f, 0.25f, 0.25f, 1.0f },
        { 0.3f, 0.9f, 0.3f, 1.0f },
        { 0.3f, 0.5f, 1.0f, 1.0f },
    } };
    constexpr math::Vector4 ActiveColor { 1.0f, 0.85f, 0.1f, 1.0f };
    constexpr math::Vector4 CenterColor { 0.95f, 0.95f, 0.95f, 1.0f };
    constexpr std::array AllAxes { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };

    [[nodiscard]] size_t AxisIndex(GizmoAxis axis) noexcept { return static_cast<size_t>(axis) - 1; }

    [[nodiscard]] Vector3 Column(const Matrix4x4& matrix, size_t column) noexcept
    {
        return { matrix(0, column), matrix(1, column), matrix(2, column) };
    }

    /// Parameter s of the point of the line origin + axis * s closest to the ray; nullopt when
    /// they are parallel. axis has unit length.
    [[nodiscard]] std::optional<float> ClosestOnLine(const ViewRay& ray, const Vector3& origin, const Vector3& axis) noexcept
    {
        const float cosine = axis.Dot(ray.direction);
        const float denominator = 1.0f - cosine * cosine;
        if (denominator < ParallelLimit)
            return std::nullopt;
        const Vector3 toOrigin = origin - ray.origin;
        return (cosine * toOrigin.Dot(ray.direction) - toOrigin.Dot(axis)) / denominator;
    }

    /// Distance from the ray to the segment origin + axis * [0, length].
    [[nodiscard]] float DistanceToSegment(const ViewRay& ray, const Vector3& origin, const Vector3& axis, float length) noexcept
    {
        const float along = std::clamp(ClosestOnLine(ray, origin, axis).value_or(0.0f), 0.0f, length);
        const Vector3 onSegment = origin + axis * along;
        const float onRay = (std::max)((onSegment - ray.origin).Dot(ray.direction), 0.0f);
        return (onSegment - (ray.origin + ray.direction * onRay)).Length();
    }

    /// Where the ray meets the plane through center with the unit normal.
    [[nodiscard]] std::optional<Vector3> HitPlane(const ViewRay& ray, const Vector3& center, const Vector3& normal) noexcept
    {
        const float cosine = ray.direction.Dot(normal);
        if (std::abs(cosine) < ParallelLimit)
            return std::nullopt;
        const float distance = (center - ray.origin).Dot(normal) / cosine;
        if (distance < 0.0f)
            return std::nullopt;
        return ray.origin + ray.direction * distance;
    }

    /// Unit axes of the handles: the node's own ones in local space and for scale.
    [[nodiscard]] std::array<Vector3, 3> HandleAxes(const Matrix4x4& world, const GizmoSettings& settings) noexcept
    {
        std::array<Vector3, 3> axes { { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
        if (settings.space == GizmoSpace::World && settings.operation != GizmoOperation::Scale)
            return axes;
        for (size_t index = 0; index < axes.size(); ++index) {
            const Vector3 column = Column(world, index).Normalized();
            // A node scaled to zero along an axis keeps the world one
            if (column.Length() > 0.0f)
                axes[index] = column;
        }
        return axes;
    }

    [[nodiscard]] Matrix4x4 RotationAround(const Vector3& center, const Vector3& axis, float radians) noexcept
    {
        const Matrix4x4 rotation = math::RotationMatrix(math::QuaternionFromAxisAngle(axis, radians));
        return Matrix4x4::Translation(center) * rotation * Matrix4x4::Translation(center * -1.0f);
    }

    /// Signed angle from one unit vector to another around the unit axis, right-handed.
    [[nodiscard]] float AngleAround(const Vector3& from, const Vector3& to, const Vector3& axis) noexcept
    {
        return std::atan2(from.Cross(to).Dot(axis), from.Dot(to));
    }

} // namespace

TransformGizmo::TransformGizmo(DebugDraw& debugDraw)
    : m_channel(debugDraw.RegisterChannel(ChannelName, "Gizmo", true))
{
}

std::optional<Matrix4x4> TransformGizmo::Update(const Matrix4x4& world, const GizmoSettings& settings, const GizmoInput& input)
{
    const Vector3 center = Column(world, 3);
    const float size = (std::max)((center - input.cameraPosition).Length() * ScreenFraction, ParallelLimit);
    m_handles = Handles { center, HandleAxes(world, settings), size, settings.operation };

    if (m_drag && !input.down)
        m_drag.reset();
    if (m_drag) {
        m_hovered = GizmoAxis::None;
        return DragTo(*m_drag, input.ray);
    }
    m_hovered = input.overView ? FindHovered(input.ray) : GizmoAxis::None;
    if (input.pressed && m_hovered != GizmoAxis::None)
        m_drag = StartDrag(m_hovered, world, input.ray);
    return std::nullopt;
}

void TransformGizmo::Reset() noexcept
{
    m_handles.reset();
    m_drag.reset();
    m_hovered = GizmoAxis::None;
}

GizmoAxis TransformGizmo::FindHovered(const ViewRay& ray) const
{
    const Handles& handles = *m_handles;
    GizmoAxis hovered = GizmoAxis::None;
    float closest = PickRadius * handles.size;
    for (const GizmoAxis axis : AllAxes) {
        const Vector3& direction = handles.axes[AxisIndex(axis)];
        float distance = closest;
        if (handles.operation == GizmoOperation::Rotate) {
            if (const auto hit = HitPlane(ray, handles.center, direction))
                distance = std::abs((*hit - handles.center).Length() - handles.size);
        } else {
            distance = DistanceToSegment(ray, handles.center, direction, handles.size);
        }
        if (distance < closest) {
            closest = distance;
            hovered = axis;
        }
    }
    return hovered;
}

std::optional<TransformGizmo::Drag> TransformGizmo::StartDrag(GizmoAxis axis, const Matrix4x4& world, const ViewRay& ray) const
{
    const Handles& handles = *m_handles;
    const Vector3& direction = handles.axes[AxisIndex(axis)];
    Drag drag { axis, world, handles };
    if (handles.operation == GizmoOperation::Rotate) {
        const auto hit = HitPlane(ray, handles.center, direction);
        if (!hit)
            return std::nullopt;
        drag.startDirection = (*hit - handles.center).Normalized();
        return drag;
    }
    const auto along = ClosestOnLine(ray, handles.center, direction);
    if (!along || (handles.operation == GizmoOperation::Scale && *along < MinScaleStart * handles.size))
        return std::nullopt;
    drag.startParameter = *along;
    return drag;
}

std::optional<Matrix4x4> TransformGizmo::DragTo(const Drag& drag, const ViewRay& ray)
{
    const Handles& start = drag.startHandles;
    const size_t index = AxisIndex(drag.axis);
    const Vector3& axis = start.axes[index];
    if (start.operation == GizmoOperation::Rotate) {
        const auto hit = HitPlane(ray, start.center, axis);
        if (!hit)
            return std::nullopt;
        const float angle = AngleAround(drag.startDirection, (*hit - start.center).Normalized(), axis);
        return RotationAround(start.center, axis, angle) * drag.startWorld;
    }
    const auto along = ClosestOnLine(ray, start.center, axis);
    if (!along)
        return std::nullopt;
    if (start.operation == GizmoOperation::Translate)
        return Matrix4x4::Translation(axis * (*along - drag.startParameter)) * drag.startWorld;

    std::array<float, 3> factors { 1.0f, 1.0f, 1.0f };
    factors[index] = (std::max)(*along / drag.startParameter, MinScaleFactor);
    return drag.startWorld * Matrix4x4::Scaling({ factors[0], factors[1], factors[2] });
}

void TransformGizmo::Draw(DebugDraw& debugDraw) const
{
    if (!m_handles || !debugDraw.IsEnabled(m_channel))
        return;
    const Handles& handles = *m_handles;
    const GizmoAxis active = m_drag ? m_drag->axis : m_hovered;
    for (const GizmoAxis axis : AllAxes) {
        const size_t index = AxisIndex(axis);
        const DebugPen pen { .color = axis == active ? ActiveColor : AxisColors[index], .depth = render::DebugDepth::OnTop };
        const Vector3 end = handles.center + handles.axes[index] * handles.size;
        switch (handles.operation) {
        case GizmoOperation::Translate:
            debugDraw.Arrow(m_channel, handles.center, end, pen);
            break;
        case GizmoOperation::Rotate:
            debugDraw.Circle(m_channel, handles.center, handles.axes[index], handles.size, pen);
            break;
        case GizmoOperation::Scale:
            debugDraw.Line(m_channel, handles.center, end, pen);
            debugDraw.Box(m_channel, Matrix4x4::Translation(end), Vector3 { 1.0f, 1.0f, 1.0f } * (ScaleCubeHalfSize * handles.size), pen);
            break;
        }
    }
    debugDraw.Point(m_channel, handles.center, CenterMarkerSize * handles.size, { .color = CenterColor, .depth = render::DebugDepth::OnTop });
}

} // namespace elm::graphics
