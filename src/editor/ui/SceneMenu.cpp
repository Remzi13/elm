#include "editor/ui/SceneMenu.hpp"

#include "editor/SceneDocument.hpp"
#include "editor/SceneEdits.hpp"

#include "imgui.h"

#include <utility>

namespace elm {

    namespace {

        constexpr const char* ConfirmationPopup = "Unsaved Scene";
        constexpr ImVec4 ModifiedColor { 1.0f, 0.75f, 0.3f, 1.0f };
        constexpr const char* ModifiedMark = "* unsaved";

    } // namespace

    void SceneMenu::SetDocument(SceneDocument* document, SceneEdits* edits)
    {
        m_document = document;
        m_edits = edits;
        m_requested.reset();
        m_pending.reset();
    }

    void SceneMenu::DrawMenus()
    {
        DrawSceneMenu();
        DrawEditMenu();
    }

    void SceneMenu::DrawSceneMenu()
    {
        if (!ImGui::BeginMenu("Scene", m_document != nullptr))
            return;
        if (ImGui::MenuItem("New"))
            RequestNew();
        if (ImGui::MenuItem("Open...")) {
            m_openDialog.Open("Open Scene", ".scene", m_document->ScenesFolder());
            m_openDialog.LimitTo(m_document->AssetsFolder());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            (void)m_document->Save();
        if (ImGui::MenuItem("Save As...")) {
            m_saveDialog.SaveAs("Save Scene", ".scene", m_document->File().Filename().string().c_str(), m_document->ScenesFolder());
            m_saveDialog.LimitTo(m_document->AssetsFolder());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import glTF..."))
            m_importDialog.Open("Import glTF Scene", ".gltf;.glb", m_document->AssetsFolder());
        if (ImGui::MenuItem("Export glTF..."))
            m_exportDialog.SaveAs("Export Scene to glTF", ".gltf", "scene.gltf", m_document->ScenesFolder());
        ImGui::EndMenu();
    }

    void SceneMenu::DrawEditMenu()
    {
        if (!ImGui::BeginMenu("Edit", m_edits != nullptr))
            return;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_edits->CanUndo()))
            (void)m_edits->Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_edits->CanRedo()))
            (void)m_edits->Redo();
        ImGui::EndMenu();
    }

    void SceneMenu::DrawFileName()
    {
        if (!m_document)
            return;
        const String name = m_document->ShownName();
        const bool modified = m_document->IsModified();
        const ImGuiStyle& style = ImGui::GetStyle();
        float width = ImGui::CalcTextSize(name.c_str()).x + style.ItemSpacing.x;
        if (modified)
            width += ImGui::CalcTextSize(ModifiedMark).x + style.ItemSpacing.x;
        const float x = ImGui::GetWindowContentRegionMax().x - width;
        if (x > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(x);
        ImGui::TextDisabled("%s", name.c_str());
        if (modified) {
            ImGui::TextColored(ModifiedColor, "%s", ModifiedMark);
        }
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
