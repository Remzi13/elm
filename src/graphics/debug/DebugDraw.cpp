#include "graphics/debug/DebugDraw.hpp"

#include "core/Debug.hpp"
#include "core/Settings.hpp"

#include "render/api/FrameWriter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>

namespace elm::graphics {

namespace {

    using math::Vector3;

    /// All channels share one generation: channels are never removed.
    constexpr uint8_t ChannelGeneration = 1;
    constexpr float FullTurn = 2.0f * std::numbers::pi_v<float>;
    constexpr float HalfTurn = std::numbers::pi_v<float>;
    /// Segments of a full circle; arcs get a proportional share.
    constexpr int CircleSegments = 24;
    constexpr float ArrowHeadFraction = 0.2f;
    /// Radius of the arrow head relative to its length.
    constexpr float ArrowHeadWidth = 0.4f;
    /// Above this |direction.y| the arrow head is built around the X axis instead of Y.
    constexpr float NearlyVertical = 0.99f;

    constexpr math::Vector4 AxisXColor { 0.95f, 0.25f, 0.25f, 1.0f };
    constexpr math::Vector4 AxisYColor { 0.25f, 0.9f, 0.25f, 1.0f };
    constexpr math::Vector4 AxisZColor { 0.3f, 0.5f, 1.0f, 1.0f };

    /// Box corners are combinations of ±halfExtents: bit 0 is X, bit 1 is Y, bit 2 is Z.
    constexpr std::array<std::pair<int, int>, 12> BoxEdges { {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, // along X
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, // along Y
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // along Z
    } };

    /// Part of a circle in the plane of axisU and axisV, angles measured from axisU towards axisV.
    struct Arc {
        Vector3 center;
        Vector3 axisU;
        Vector3 axisV;
        float radius { 0.0f };
        float startAngle { 0.0f };
        float endAngle { FullTurn };
    };

    [[nodiscard]] Vector3 ToWorld(const math::Matrix4x4& transform, const Vector3& point) noexcept
    {
        const math::Vector4 world = transform.TransformPoint(point);
        return { world.x, world.y, world.z };
    }

    [[nodiscard]] Vector3 ArcPoint(const Arc& arc, float angle) noexcept
    {
        return arc.center + arc.axisU * (std::cos(angle) * arc.radius) + arc.axisV * (std::sin(angle) * arc.radius);
    }

    /// Appends the arc as line segments: two points per segment.
    void AppendArc(Vector<Vector3>& points, const Arc& arc)
    {
        const float span = arc.endAngle - arc.startAngle;
        const int segments = (std::max)(1, static_cast<int>(std::lround(CircleSegments * span / FullTurn)));
        const float step = span / static_cast<float>(segments);
        for (int segment = 0; segment < segments; ++segment) {
            const float angle = arc.startAngle + step * static_cast<float>(segment);
            points.push_back(ArcPoint(arc, angle));
            points.push_back(ArcPoint(arc, angle + step));
        }
    }

    void AppendSegment(Vector<Vector3>& points, const Vector3& from, const Vector3& to)
    {
        points.push_back(from);
        points.push_back(to);
    }

    /// Two unit axes perpendicular to the unit direction and to each other.
    [[nodiscard]] std::pair<Vector3, Vector3> PerpendicularAxes(const Vector3& direction) noexcept
    {
        const Vector3 helper = std::abs(direction.y) < NearlyVertical ? Vector3 { 0.0f, 1.0f, 0.0f } : Vector3 { 1.0f, 0.0f, 0.0f };
        const Vector3 side = direction.Cross(helper).Normalized();
        return { side, side.Cross(direction) };
    }

} // namespace

DebugDraw::DebugDraw(Settings& settings)
    : m_settings(settings)
{
}

DebugChannel DebugDraw::RegisterChannel(StringView name, StringView label, bool enabledByDefault)
{
    const String settingPath = String(ChannelSettingPrefix) + String(name);
    const auto existing = std::ranges::find(m_channels, settingPath, &Channel::settingPath);
    const auto index = static_cast<uint32_t>(std::distance(m_channels.begin(), existing));
    if (existing == m_channels.end()) {
        // A value loaded from the settings file wins over the default
        m_settings.Register(Settings::Category::Render, settingPath, label, enabledByDefault);
        const bool enabled = m_settings.Get<bool>(Settings::Category::Render, settingPath, enabledByDefault);
        m_channels.push_back({ String(name), String(label), settingPath, enabled });
    }
    return DebugChannel { index, ChannelGeneration };
}

bool DebugDraw::IsEnabled(DebugChannel channel) const
{
    return IsKnown(channel) && m_channels[channel.Index()].enabled;
}

void DebugDraw::SetEnabled(DebugChannel channel, bool enabled)
{
    if (!IsKnown(channel))
        return;
    Channel& target = m_channels[channel.Index()];
    target.enabled = enabled;
    m_settings.Set(Settings::Category::Render, target.settingPath, enabled);
}

Vector<DebugChannelInfo> DebugDraw::GetChannels() const
{
    Vector<DebugChannelInfo> channels;
    channels.reserve(m_channels.size());
    for (uint32_t index = 0; index < m_channels.size(); ++index) {
        const Channel& channel = m_channels[index];
        channels.push_back({ DebugChannel { index, ChannelGeneration }, channel.name, channel.label, channel.enabled });
    }
    std::ranges::sort(channels, {}, &DebugChannelInfo::name);
    return channels;
}

bool DebugDraw::IsKnown(DebugChannel channel) const
{
    return channel.IsValid() && channel.Generation() == ChannelGeneration && channel.Index() < m_channels.size();
}

void DebugDraw::BeginFrame(float deltaSeconds)
{
    ELM_ASSERT_THREAD(Update);
    for (Channel& channel : m_channels)
        channel.enabled = m_settings.Get<bool>(Settings::Category::Render, channel.settingPath, channel.enabled);
    for (auto& vertices : m_frameVertices)
        vertices.clear();
    for (TimedPrimitive& primitive : m_timed)
        primitive.remainingSeconds -= deltaSeconds;
    DropExpired();
    // A label of the last frame only has 0 left, so it goes too
    for (LabelEntry& entry : m_labels)
        entry.remainingSeconds -= deltaSeconds;
    std::erase_if(m_labels, [](const LabelEntry& entry) { return entry.remainingSeconds <= 0.0f; });
}

void DebugDraw::ClearChannel(DebugChannel channel)
{
    ELM_ASSERT_THREAD(Update);
    for (TimedPrimitive& primitive : m_timed) {
        if (primitive.channel == channel)
            primitive.remainingSeconds = 0.0f;
    }
    DropExpired();
    std::erase_if(m_labels, [channel](const LabelEntry& entry) { return entry.channel == channel && entry.remainingSeconds > 0.0f; });
}

void DebugDraw::PublishLabels()
{
    ELM_ASSERT_THREAD(Update);
    m_publishedLabels.clear();
    for (const LabelEntry& entry : m_labels) {
        if (IsEnabled(entry.channel))
            m_publishedLabels.push_back(entry.label);
    }
}

void DebugDraw::Text(DebugChannel channel, const Vector3& position, StringView text, const DebugPen& pen)
{
    ELM_ASSERT_THREAD(Update);
    if (!IsEnabled(channel) || text.empty())
        return;
    m_labels.push_back({ channel, { position, String(text), pen.color }, pen.duration });
}

void DebugDraw::WriteTo(render::FrameWriter& frame) const
{
    ELM_ASSERT_THREAD(Update);
    // Styles in order and every style whole: FrameWriter joins the pieces into one batch per style
    for (const render::DebugStyle style : render::AllDebugStyles) {
        frame.AddDebugVertices(style, m_frameVertices[render::DebugStyleIndex(style)]);
        WriteTimed(style, frame);
    }
}

void DebugDraw::Line(DebugChannel channel, const Vector3& from, const Vector3& to, const DebugPen& pen)
{
    const std::array points { from, to };
    Emit(channel, render::DebugTopology::Lines, points, pen);
}

void DebugDraw::Triangle(DebugChannel channel, std::span<const Vector3, 3> corners, const DebugPen& pen)
{
    Emit(channel, render::DebugTopology::Triangles, corners, pen);
}

void DebugDraw::Point(DebugChannel channel, const Vector3& position, float size, const DebugPen& pen)
{
    if (!IsEnabled(channel))
        return;
    const float half = size * 0.5f;
    m_points.clear();
    AppendSegment(m_points, position - Vector3 { half, 0.0f, 0.0f }, position + Vector3 { half, 0.0f, 0.0f });
    AppendSegment(m_points, position - Vector3 { 0.0f, half, 0.0f }, position + Vector3 { 0.0f, half, 0.0f });
    AppendSegment(m_points, position - Vector3 { 0.0f, 0.0f, half }, position + Vector3 { 0.0f, 0.0f, half });
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Arrow(DebugChannel channel, const Vector3& from, const Vector3& to, const DebugPen& pen)
{
    const Vector3 shaft = to - from;
    const float length = shaft.Length();
    if (!IsEnabled(channel) || length <= 0.0f)
        return;

    const Vector3 direction = shaft / length;
    const auto [side, up] = PerpendicularAxes(direction);
    const float headLength = length * ArrowHeadFraction;
    const float headRadius = headLength * ArrowHeadWidth;
    const Vector3 headBase = to - direction * headLength;

    m_points.clear();
    AppendSegment(m_points, from, to);
    AppendSegment(m_points, to, headBase + side * headRadius);
    AppendSegment(m_points, to, headBase - side * headRadius);
    AppendSegment(m_points, to, headBase + up * headRadius);
    AppendSegment(m_points, to, headBase - up * headRadius);
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Circle(DebugChannel channel, const Vector3& center, const Vector3& normal, float radius, const DebugPen& pen)
{
    const Vector3 unitNormal = normal.Normalized();
    if (!IsEnabled(channel) || unitNormal.Length() == 0.0f)
        return;
    const auto [axisU, axisV] = PerpendicularAxes(unitNormal);
    m_points.clear();
    AppendArc(m_points, { center, axisU, axisV, radius });
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Axes(DebugChannel channel, const math::Matrix4x4& transform, float length, const DebugPen& pen)
{
    if (!IsEnabled(channel))
        return;
    const Vector3 origin = ToWorld(transform, {});
    DebugPen axisPen = pen;
    axisPen.color = AxisXColor;
    Line(channel, origin, ToWorld(transform, { length, 0.0f, 0.0f }), axisPen);
    axisPen.color = AxisYColor;
    Line(channel, origin, ToWorld(transform, { 0.0f, length, 0.0f }), axisPen);
    axisPen.color = AxisZColor;
    Line(channel, origin, ToWorld(transform, { 0.0f, 0.0f, length }), axisPen);
}

void DebugDraw::Box(DebugChannel channel, const math::Matrix4x4& transform, const Vector3& halfExtents, const DebugPen& pen)
{
    if (!IsEnabled(channel))
        return;
    const auto corner = [&halfExtents](int bits) {
        return Vector3 { (bits & 1) ? halfExtents.x : -halfExtents.x, (bits & 2) ? halfExtents.y : -halfExtents.y,
            (bits & 4) ? halfExtents.z : -halfExtents.z };
    };
    m_points.clear();
    for (const auto& [from, to] : BoxEdges)
        AppendSegment(m_points, corner(from), corner(to));
    TransformPoints(transform);
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Sphere(DebugChannel channel, const math::Matrix4x4& transform, float radius, const DebugPen& pen)
{
    if (!IsEnabled(channel))
        return;
    const Vector3 axisX { 1.0f, 0.0f, 0.0f };
    const Vector3 axisY { 0.0f, 1.0f, 0.0f };
    const Vector3 axisZ { 0.0f, 0.0f, 1.0f };
    m_points.clear();
    AppendArc(m_points, { {}, axisX, axisY, radius });
    AppendArc(m_points, { {}, axisX, axisZ, radius });
    AppendArc(m_points, { {}, axisY, axisZ, radius });
    TransformPoints(transform);
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Capsule(DebugChannel channel, const math::Matrix4x4& transform, float radius, float halfHeight, const DebugPen& pen)
{
    if (!IsEnabled(channel))
        return;
    const Vector3 axisX { 1.0f, 0.0f, 0.0f };
    const Vector3 axisY { 0.0f, 1.0f, 0.0f };
    const Vector3 axisZ { 0.0f, 0.0f, 1.0f };
    const Vector3 top = axisY * halfHeight;
    const Vector3 bottom = axisY * -halfHeight;

    m_points.clear();
    for (const Vector3& side : { axisX * radius, axisX * -radius, axisZ * radius, axisZ * -radius })
        AppendSegment(m_points, bottom + side, top + side);
    for (const Vector3& planeAxis : { axisX, axisZ }) {
        // Upper half turn from top, lower half turn from bottom: together the rounded ends
        AppendArc(m_points, { top, planeAxis, axisY, radius, 0.0f, HalfTurn });
        AppendArc(m_points, { bottom, planeAxis, axisY, radius, HalfTurn, FullTurn });
    }
    AppendArc(m_points, { top, axisX, axisZ, radius });
    AppendArc(m_points, { bottom, axisX, axisZ, radius });
    TransformPoints(transform);
    Emit(channel, render::DebugTopology::Lines, m_points, pen);
}

void DebugDraw::Emit(DebugChannel channel, render::DebugTopology topology, std::span<const Vector3> points, const DebugPen& pen)
{
    ELM_ASSERT_THREAD(Update);
    if (!IsEnabled(channel) || points.empty())
        return;

    const uint32_t color = render::PackDebugColor(pen.color);
    const render::DebugStyle style { topology, pen.depth };
    const bool timed = pen.duration > 0.0f;
    if (timed)
        m_timed.push_back({ channel, style, static_cast<uint32_t>(m_timedVertices.size()), static_cast<uint32_t>(points.size()), pen.duration });

    Vector<render::DebugVertex>& vertices = timed ? m_timedVertices : m_frameVertices[render::DebugStyleIndex(style)];
    for (const Vector3& point : points)
        vertices.push_back({ point, color });
}

void DebugDraw::DropExpired()
{
    // Survivors move down in order, so a range never overlaps the place it moves to from behind
    size_t keptPrimitives = 0;
    uint32_t keptVertices = 0;
    for (const TimedPrimitive& primitive : m_timed) {
        if (primitive.remainingSeconds <= 0.0f)
            continue;
        const auto source = m_timedVertices.begin() + primitive.firstVertex;
        if (primitive.firstVertex != keptVertices)
            std::copy(source, source + primitive.vertexCount, m_timedVertices.begin() + keptVertices);
        m_timed[keptPrimitives] = primitive;
        m_timed[keptPrimitives].firstVertex = keptVertices;
        ++keptPrimitives;
        keptVertices += primitive.vertexCount;
    }
    m_timed.resize(keptPrimitives);
    m_timedVertices.resize(keptVertices);
}

void DebugDraw::WriteTimed(render::DebugStyle style, render::FrameWriter& frame) const
{
    const std::span<const render::DebugVertex> timedVertices = m_timedVertices;
    for (const TimedPrimitive& primitive : m_timed) {
        if (primitive.style != style || !IsEnabled(primitive.channel))
            continue;
        frame.AddDebugVertices(style, timedVertices.subspan(primitive.firstVertex, primitive.vertexCount));
    }
}

void DebugDraw::TransformPoints(const math::Matrix4x4& transform)
{
    for (Vector3& point : m_points)
        point = ToWorld(transform, point);
}

} // namespace elm::graphics
