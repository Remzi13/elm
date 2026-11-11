#pragma once

#include "core/Error.hpp"

#include "editor/ui/FileDialog.hpp"
#include "editor/ui/IImGuiWindow.hpp"

#include <array>
#include <functional>

namespace elm::editor {

    /// What the Project Hub asks the engine for. The requests are applied at the start of the next update.
    struct ProjectHubDesc {
        /// Project files opened before, the last one first.
        std::function<Vector<Path>()> recent;
        /// The open project's file, empty without one: opening or making another project asks first.
        std::function<Path()> openProject;
        std::function<void(const Path& file)> open;
        /// Makes a project <location>/<name>/ and opens it; the error says why it was not made.
        std::function<EngineResult<void>(const Path& location, StringView name)> create;
        /// Where a new project goes unless another folder is typed in.
        Path defaultLocation;
    };

    /// The one place where projects are opened, made and switched (docs/project/Проекты.md#Project Hub): recent
    /// projects (the open one marked, a click on it reloads it), Open Project and Create Project. With a project
    /// open each of them asks first, since unsaved changes are lost. Shown while no project is open and from
    /// Project → Open Project.
    /// Owner: ImGuiSystem (a dialog window: it outlives projects), thread: update.
    class ProjectHubWindow final : public IImGuiWindow {
    public:
        static constexpr const char* Name = "Project Hub";
        static constexpr const char* ConfirmationPopup = "Close Project";

        ProjectHubWindow(Settings& settings, ProjectHubDesc desc);

        [[nodiscard]] StringView GetName() const override { return Name; }
        void Render(render::FrameWriter& frame) override;

    private:
        void DrawRecent();
        void DrawOpen();
        void DrawCreate();
        /// Runs the action right away without an open project, after the confirmation with one.
        void Confirm(const char* button, std::function<void()> action);
        void DrawConfirmation();

        ProjectHubDesc m_desc;
        ui::FileDialog m_openDialog;
        std::array<char, 64> m_name {};
        std::array<char, 260> m_location {};
        String m_status;
        /// What the confirmation does, empty while none is asked; the label of its button.
        std::function<void()> m_confirmed;
        const char* m_confirmButton { "" };
        bool m_openConfirmation { false };
    };

} // namespace elm::editor
