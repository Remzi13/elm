#include "graphics/ui/FileDialog.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace elm::ui {

void FileDialog::Open(StringView title, StringView extension)
{
    m_title = String(title);
    m_extension = String(extension);
    m_saveMode = false;
    m_saveFileName.fill('\0');
    m_selectedPath.clear();
    std::error_code error;
    m_directory = std::filesystem::current_path(error);
    m_error = error ? String(error.message().c_str()) : String{};
    if (!error)
        RefreshEntries();
    ImGui::OpenPopup(m_title.c_str());
}

void FileDialog::SaveAs(StringView title, StringView extension, StringView defaultFileName)
{
    m_title = String(title);
    m_extension = String(extension);
    m_saveMode = true;
    m_selectedPath.clear();
    std::snprintf(m_saveFileName.data(), m_saveFileName.size(), "%.*s",
        static_cast<int>(defaultFileName.size()), defaultFileName.data());
    std::error_code error;
    m_directory = std::filesystem::current_path(error);
    m_error = error ? String(error.message().c_str()) : String{};
    if (!error)
        RefreshEntries();
    ImGui::OpenPopup(m_title.c_str());
}

void FileDialog::RefreshEntries()
{
    m_entries.clear();
    m_error.clear();

    std::error_code error;
    std::filesystem::directory_iterator entry(m_directory, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        m_error = String(error.message().c_str());
        return;
    }

    Vector<Entry> directories;
    Vector<Entry> files;
    while (entry != end) {
        std::error_code typeError;
        const bool isDirectory = entry->is_directory(typeError);
        if (typeError) {
            m_error = String(typeError.message().c_str());
            return;
        }

        if (isDirectory) {
            directories.push_back({ entry->path(), true });
        } else {
            const bool isRegularFile = entry->is_regular_file(typeError);
            if (typeError) {
                m_error = String(typeError.message().c_str());
                return;
            }
            if (isRegularFile && entry->path().extension() == m_extension)
                files.push_back({ entry->path(), false });
        }

        entry.increment(error);
        if (error) {
            m_error = String(error.message().c_str());
            return;
        }
    }

    const auto sortByName = [](const Entry& left, const Entry& right) {
        return left.path.filename().string() < right.path.filename().string();
    };
    std::sort(directories.begin(), directories.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);
    m_entries.reserve(directories.size() + files.size());
    m_entries.insert(m_entries.end(), directories.begin(), directories.end());
    m_entries.insert(m_entries.end(), files.begin(), files.end());
}

auto FileDialog::Draw() -> std::optional<std::filesystem::path>
{
    if (!ImGui::BeginPopupModal(m_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return std::nullopt;

    ImGui::TextWrapped("%s", m_directory.string().c_str());
    const auto parent = m_directory.parent_path();
    if (parent != m_directory && ImGui::Button("..")) {
        m_directory = parent;
        m_selectedPath.clear();
        RefreshEntries();
    }

    if (!m_error.empty()) {
        ImGui::TextWrapped("Error: %s", m_error.c_str());
    } else {
        ImGui::BeginChild("FileDialogEntries", ImVec2(480.0f, 280.0f), true);
        std::optional<std::filesystem::path> selectedEntry;
        for (const auto& entry : m_entries) {
            String label;
            if (entry.isDirectory)
                label = "[Folder] ";
            label += entry.path.filename().string().c_str();
            const bool selected = !m_saveMode && !entry.isDirectory && entry.path == m_selectedPath;
            if (ImGui::Selectable(label.c_str(), selected)) {
                if (entry.isDirectory) {
                    selectedEntry = entry.path;
                } else if (!m_saveMode) {
                    m_selectedPath = entry.path;
                } else {
                    std::snprintf(m_saveFileName.data(), m_saveFileName.size(), "%s",
                        entry.path.filename().string().c_str());
                }
            }
        }
        ImGui::EndChild();
        if (selectedEntry) {
            m_directory = std::move(*selectedEntry);
            m_selectedPath.clear();
            RefreshEntries();
        }
    }

    if (m_saveMode)
        ImGui::InputText("File name", m_saveFileName.data(), m_saveFileName.size());

    std::optional<std::filesystem::path> selectedPath;
    const bool canConfirm = m_saveMode ? m_saveFileName[0] != '\0' : !m_selectedPath.empty();
    if (!canConfirm)
        ImGui::BeginDisabled();
    if (ImGui::Button(m_saveMode ? "Save" : "Open")) {
        if (m_saveMode) {
            std::filesystem::path filename(m_saveFileName.data());
            if (filename.has_parent_path() || filename.is_absolute()) {
                m_error = "Enter a file name, not a path.";
            } else {
                if (filename.extension() != m_extension)
                    filename += m_extension;
                selectedPath = m_directory / filename;
            }
        } else {
            selectedPath = m_selectedPath;
        }
        if (selectedPath)
            ImGui::CloseCurrentPopup();
    }
    if (!canConfirm)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return selectedPath;
}

} // namespace elm::ui
