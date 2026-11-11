#include "editor/ProjectSettingsPanel.hpp"

#include "editor/ui/EditorWidgets.hpp"

#include "imgui.h"

#include <utility>

namespace elm::editor {

    namespace {

        constexpr const char* NoneLabel = "(none)";

    } // namespace

    ProjectSettingsPanel::ProjectSettingsPanel(const project::Project& project, SetProjectEntry setEntry)
        : m_project(project)
        , m_setEntry(std::move(setEntry))
        , m_assets(project.Assets())
    {
    }

    void ProjectSettingsPanel::Draw()
    {
        ImGui::TextUnformatted(m_project.Name().c_str());
        ImGui::TextDisabled("%s", m_project.File().string().c_str());
        if (ui::BeginPropertyGrid()) {
            DrawStartupScene();
            DrawMainModule();
            ui::EndPropertyGrid();
        }
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
    }

    void ProjectSettingsPanel::DrawStartupScene()
    {
        const project::ProjectEntry& entry = m_project.Desc().entry;
        ui::PropertyRow("Startup scene");
        const String current = entry.startupScene ? entry.startupScene->Text() : String(NoneLabel);
        if (!ImGui::BeginCombo("##StartupScene", current.c_str()))
            return;
        if (ImGui::IsWindowAppearing())
            m_assets.Refresh();
        std::optional<project::ProjectEntry> chosen;
        if (ImGui::Selectable(NoneLabel, !entry.startupScene)) {
            chosen = entry;
            chosen->startupScene.reset();
        }
        for (const project::AssetPath& scene : m_assets.FilesOf(project::AssetType::Scene)) {
            if (ImGui::Selectable(scene.Text().c_str(), entry.startupScene == scene)) {
                chosen = entry;
                chosen->startupScene = scene;
            }
        }
        ImGui::EndCombo();
        if (chosen)
            Apply(std::move(*chosen));
    }

    void ProjectSettingsPanel::DrawMainModule()
    {
        const project::ProjectEntry& entry = m_project.Desc().entry;
        ui::PropertyRow("Main module");
        const String current = entry.mainModule.empty() ? String(NoneLabel) : entry.mainModule;
        if (!ImGui::BeginCombo("##MainModule", current.c_str()))
            return;
        if (ImGui::IsWindowAppearing())
            m_assets.Refresh();
        std::optional<project::ProjectEntry> chosen;
        if (ImGui::Selectable(NoneLabel, entry.mainModule.empty())) {
            chosen = entry;
            chosen->mainModule.clear();
        }
        for (const String& module : m_assets.ScriptModules()) {
            if (ImGui::Selectable(module.c_str(), entry.mainModule == module)) {
                chosen = entry;
                chosen->mainModule = module;
            }
        }
        ImGui::EndCombo();
        if (chosen)
            Apply(std::move(*chosen));
    }

    void ProjectSettingsPanel::Apply(project::ProjectEntry entry)
    {
        if (!entry.startupScene && entry.mainModule.empty()) {
            m_status = "The game needs a startup scene or a main module: the entry stays as it was.";
            return;
        }
        const auto saved = m_setEntry(std::move(entry));
        m_status = saved ? String {} : saved.error().message;
    }

} // namespace elm::editor
