#include "editor/ProjectWindow.hpp"

#include "graphics/ui/WindowMessages.hpp"

#include "imgui.h"

#include <cctype>
#include <utility>

namespace elm::editor {

    namespace {

        constexpr const char* NewFolderPopup = "New Folder";
        constexpr const char* AllTypes = "All";
        const ImVec4 StartupColor { 0.45f, 0.8f, 0.45f, 1.0f };

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            for (size_t start = 0; start + part.size() <= text.size(); ++start) {
                size_t index = 0;
                while (index < part.size() && std::tolower(static_cast<unsigned char>(text[start + index]))
                        == std::tolower(static_cast<unsigned char>(part[index])))
                    ++index;
                if (index == part.size())
                    return true;
            }
            return false;
        }

        /// Models can be dragged into Scene Hierarchy: a .model, or a glTF that is imported on the drop.
        bool IsDraggableModel(const project::AssetEntry& file)
        {
            return file.type == project::AssetType::Model || file.type == project::AssetType::SourceModel;
        }

    } // namespace

    ProjectWindow::ProjectWindow(Settings& settings, const project::Project& project, SetProjectEntry setEntry)
        : IImGuiWindow(settings)
        , m_project(project)
        , m_setEntry(std::move(setEntry))
        , m_assets(project.Assets())
        , m_settingsPanel(project, m_setEntry, m_assets)
    {
    }

    void ProjectWindow::Render(ImGuiWindowContext&)
    {
        if (!m_visible)
            return;
        ImGui::SetNextWindowSize(ImVec2(320.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabBar("ProjectTabs")) {
            if (ImGui::BeginTabItem("Assets")) {
                DrawAssets();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                m_settingsPanel.Draw();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        DrawNewFolderPopup();
        ImGui::End();
    }

    void ProjectWindow::DrawAssets()
    {
        DrawToolbar();
        ImGui::Separator();
        ImGui::BeginChild("ProjectAssets", ImVec2(0.0f, m_status.empty() ? 0.0f : -ImGui::GetFrameHeightWithSpacing()));
        if (IsFiltering())
            DrawFound(m_assets.Root());
        else
            DrawFolder(m_assets.Root());
        ImGui::EndChild();
        if (!m_status.empty())
            ImGui::TextDisabled("%s", m_status.c_str());
    }

    void ProjectWindow::DrawToolbar()
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##Search", "Search assets...", m_search.data(), m_search.size());
        ImGui::SetNextItemWidth(140.0f);
        const String filterName = m_typeFilter ? String(project::AssetTypeName(*m_typeFilter)) : String(AllTypes);
        if (ImGui::BeginCombo("##Type", filterName.c_str())) {
            if (ImGui::Selectable(AllTypes, !m_typeFilter))
                m_typeFilter.reset();
            for (const project::AssetType type : project::AssetTypes) {
                if (ImGui::Selectable(String(project::AssetTypeName(type)).c_str(), m_typeFilter == type))
                    m_typeFilter = type;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            m_assets.Refresh();
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) {
            m_newFolderParent = { };
            m_openNewFolder = true;
        }
    }

    void ProjectWindow::DrawFolder(const project::AssetEntry& folder)
    {
        for (const project::AssetEntry& entry : folder.children) {
            if (!entry.folder) {
                DrawFile(entry, entry.name);
                continue;
            }
            ImGui::PushID(entry.path.Text().c_str());
            const bool open = ImGui::TreeNodeEx(entry.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("New Folder...")) {
                    m_newFolderParent = entry.path;
                    m_openNewFolder = true;
                }
                ImGui::EndPopup();
            }
            if (open) {
                DrawFolder(entry);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void ProjectWindow::DrawFile(const project::AssetEntry& file, StringView label)
    {
        ImGui::PushID(file.path.Text().c_str());
        const bool startup = file.path == m_project.Desc().entry.startupScene;
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::TreeNodeEx("##File", flags, "%.*s", static_cast<int>(label.size()), label.data());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            Open(file);
        if (IsDraggableModel(file) && ImGui::BeginDragDropSource()) {
            const String path(m_project.Resolve(file.path).string().c_str());
            ImGui::SetDragDropPayload(ModelFilePayload, path.data(), path.size());
            ImGui::Text("%s", file.name.c_str());
            ImGui::EndDragDropSource();
        }
        DrawFileMenu(file);
        ImGui::SameLine();
        if (startup)
            ImGui::TextColored(StartupColor, "startup");
        else
            ImGui::TextDisabled("%s", String(project::AssetTypeName(file.type)).c_str());
        ImGui::PopID();
    }

    void ProjectWindow::DrawFound(const project::AssetEntry& folder)
    {
        for (const project::AssetEntry& entry : folder.children) {
            if (entry.folder)
                DrawFound(entry);
            else if (Matches(entry))
                DrawFile(entry, entry.path.Text());
        }
    }

    void ProjectWindow::DrawFileMenu(const project::AssetEntry& file)
    {
        if (!ImGui::BeginPopupContextItem())
            return;
        if (ImGui::MenuItem("Open"))
            Open(file);
        if (file.type == project::AssetType::Scene && ImGui::MenuItem("Set as Startup Scene"))
            SetStartupScene(file.path);
        ImGui::EndPopup();
    }

    void ProjectWindow::DrawNewFolderPopup()
    {
        if (std::exchange(m_openNewFolder, false)) {
            m_newFolderName.fill('\0');
            ImGui::OpenPopup(NewFolderPopup);
        }
        if (!ImGui::BeginPopupModal(NewFolderPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("In %s", m_newFolderParent.Empty() ? "assets" : m_newFolderParent.Text().c_str());
        ImGui::InputText("Name", m_newFolderName.data(), m_newFolderName.size());
        const String name(m_newFolderName.data());
        // One folder name: an asset path of one part
        const auto folder = name.find('/') == String::npos && name.find('\\') == String::npos
            ? project::AssetPath::FromString(m_newFolderParent.Empty() ? name : m_newFolderParent.Text() + "/" + name)
            : std::nullopt;
        ImGui::BeginDisabled(!folder);
        if (ImGui::Button("Create")) {
            std::error_code error;
            FileSystem::CreateDirectories(m_project.Resolve(*folder), error);
            m_status = error ? "Cannot create " + folder->Text() + ": " + String(error.message().c_str()) : "Created " + folder->Text();
            m_assets.Refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void ProjectWindow::Open(const project::AssetEntry& file)
    {
        const Path path = m_project.Resolve(file.path);
        std::function<void(const Path&)> open;
        if (file.type == project::AssetType::Scene) {
            if (const auto query = Request<OpenSceneFile>())
                open = query->open;
        } else if (file.type == project::AssetType::Model) {
            if (const auto query = Request<OpenModelFile>())
                open = query->open;
        } else if (file.type == project::AssetType::Script) {
            if (const auto query = Request<OpenScriptFile>())
                open = query->open;
        }
        if (!open) {
            m_status = "No editor opens " + file.name;
            return;
        }
        open(path);
        m_status = "Opened " + file.path.Text();
    }

    void ProjectWindow::SetStartupScene(const project::AssetPath& scene)
    {
        project::ProjectEntry entry = m_project.Desc().entry;
        entry.startupScene = scene;
        const auto saved = m_setEntry(std::move(entry));
        m_status = saved ? "Startup scene: " + scene.Text() : saved.error().message;
    }

    bool ProjectWindow::IsFiltering() const
    {
        return m_search[0] != '\0' || m_typeFilter.has_value();
    }

    bool ProjectWindow::Matches(const project::AssetEntry& file) const
    {
        if (m_typeFilter && file.type != *m_typeFilter)
            return false;
        return ContainsNoCase(file.name, StringView(m_search.data()));
    }

} // namespace elm::editor
