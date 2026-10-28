#include "editor/ProjectSettingsPanel.hpp"

#include "imgui.h"

#include <utility>

namespace elm::editor {

    namespace {

        constexpr const char* NoneLabel = "(none)";
        const ImVec4 ErrorColor { 1.0f, 0.4f, 0.4f, 1.0f };

        bool SameEntry(const project::ProjectEntry& left, const project::ProjectEntry& right)
        {
            return left.startupScene == right.startupScene && left.mainModule == right.mainModule;
        }

    } // namespace

    ProjectSettingsPanel::ProjectSettingsPanel(const project::Project& project, SetProjectEntry setEntry,
        const project::ProjectAssets& assets)
        : m_project(project)
        , m_setEntry(std::move(setEntry))
        , m_assets(assets)
        , m_entry(project.Desc().entry)
        , m_seenEntry(project.Desc().entry)
    {
    }

    void ProjectSettingsPanel::Draw()
    {
        TakeOutsideChanges();
        ImGui::Text("Project: %s", m_project.Name().c_str());
        ImGui::TextDisabled("%s", m_project.File().string().c_str());
        ImGui::SeparatorText("Entry");
        DrawStartupScene();
        DrawMainModule();
        if (!m_entry.startupScene && m_entry.mainModule.empty())
            ImGui::TextColored(ErrorColor, "The game needs a startup scene or a main module");
        DrawButtons();
        if (!m_status.empty())
            ImGui::TextDisabled("%s", m_status.c_str());
    }

    void ProjectSettingsPanel::TakeOutsideChanges()
    {
        const project::ProjectEntry& current = m_project.Desc().entry;
        if (SameEntry(current, m_seenEntry))
            return;
        // Edits of the tab stay; an untouched tab follows the project
        if (SameEntry(m_entry, m_seenEntry))
            m_entry = current;
        m_seenEntry = current;
    }

    void ProjectSettingsPanel::DrawStartupScene()
    {
        const String current = m_entry.startupScene ? m_entry.startupScene->Text() : String(NoneLabel);
        if (!ImGui::BeginCombo("Startup scene", current.c_str()))
            return;
        if (ImGui::Selectable(NoneLabel, !m_entry.startupScene))
            m_entry.startupScene.reset();
        for (const project::AssetPath& scene : m_assets.FilesOf(project::AssetType::Scene)) {
            if (ImGui::Selectable(scene.Text().c_str(), m_entry.startupScene == scene))
                m_entry.startupScene = scene;
        }
        ImGui::EndCombo();
    }

    void ProjectSettingsPanel::DrawMainModule()
    {
        const String current = m_entry.mainModule.empty() ? String(NoneLabel) : m_entry.mainModule;
        if (!ImGui::BeginCombo("Main module", current.c_str()))
            return;
        if (ImGui::Selectable(NoneLabel, m_entry.mainModule.empty()))
            m_entry.mainModule.clear();
        for (const String& module : m_assets.ScriptModules()) {
            if (ImGui::Selectable(module.c_str(), m_entry.mainModule == module))
                m_entry.mainModule = module;
        }
        ImGui::EndCombo();
    }

    void ProjectSettingsPanel::DrawButtons()
    {
        const bool valid = m_entry.startupScene || !m_entry.mainModule.empty();
        ImGui::BeginDisabled(!valid || !IsChanged());
        if (ImGui::Button("Save")) {
            const auto saved = m_setEntry(m_entry);
            m_status = saved ? "Saved to " + String(m_project.File().Filename().string().c_str()) : saved.error().message;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!IsChanged());
        if (ImGui::Button("Revert"))
            m_entry = m_project.Desc().entry;
        ImGui::EndDisabled();
    }

    bool ProjectSettingsPanel::IsChanged() const
    {
        return !SameEntry(m_project.Desc().entry, m_entry);
    }

} // namespace elm::editor
