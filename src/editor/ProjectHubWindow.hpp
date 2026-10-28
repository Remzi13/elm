#pragma once

#include "core/Error.hpp"

#include "graphics/ui/FileDialog.hpp"
#include "graphics/ui/IImGuiWindow.hpp"

#include <array>
#include <functional>

namespace elm::editor {

    /// What the Project Hub asks the engine for. The requests are applied at the start of the next update.
    struct ProjectHubDesc {
        /// Project files opened before, the last one first.
        std::function<Vector<Path>()> recent;
        std::function<void(const Path& file)> open;
        /// Makes a project <location>/<name>/ and opens it; the error says why it was not made.
        std::function<EngineResult<void>(const Path& location, StringView name)> create;
        /// Where a new project goes unless another folder is typed in.
        Path defaultLocation;
    };

    /// The start of the editor (docs/project/Проекты.md#Project Hub): recent projects, Open Project and Create
    /// Project. Shown while no project is open and from the Project menu.
    /// Owner: ImGuiSystem (an editor window, it outlives projects), thread: update.
    class ProjectHubWindow final : public IImGuiWindow {
    public:
        ProjectHubWindow(Settings& settings, ProjectHubDesc desc);

        [[nodiscard]] StringView GetName() const override { return "Project Hub"; }
        void Render(ImGuiWindowContext& context) override;

    private:
        void DrawRecent();
        void DrawOpen();
        void DrawCreate();

        ProjectHubDesc m_desc;
        ui::FileDialog m_openDialog;
        std::array<char, 64> m_name { };
        std::array<char, 260> m_location { };
        String m_status;
    };

} // namespace elm::editor
