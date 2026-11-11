#pragma once

#include "core/Std.hpp"

#include "editor/ui/IImGuiWindow.hpp"

namespace elm {

    class OcclusionCulling;

    /// What culling does: the occlusion depth buffer, how culled nodes are shown, the statistics. Whether culling
    /// runs and the size of its buffer are settings (SettingsWindow, Rendering).
    /// Owner: ImGuiSystem, thread: update (UI).
    class SocLabWindow final : public IImGuiWindow {
    public:
        SocLabWindow(Settings& settings, const OcclusionCulling* culling = nullptr)
            : IImGuiWindow(settings)
            , m_culling(culling)
        {}

        [[nodiscard]] StringView GetName() const override { return "Software Occlusion Culling Lab"; }
        void Render(render::FrameWriter& frame) override;

        void SetCullingSystem(const OcclusionCulling* culling) noexcept { m_culling = culling; }

    private:
        const OcclusionCulling* m_culling { nullptr };
    };

} // namespace elm
