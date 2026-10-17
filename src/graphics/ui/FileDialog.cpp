#include "graphics/ui/FileDialog.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace elm::ui {

auto FileDialog::PathToString(const Path& path) -> String
{
    return String(path.string().c_str());
}

void FileDialog::Open(StringView title, StringView extension, const Path& directory)
{
    m_title = String(title);
    m_extension = String(extension);
    m_saveMode = false;
    m_saveFileName.fill('\0');
    m_selectedPath.Clear();
    SetStartDirectory(directory);
    ImGui::OpenPopup(m_title.c_str());
}

void FileDialog::SaveAs(StringView title, StringView extension, StringView defaultFileName, const Path& directory)
{
    m_title = String(title);
    m_extension = String(extension);
    m_saveMode = true;
    m_selectedPath.Clear();
    std::snprintf(m_saveFileName.data(), m_saveFileName.size(), "%.*s",
        static_cast<int>(defaultFileName.size()), defaultFileName.data());
    SetStartDirectory(directory);
    ImGui::OpenPopup(m_title.c_str());
}

void FileDialog::SetStartDirectory(const Path& directory)
{
    std::error_code error;
    if (!directory.Empty())
        FileSystem::CreateDirectories(directory, error);
    m_directory = directory.Empty() || error ? FileSystem::CurrentPath(error) : directory;
    m_error = error ? String(error.message().c_str()) : String { };
    if (!error)
        RefreshEntries();
}

void FileDialog::RefreshEntries()
{
    m_entries.clear();
    m_error.clear();

    std::error_code error;
    Directory dir(m_directory);
    auto entry = dir.Begin(error);
    if (error) {
        m_error = String(error.message().c_str());
        return;
    }

    Vector<Entry> directories;
    Vector<Entry> files;
    while (!entry.IsEnd()) {
        std::error_code typeError;
        const bool isDirectory = entry.IsDirectory(typeError);
        if (typeError) {
            m_error = String(typeError.message().c_str());
            return;
        }

        if (isDirectory) {
            directories.push_back({ entry.CurrentPath(), true });
        } else {
            const bool isRegularFile = entry.IsRegularFile(typeError);
            if (typeError) {
                m_error = String(typeError.message().c_str());
                return;
            }
            if (isRegularFile) {
                bool matches = m_extension.empty();
                if (!matches) {
                    const String ext = entry.Extension().string().c_str();
                    size_t start = 0;
                    while (start < m_extension.size()) {
                        size_t end = m_extension.find_first_of(";|, ", start);
                        if (end == String::npos)
                            end = m_extension.size();
                        if (end > start) {
                            String token = m_extension.substr(start, end - start);
                            if (!token.empty() && token[0] != '.')
                                token = "." + token;
                            if (ext.size() == token.size()) {
                                bool match = true;
                                for (size_t i = 0; i < ext.size(); ++i) {
                                    if (std::tolower(static_cast<unsigned char>(ext[i])) !=
                                        std::tolower(static_cast<unsigned char>(token[i]))) {
                                        match = false;
                                        break;
                                    }
                                }
                                if (match) {
                                    matches = true;
                                    break;
                                }
                            }
                        }
                        start = end + 1;
                    }
                }
                if (matches)
                    files.push_back({ entry.CurrentPath(), false });
            }
        }

        entry.Next(error);
        if (error) {
            m_error = String(error.message().c_str());
            return;
        }
    }

    const auto sortByName = [](const Entry& left, const Entry& right) {
        return PathToString(left.path.Filename()) < PathToString(right.path.Filename());
    };
    std::sort(files.begin(), files.end(), sortByName);
    m_entries.reserve(directories.size() + files.size());
    m_entries.insert(m_entries.end(), directories.begin(), directories.end());
    m_entries.insert(m_entries.end(), files.begin(), files.end());
}

auto FileDialog::Draw() -> std::optional<Path>
{
    if (!ImGui::BeginPopupModal(m_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return std::nullopt;

    ImGui::TextWrapped("%s", PathToString(m_directory).c_str());
    const auto parent = m_directory.Parent();
    if (parent != m_directory && ImGui::Button("..")) {
        m_directory = parent;
        m_selectedPath.Clear();
        RefreshEntries();
    }

    if (!m_error.empty()) {
        ImGui::TextWrapped("Error: %s", m_error.c_str());
    } else {
        ImGui::BeginChild("FileDialogEntries", ImVec2(480.0f, 280.0f), true);
        std::optional<Path> selectedEntry;
        for (const auto& entry : m_entries) {
            String label;
            if (entry.isDirectory)
                label = "[Folder] ";
            label += PathToString(entry.path.Filename());
            const bool selected = !m_saveMode && !entry.isDirectory && entry.path == m_selectedPath;
            if (ImGui::Selectable(label.c_str(), selected)) {
                if (entry.isDirectory) {
                    selectedEntry = entry.path;
                } else if (!m_saveMode) {
                    m_selectedPath = entry.path;
                } else {
                    String filenameStr = PathToString(entry.path.Filename());
                    std::snprintf(m_saveFileName.data(), m_saveFileName.size(), "%s",
                        filenameStr.c_str());
                }
            }
        }
        ImGui::EndChild();
        if (selectedEntry) {
            m_directory = std::move(*selectedEntry);
            m_selectedPath.Clear();
            RefreshEntries();
        }
    }

    if (m_saveMode)
        ImGui::InputText("File name", m_saveFileName.data(), m_saveFileName.size());

    std::optional<Path> selectedPath;
    const bool canConfirm = m_saveMode ? m_saveFileName[0] != '\0' : !m_selectedPath.Empty();
    if (!canConfirm)
        ImGui::BeginDisabled();
    if (ImGui::Button(m_saveMode ? "Save" : "Open")) {
        if (m_saveMode) {
            Path filename(m_saveFileName.data());
            if (filename.HsaParentPath() || filename.IsAbsolute()) {
                m_error = "Enter a file name, not a path.";
            } else {
                if (filename.Extension() != m_extension)
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