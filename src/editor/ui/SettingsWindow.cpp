#include "editor/ui/SettingsWindow.hpp"

#include "editor/ui/EditorWidgets.hpp"

#include "core/Log.hpp"
#include "graphics/culling/OcclusionCulling.hpp"
#include "resource/SettingsFile.hpp"
#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

#include "imgui.h"

#include <array>
#include <utility>

namespace elm {

    namespace {

        /// A section in the list: its icon (a symbol of the icon font, EditorStyle) and name.
        struct SectionItem {
            SettingsSection section;
            const char* label;
        };
        constexpr std::array<SectionItem, 3> SectionItems { {
            { SettingsSection::Project, "\xE2\x96\xA4  Project" },     // U+25A4
            { SettingsSection::Editor, "\xE2\x9A\x99  Editor" },       // U+2699
            { SettingsSection::Rendering, "\xE2\x97\x90  Rendering" }, // U+25D0
        } };
        constexpr float SectionListWidth = 140.0f;

        /// Sizes of the culling depth buffer: one aspect, from coarse to fine.
        struct DepthBufferSize {
            uint32_t width;
            uint32_t height;
        };
        constexpr std::array<DepthBufferSize, 5> DepthBufferSizes { {
            { 64, 36 },
            { 128, 72 },
            { 256, 144 },
            { 320, 180 },
            { 512, 288 },
        } };
        constexpr float MaxDepthBias = 0.01f;

        /// A row with a checkbox for a bool setting; true when it changed.
        bool DrawFlag(Settings& settings, Settings::Category category, StringView key, const char* label, const char* tooltip)
        {
            ui::PropertyRow(label);
            bool value = settings.Get<bool>(category, key);
            ImGui::PushID(label);
            const bool changed = ImGui::Checkbox("##Value", &value);
            ImGui::PopID();
            ImGui::SetItemTooltip("%s", tooltip);
            if (changed)
                settings.Set(category, key, value);
            return changed;
        }

        /// The culling depth buffer: one of DepthBufferSizes; true when it changed.
        bool DrawDepthBufferSize(Settings& settings)
        {
            using Culling = OcclusionCulling;
            const uint32_t width = settings.Get<uint32_t>(Settings::Category::Render, Culling::ResolutionWidthSetting);
            const uint32_t height = settings.Get<uint32_t>(Settings::Category::Render, Culling::ResolutionHeightSetting);
            ui::PropertyRow("Depth buffer");
            const String current = std::to_string(width).c_str() + String("x") + std::to_string(height).c_str();
            if (!ImGui::BeginCombo("##DepthBuffer", current.c_str()))
                return false;
            bool changed = false;
            for (const DepthBufferSize& size : DepthBufferSizes) {
                const String label = std::to_string(size.width).c_str() + String("x") + std::to_string(size.height).c_str();
                if (ImGui::Selectable(label.c_str(), size.width == width && size.height == height)) {
                    settings.Set(Settings::Category::Render, Culling::ResolutionWidthSetting, size.width);
                    settings.Set(Settings::Category::Render, Culling::ResolutionHeightSetting, size.height);
                    changed = true;
                }
            }
            ImGui::EndCombo();
            return changed;
        }

    } // namespace

    SettingsWindow::SettingsWindow(Settings& settings, Path file)
        : IImGuiWindow(settings)
        , m_file(std::move(file))
    {
        m_visible = false;
    }

    void SettingsWindow::Show(SettingsSection section)
    {
        m_visible = true;
        m_section = section;
        m_bringToFront = true;
    }

    void SettingsWindow::SetProject(const project::Project& project, editor::SetProjectEntry setEntry)
    {
        m_project.emplace(project, std::move(setEntry));
    }

    void SettingsWindow::ClearProject()
    {
        m_project.reset();
    }

    void SettingsWindow::Render(render::FrameWriter&)
    {
        if (!m_visible)
            return;
        if (std::exchange(m_bringToFront, false))
            ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize(ImVec2(560.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }
        DrawSectionList();
        ImGui::SameLine();
        ImGui::BeginChild("Section");
        bool changed = false;
        if (m_section == SettingsSection::Project) {
            if (m_project)
                m_project->Draw();
            else
                ImGui::TextDisabled("No project is open");
        } else if (m_section == SettingsSection::Editor) {
            changed = DrawEditorSection();
        } else {
            changed = DrawRenderingSection();
        }
        if (changed)
            WriteFile();
        if (!m_error.empty())
            ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::EndChild();
        ImGui::End();
    }

    void SettingsWindow::DrawSectionList()
    {
        ImGui::BeginChild("Sections", ImVec2(SectionListWidth, 0.0f), true);
        for (const SectionItem& item : SectionItems) {
            if (ImGui::Selectable(item.label, m_section == item.section))
                m_section = item.section;
        }
        ImGui::EndChild();
    }

    bool SettingsWindow::DrawEditorSection()
    {
        if (!ui::BeginPropertyGrid())
            return false;
        using Category = Settings::Category;
        bool changed = DrawFlag(m_settings, Category::Script, script::PlayMode::PlayOnStartSetting, "Start in Play",
            "The editor starts playing the project right away");
        changed |= DrawFlag(m_settings, Category::Script, script::ScriptSystem::EnabledSetting, "Run scripts",
            "Off: Play starts without the main module and the scripts of entities");
        changed |= DrawFlag(m_settings, Category::Script, script::ScriptSystem::HotReloadSetting, "Hot reload",
            "A script is reloaded as soon as its file is saved");
        ui::PropertyRow("Log file");
        String logFile = m_settings.Get<String>(Category::Core, log::FileSetting);
        if (ui::InputString("##LogFile", logFile))
            m_settings.Set(Category::Core, log::FileSetting, logFile);
        // The file is opened when the editor starts
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Where the log is written too, from the next start; empty: nowhere");
        ui::EndPropertyGrid();
        return changed;
    }

    bool SettingsWindow::DrawRenderingSection()
    {
        if (!ui::BeginPropertyGrid())
            return false;
        using Culling = OcclusionCulling;
        constexpr Settings::Category Render = Settings::Category::Render;
        bool changed = DrawFlag(m_settings, Render, Culling::EnableFrustumCullingSetting, "Frustum culling",
            "Nodes out of the view are not drawn");
        changed |= DrawFlag(m_settings, Render, Culling::EnableOcclusionCullingSetting, "Occlusion culling",
            "Nodes behind others are not drawn: the CPU draws a small depth buffer of the occluders");
        changed |= DrawDepthBufferSize(m_settings);
        ImGui::SetItemTooltip("The size of the occlusion depth buffer: finer culls more and costs more");
        ui::PropertyRow("Depth bias");
        float bias = m_settings.Get<float>(Render, Culling::DepthBiasSetting);
        if (ImGui::SliderFloat("##DepthBias", &bias, 0.0f, MaxDepthBias, "%.4f"))
            m_settings.Set(Render, Culling::DepthBiasSetting, bias);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Lets a node a little behind the occluders stay visible");
        ui::EndPropertyGrid();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("The depth buffer itself and the culling statistics are in the Software Occlusion Culling Lab window.");
        ImGui::PopStyleColor();
        return changed;
    }

    void SettingsWindow::WriteFile()
    {
        const auto written = resource::WriteSettingsFile(m_settings, m_file);
        m_error = written ? String {} : written.error().message;
    }

} // namespace elm
