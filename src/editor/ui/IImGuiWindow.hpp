#pragma once

#include "core/Std.hpp"
#include "core/Settings.hpp"

namespace elm {

    namespace render {
        class FrameWriter;
    }

    /// An editor window. What it works with it gets at construction (its own dependency struct, see
    /// docs/core/Системы.md#Окна редактора), the frame only in Render: a window may add its own scene views
    /// (previews) to it. Owner: ImGuiSystem. Thread: update (UI).
    class IImGuiWindow {
    public:
        IImGuiWindow(Settings& settings)
            : m_settings(settings)
        {}
        virtual ~IImGuiWindow() = default;

        virtual void Render(render::FrameWriter& frame) = 0;
        [[nodiscard]] virtual StringView GetName() const = 0;

        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        void SetVisible(bool visible) noexcept { m_visible = visible; }
        bool* GetVisiblePtr() noexcept { return &m_visible; }

    protected:
        Settings& m_settings;
        bool m_visible { true };
    };

} // namespace elm
