#include "graphics/ui/LogWindow.hpp"

#include "core/Log.hpp"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace elm {

    namespace {

        char const* getSeverityName(log::Severity severity)
        {
            switch (severity)
            {
            case log::Severity::Info: return "INFO";
            case log::Severity::Warning: return "WARNING";
            case log::Severity::Error: return "ERROR";
            case log::Severity::FatalError: return "FATAL";
            }
            return "UNKNOWN";
        }

        ImVec4 getSeverityColor(log::Severity severity)
        {
            switch (severity)
            {
            case log::Severity::Info: return ImVec4(0.72f, 0.82f, 0.94f, 1.0f);
            case log::Severity::Warning: return ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
            case log::Severity::Error: return ImVec4(1.0f, 0.35f, 0.32f, 1.0f);
            case log::Severity::FatalError: return ImVec4(1.0f, 0.25f, 0.72f, 1.0f);
            }
            return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }

        ImVec4 getCategoryColor(log::Category category)
        {
            switch (category)
            {
            case log::Category::Render: return ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
            case log::Category::Physics: return ImVec4(0.35f, 0.88f, 0.55f, 1.0f);
            case log::Category::Core: return ImVec4(0.78f, 0.55f, 1.0f, 1.0f);
            case log::Category::Invalid: return ImVec4(0.68f, 0.68f, 0.68f, 1.0f);
            }
            return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        }

        char const* getCategoryName(log::Category category)
        {
            switch (category)
            {
            case log::Category::Render: return "RENDER";
            case log::Category::Physics: return "PHYSICS";
            case log::Category::Core: return "CORE";
            case log::Category::Invalid: return "INVALID";
            }
            return "UNKNOWN";
        }

        int getCategoryIndex(log::Category category)
        {
            switch (category)
            {
            case log::Category::Render: return 0;
            case log::Category::Physics: return 1;
            case log::Category::Core: return 2;
            case log::Category::Invalid: return 3;
            }
            return -1;
        }

        String formatTimestamp(std::chrono::system_clock::time_point timestamp)
        {
            auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                timestamp.time_since_epoch()).count();
            auto const time = std::chrono::system_clock::to_time_t(timestamp);
            std::tm localTime{};
#if defined(_WIN32)
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03lld",
                localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
                static_cast<long long>((milliseconds % 1000 + 1000) % 1000));
            return buffer;
        }

        void appendSingleLine(String& destination, String const& text)
        {
            for (char character : text)
                destination += character == '\n' || character == '\r' ? ' ' : character;
        }

    } // namespace

    void LogWindow::Render(ImGuiWindowContext&)
    {
        if (!m_visible)
            return;

        if (m_maxMessages != static_cast<int>(log::getMaxMessages()))
            m_maxMessages = static_cast<int>((std::min)(log::getMaxMessages(), static_cast<std::size_t>(1000000)));

        ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear"))
            log::clearMessages();
        auto const messages = log::getMessages();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu messages", messages.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputInt("Max messages", &m_maxMessages))
        {
            m_maxMessages = (std::clamp)(m_maxMessages, 1, 1000000);
            log::setMaxMessages(static_cast<std::size_t>(m_maxMessages));
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);

        ImGui::TextDisabled("Severity");
        const char* severityNames[] = { "Info", "Warning", "Error", "Fatal" };
        for (std::size_t i = 0; i < m_severityEnabled.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            ImGui::Checkbox(severityNames[i], &m_severityEnabled[i]);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("  Category");
        const char* categoryNames[] = { "Render", "Physics", "Core", "Invalid" };
        for (std::size_t i = 0; i < m_categoryEnabled.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            ImGui::Checkbox(categoryNames[i], &m_categoryEnabled[i]);
        }

        ImGui::Separator();
        ImGui::TextDisabled("%zu / %d", messages.size(), m_maxMessages);

        if (ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), true,
            ImGuiWindowFlags_HorizontalScrollbar))
        {
            bool const wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
            for (auto const& entry : messages)
            {
                auto const severityIndex = static_cast<std::size_t>(entry.severity);
                if (severityIndex >= m_severityEnabled.size() || !m_severityEnabled[severityIndex])
                    continue;

                int const categoryIndex = getCategoryIndex(entry.category);
                if (categoryIndex >= 0 && !m_categoryEnabled[static_cast<std::size_t>(categoryIndex)])
                    continue;

                bool firstSegment = true;
                auto drawSegment = [&firstSegment](ImVec4 color, String const& text)
                {
                    if (!firstSegment)
                        ImGui::SameLine(0.0f, 5.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopStyleColor();
                    firstSegment = false;
                };

                drawSegment(ImVec4(0.55f, 0.58f, 0.63f, 1.0f), formatTimestamp(entry.timestamp));
                String severityLabel = "[";
                severityLabel += getSeverityName(entry.severity);
                severityLabel += "]";
                drawSegment(getSeverityColor(entry.severity), severityLabel);

                ImVec4 const categoryColor = getCategoryColor(entry.category);
                String categoryLabel = "[";
                categoryLabel += getCategoryName(entry.category);
                categoryLabel += "]";
                drawSegment(categoryColor, categoryLabel);
                if (!entry.sourceInfo.empty())
                {
                    String source;
                    source += "[";
                    appendSingleLine(source, entry.sourceInfo);
                    source += "]";
                    drawSegment(categoryColor, source);
                }

                String message;
                appendSingleLine(message, entry.message);
                drawSegment(categoryColor, message);

                if (!entry.filename.empty())
                {
                    String location;
                    location += "(";
                    appendSingleLine(location, entry.filename);
                    location += ":";
                    location += std::to_string(entry.lineNumber);
                    location += ")";
                    drawSegment(ImVec4(0.55f, 0.58f, 0.63f, 1.0f), location);
                }
            }

            if (m_autoScroll && wasAtBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

} // namespace elm
