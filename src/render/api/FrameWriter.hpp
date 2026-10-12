#pragma once

#include "render/frame/FrameSnapshot.hpp"

namespace elm::render {

    class RenderSystem;

    /// Update-side writer for one frame. Obtained from RenderSystem::BeginFrame() and handed back
    /// with RenderSystem::SubmitFrame(). Everything written here is a copy: the update thread may
    /// change the scene right after submitting while the render thread draws the snapshot.
    class FrameWriter {
    public:
        FrameWriter() = default;
        FrameWriter(const FrameWriter&) = delete;
        FrameWriter& operator=(const FrameWriter&) = delete;
        FrameWriter(FrameWriter&& other) noexcept
            : m_frame(std::exchange(other.m_frame, nullptr))
        {
        }
        FrameWriter& operator=(FrameWriter&& other) noexcept
        {
            m_frame = std::exchange(other.m_frame, nullptr);
            return *this;
        }

        /// False once rendering has stopped; writes are then ignored.
        [[nodiscard]] bool IsValid() const noexcept { return m_frame != nullptr; }
        explicit operator bool() const noexcept { return IsValid(); }

        /// Pixel size of the main surface for this frame (the window framebuffer).
        void SetBackbufferSize(Size size)
        {
            if (m_frame)
                m_frame->backbufferSize = size;
        }

        /// Starts a new 3D view: the following Draw calls go into it. A frame may contain several views.
        void BeginSceneView(const CameraData& camera, TextureHandle colorTarget, Size size)
        {
            if (m_frame)
                m_frame->sceneViews.push_back({ camera, colorTarget, size, m_frame->drawItems.size(), 0 });
        }

        /// Adds an instance to the current scene view; ignored before the first BeginSceneView.
        void Draw(MeshHandle mesh, const Matrix4x4& transform, const Vector4& color)
        {
            if (!m_frame || !mesh.IsValid() || m_frame->sceneViews.empty())
                return;
            m_frame->drawItems.push_back({ mesh, { transform, color } });
            ++m_frame->sceneViews.back().drawCount;
        }

        void ReserveDraws(size_t count)
        {
            if (m_frame)
                m_frame->drawItems.reserve(count);
        }

        /// UI geometry captured for this frame. Only valid while IsValid().
        [[nodiscard]] OverlayFrame& Overlay() noexcept { return m_frame->overlay; }

    private:
        friend class RenderSystem;
        explicit FrameWriter(FrameSnapshot* frame) noexcept
            : m_frame(frame)
        {
        }

        FrameSnapshot* m_frame { nullptr };
    };

} // namespace elm::render
