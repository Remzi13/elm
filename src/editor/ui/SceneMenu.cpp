#include "editor/ui/SceneMenu.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/SceneEdits.hpp"
#include "editor/ui/EditorServices.hpp"

#include "Scene/Scene.hpp"

#include "project/Project.hpp"
#include "project/ProjectAssets.hpp"

#include "imgui.h"

#include <utility>

namespace elm {

    namespace {

        constexpr const char* ConfirmationPopup = "Unsaved Scene";
        constexpr ImVec4 ModifiedColor { 1.0f, 0.75f, 0.3f, 1.0f };
        constexpr const char* ModifiedMark = "\xE2\x97\x8F"; // U+25CF
        /// After the file name: it opens the scene list.
        constexpr const char* SceneListArrow = " \xE2\x96\xBE"; // U+25BE
        constexpr const char* SceneListId = "###SceneList";
        /// At the startup scene of the list.
        constexpr const char* StartupMark = "\xE2\x98\x85"; // U+2605
        /// The end of a status cut short: the dots of the text font.
        constexpr const char* Ellipsis = "...";

        ImVec4 StatusColor(SceneStatus::Kind kind)
        {
            switch (kind) {
            case SceneStatus::Kind::Success: return { 0.4f, 0.85f, 0.5f, 1.0f };
            case SceneStatus::Kind::Failure: return { 1.0f, 0.4f, 0.4f, 1.0f };
            case SceneStatus::Kind::Info: break;
            }
            return { 0.7f, 0.7f, 0.75f, 1.0f };
        }

    } // namespace

    void SceneMenu::SetDocument(const SceneMenuDeps& deps)
    {
        m_document = deps.document;
        m_edits = deps.edits;
        m_scene = deps.scene;
        m_selection = deps.selection;
        m_project = deps.project;
        m_sceneFiles.clear();
        m_requested.reset();
        m_pending.reset();
    }

    void SceneMenu::DrawFileItems()
    {
        // A disabled item is not clicked: the document is there in every branch below
        ImGui::BeginDisabled(m_document == nullptr);
        if (ImGui::MenuItem("New Scene"))
            RequestNew();
        if (ImGui::MenuItem("Open Scene...")) {
            m_openDialog.Open("Open Scene", ".scene", m_document->ScenesFolder());
            m_openDialog.LimitTo(m_document->AssetsFolder());
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            (void)m_document->Save();
        if (ImGui::MenuItem("Save Scene As...")) {
            m_saveDialog.SaveAs("Save Scene", ".scene", m_document->File().Filename().string().c_str(), m_document->ScenesFolder());
            m_saveDialog.LimitTo(m_document->AssetsFolder());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import glTF..."))
            m_importDialog.Open("Import glTF Scene", ".gltf;.glb", m_document->AssetsFolder());
        if (ImGui::MenuItem("Export glTF..."))
            m_exportDialog.SaveAs("Export Scene to glTF", ".gltf", "scene.gltf", m_document->ScenesFolder());
        ImGui::EndDisabled();
    }

    void SceneMenu::DrawEditMenu()
    {
        if (!ImGui::BeginMenu("Edit", m_edits != nullptr))
            return;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_edits->CanUndo()))
            (void)m_edits->Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_edits->CanRedo()))
            (void)m_edits->Redo();
        ImGui::Separator();
        // No shortcuts shown: Ctrl+D and Del are Scene Hierarchy's, while it has the focus
        const bool selected = m_scene && m_selection && m_scene->Find(m_selection->node);
        if (ImGui::MenuItem("Duplicate", nullptr, false, selected)) {
            m_selection->node = DuplicateNode(*m_scene, m_selection->node);
            m_document->Report(SceneStatus::Kind::Info, "Duplicated node.");
        }
        if (ImGui::MenuItem("Delete", nullptr, false, selected))
            m_selection->node = RemoveNodeFromPlace(*m_scene, m_selection->node);
        ImGui::EndMenu();
    }

    void SceneMenu::DrawFileName()
    {
        if (!m_document)
            return;
        const String name = m_document->ShownName() + SceneListArrow;
        const bool modified = m_document->IsModified();
        const ImGuiStyle& style = ImGui::GetStyle();
        // A menu of the bar takes its label and item spacing on both sides
        float width = ImGui::CalcTextSize(name.c_str()).x + style.ItemSpacing.x * 2.0f;
        if (modified)
            width += ImGui::CalcTextSize(ModifiedMark).x + style.ItemSpacing.x;
        const float x = ImGui::GetWindowContentRegionMax().x - width;
        DrawStatus(x - style.ItemSpacing.x);
        if (x > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(x);
        if (modified) {
            ImGui::TextColored(ModifiedColor, "%s", ModifiedMark);
            ImGui::SetItemTooltip("Unsaved changes (Ctrl+S)");
        }
        if (ImGui::BeginMenu((name + SceneListId).c_str())) {
            DrawSceneList();
            ImGui::EndMenu();
        }
    }

    void SceneMenu::DrawSceneList()
    {
        if (!m_project)
            return;
        if (ImGui::IsWindowAppearing())
            m_sceneFiles = project::ProjectAssets(m_project->Assets()).FilesOf(project::AssetType::Scene);
        ImGui::TextDisabled("Scenes of %s: %zu", m_project->Name().c_str(), m_sceneFiles.size());
        ImGui::Separator();
        const auto& startup = m_project->Desc().entry.startupScene;
        // In Play the scene is the running game: the document would refuse
        const bool playing = m_document->IsPlaying();
        for (const project::AssetPath& scene : m_sceneFiles) {
            const Path file = m_project->Resolve(scene);
            const bool open = file == m_document->File();
            const bool isStartup = startup && *startup == scene;
            if (ImGui::MenuItem(scene.Text().c_str(), isStartup ? StartupMark : nullptr, open, !playing) && !open)
                RequestOpen(file);
            if (isStartup)
                ImGui::SetItemTooltip("Startup scene: Play Project (Shift+F5) starts here");
        }
        if (playing)
            ImGui::TextDisabled("Stop to open another scene");
    }

    void SceneMenu::DrawStatus(float right)
    {
        const SceneStatus& status = m_document->Status();
        const float room = right - ImGui::GetCursorPosX();
        const float ellipsisWidth = ImGui::CalcTextSize(Ellipsis).x;
        if (status.text.empty() || room <= ellipsisWidth)
            return;
        // Against the file name. A longer text is cut short: what is drawn is what takes room in the bar, so the
        // file name stays at its place; the tooltip has the text whole
        String shown = status.text;
        if (ImGui::CalcTextSize(shown.c_str()).x > room) {
            const char* cut = nullptr;
            const char* text = status.text.c_str();
            (void)ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), room - ellipsisWidth, 0.0f, text, text + status.text.size(), &cut);
            shown = String(text, cut) + Ellipsis;
        }
        ImGui::SetCursorPosX(right - ImGui::CalcTextSize(shown.c_str()).x);
        ImGui::TextColored(StatusColor(status.kind), "%s", shown.c_str());
        ImGui::SetItemTooltip("%s", status.text.c_str());
    }

    void SceneMenu::DrawPopups()
    {
        if (!m_document)
            return;
        DrawDialogs();
        if (m_requested) {
            Replacement requested = std::move(*m_requested);
            m_requested.reset();
            // In Play nothing gets replaced: the document says so without a question first
            if (m_document->IsModified() && !m_document->IsPlaying()) {
                m_pending = std::move(requested);
                ImGui::OpenPopup(ConfirmationPopup);
            } else {
                Apply(requested);
            }
        }
        DrawConfirmation();
    }

    void SceneMenu::DrawDialogs()
    {
        if (const auto file = m_openDialog.Draw())
            RequestOpen(*file);
        if (const auto file = m_saveDialog.Draw())
            (void)m_document->SaveAs(*file);
        if (const auto file = m_importDialog.Draw())
            m_document->ImportGltf(*file);
        if (const auto file = m_exportDialog.Draw())
            m_document->ExportGltf(*file);
    }

    void SceneMenu::RequestNew()
    {
        m_requested = Replacement {};
    }

    void SceneMenu::RequestOpen(const Path& file)
    {
        m_requested = Replacement { file };
    }

    void SceneMenu::Apply(const Replacement& replacement)
    {
        if (replacement.file.Empty())
            m_document->New();
        else
            m_document->Open(replacement.file);
    }

    void SceneMenu::DrawConfirmation()
    {
        if (!ImGui::BeginPopupModal(ConfirmationPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Scene %s has unsaved changes.", m_document->ShownName().c_str());
        bool close = false;
        bool apply = false;
        if (ImGui::Button("Save")) {
            // A scene that did not save stays, the status says why
            apply = m_document->Save();
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save")) {
            apply = true;
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            close = true;
        if (close) {
            if (apply && m_pending)
                Apply(*m_pending);
            m_pending.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

} // namespace elm
