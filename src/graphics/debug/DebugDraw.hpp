#pragma once

#include "core/Handler.hpp"
#include "core/Std.hpp"

#include "math/Matrix.hpp"
#include "math/Vector.hpp"

#include "render/frame/DebugGeometry.hpp"

#include <array>
#include <span>

namespace elm {
    class Settings;
}

namespace elm::render {
    class FrameWriter;
}

namespace elm::graphics {

    struct DebugChannelTag;
    /// A named group of debug primitives that is switched on and off as a whole.
    using DebugChannel = core::Handle<DebugChannelTag>;

    /// A registered channel as the UI lists it.
    struct DebugChannelInfo {
        DebugChannel channel;
        /// Path-like name, the part before the first '/' is its group ("Physics/Colliders").
        String name;
        String label;
        bool enabled { false };
    };

    /// How a debug primitive is drawn and how long it stays.
    struct DebugPen {
        math::Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
        render::DebugDepth depth { render::DebugDepth::Tested };
        /// Seconds the primitive stays on screen; 0 draws it for the current frame only.
        float duration { 0.0f };
    };

    /// Text at a point of the world, drawn by the UI over the view.
    struct DebugLabel {
        math::Vector3 position;
        String text;
        math::Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /// Collects the debug primitives of one scene view, grouped into channels. Owned by the owner
    /// of the view (EngineApp for the engine viewport); update thread only.
    /// A channel is on or off through the setting Render/Debug/Draw/<name>, so its state persists
    /// and shows in the Settings window. Primitives of a disabled or invalid channel are dropped
    /// at the call; timed primitives of a channel switched off are kept but not drawn.
    class DebugDraw {
    public:
        static constexpr StringView ChannelSettingPrefix = "Debug/Draw/";

        explicit DebugDraw(Settings& settings);

        DebugDraw(const DebugDraw&) = delete;
        DebugDraw& operator=(const DebugDraw&) = delete;
        DebugDraw(DebugDraw&&) = delete;
        DebugDraw& operator=(DebugDraw&&) = delete;

        /// Registers a channel, or returns the one already registered under this name.
        [[nodiscard]] DebugChannel RegisterChannel(StringView name, StringView label, bool enabledByDefault);
        /// False for an invalid channel. The state is read from Settings in BeginFrame.
        [[nodiscard]] bool IsEnabled(DebugChannel channel) const;
        /// Switches the channel and stores the state in its setting; ignored for an invalid channel.
        void SetEnabled(DebugChannel channel, bool enabled);
        /// Registered channels sorted by name.
        [[nodiscard]] Vector<DebugChannelInfo> GetChannels() const;

        /// Starts a frame: reads the channel states, drops the primitives of the previous frame
        /// and ages the timed ones by deltaSeconds.
        void BeginFrame(float deltaSeconds);
        /// Drops the timed primitives of the channel (Stop clears what Play scripts left behind).
        /// Primitives of the current frame stay until the next BeginFrame.
        void ClearChannel(DebugChannel channel);
        /// Adds the primitives to draw now to the current scene view of frame, one batch per style.
        void WriteTo(render::FrameWriter& frame) const;
        /// Takes the labels to show now (enabled channels) for the UI: call once the frame's
        /// primitives are all in, like WriteTo.
        void PublishLabels();
        /// The labels of the last PublishLabels. The UI builds its frame before the debug
        /// primitives are all in, so it shows these: one frame late, for every source alike.
        [[nodiscard]] std::span<const DebugLabel> GetPublishedLabels() const noexcept { return m_publishedLabels; }

        /// Text centered on position. Always over the scene: pen.depth is not used.
        void Text(DebugChannel channel, const math::Vector3& position, StringView text, const DebugPen& pen);
        void Line(DebugChannel channel, const math::Vector3& from, const math::Vector3& to, const DebugPen& pen);
        /// A filled triangle.
        void Triangle(DebugChannel channel, std::span<const math::Vector3, 3> corners, const DebugPen& pen);
        /// A cross of three axis-aligned lines of the given size centered at position.
        void Point(DebugChannel channel, const math::Vector3& position, float size, const DebugPen& pen);
        /// A line from from to to with a head at to.
        void Arrow(DebugChannel channel, const math::Vector3& from, const math::Vector3& to, const DebugPen& pen);
        /// A circle around center in the plane perpendicular to normal.
        void Circle(DebugChannel channel, const math::Vector3& center, const math::Vector3& normal, float radius, const DebugPen& pen);
        /// The X, Y and Z axes of transform (red, green, blue) of the given length; pen.color is not used.
        void Axes(DebugChannel channel, const math::Matrix4x4& transform, float length, const DebugPen& pen);

        // Wireframe shapes centered at the origin of transform, which may rotate and scale them.
        void Box(DebugChannel channel, const math::Matrix4x4& transform, const math::Vector3& halfExtents, const DebugPen& pen);
        void Sphere(DebugChannel channel, const math::Matrix4x4& transform, float radius, const DebugPen& pen);
        /// Capsule along the local Y axis: halfHeight is half the length of its cylinder.
        void Capsule(DebugChannel channel, const math::Matrix4x4& transform, float radius, float halfHeight, const DebugPen& pen);

    private:
        struct Channel {
            String name;
            String label;
            String settingPath;
            bool enabled { false };
        };

        /// True for a channel this DebugDraw registered.
        [[nodiscard]] bool IsKnown(DebugChannel channel) const;

        /// A primitive with a duration; its vertices are m_timedVertices[firstVertex, firstVertex + vertexCount).
        struct TimedPrimitive {
            DebugChannel channel;
            render::DebugStyle style;
            uint32_t firstVertex { 0 };
            uint32_t vertexCount { 0 };
            float remainingSeconds { 0.0f };
        };

        /// A label with its channel; remainingSeconds is 0 for a label of the current frame only.
        struct LabelEntry {
            DebugChannel channel;
            DebugLabel label;
            float remainingSeconds { 0.0f };
        };

        /// Adds points as primitives of the topology, in world space.
        void Emit(DebugChannel channel, render::DebugTopology topology, std::span<const math::Vector3> points, const DebugPen& pen);
        void DropExpired();
        /// Adds the vertices of the timed primitives of this style in enabled channels.
        void WriteTimed(render::DebugStyle style, render::FrameWriter& frame) const;
        /// Moves the shape just built in m_points from the space of transform to world space.
        void TransformPoints(const math::Matrix4x4& transform);

        Settings& m_settings;
        Vector<Channel> m_channels;
        /// Primitives of the current frame, indexed by render::DebugStyleIndex.
        std::array<Vector<render::DebugVertex>, render::AllDebugStyles.size()> m_frameVertices;
        Vector<TimedPrimitive> m_timed;
        Vector<render::DebugVertex> m_timedVertices;
        /// Scratch buffer the shapes build their lines in.
        Vector<math::Vector3> m_points;
        Vector<LabelEntry> m_labels;
        Vector<DebugLabel> m_publishedLabels;
    };

} // namespace elm::graphics
