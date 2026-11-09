#include "editor/ui/EditorToolbar.hpp"

#include "graphics/debug/DebugDraw.hpp"

#include "script/EngineContext.hpp"

#include "imgui.h"

#include <algorithm>

namespace elm {

    namespace {

        const ImVec4 PlayingColor { 1.0f, 0.62f, 0.25f, 1.0f };

        constexpr const char* StartMenuButton = "##StartMenu";
        constexpr const char* StartMenuPopup = "StartMenuPopup";

    } // namespace

    void EditorToolbar::Draw()
    {
        DrawDebugMenu();
        DrawPlayControls();
    }

    void EditorToolbar::DrawDebugMenu()
    {
        if (!m_desc.debugDraw || !ImGui::BeginMenu("Debug"))
            return;
        // Checkbox per debug draw channel, grouped by the part of the name before the first '/'
        String group;
        for (const auto& channel : m_desc.debugDraw->GetChannels()) {
            const String channelGroup = channel.name.substr(0, channel.name.find('/'));
            if (channelGroup != group) {
                ImGui::SeparatorText(channelGroup.c_str());
                group = channelGroup;
            }
            bool enabled = channel.enabled;
            ImGui::PushID(channel.name.c_str());
            if (ImGui::Checkbox(channel.label.c_str(), &enabled))
                m_desc.debugDraw->SetEnabled(channel.channel, enabled);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }

    void EditorToolbar::DrawPlayControls()
    {
        if (!m_desc.play)
            return;
        const script::PlayRequests& play = *m_desc.play;
        const ImGuiIO& io = ImGui::GetIO();
        const bool f5 = !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false);
        if (f5 && io.KeyCtrl && m_desc.runStandalone)
            m_desc.runStandalone();
        const bool playKey = f5 && !io.KeyCtrl;
        const bool playing = play.isPlaying && play.isPlaying();
        // The middle of the bar, or right after the menus when they reach that far
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* label = playing ? "Stop" : "Play";
        const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x + ImGui::GetFrameHeight();
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX() + ImGui::GetFontSize(), (ImGui::GetWindowWidth() - width) * 0.5f));
        // The buttons come first: they are drawn whether a key was pressed or not
        if (playing) {
            if ((ImGui::Button(label) || playKey) && play.stop)
                play.stop();
            ImGui::SetItemTooltip("Back to the edited scene (F5)");
            ImGui::TextColored(PlayingColor, "Playing");
            ImGui::SetItemTooltip("Changes are undone on Stop");
            return;
        }
        if ((ImGui::Button(label) || (playKey && !io.KeyShift)) && play.playScene)
            play.playScene();
        ImGui::SetItemTooltip("The edited scene with the project's main module (F5)");
        if (playKey && io.KeyShift && play.playProject)
            play.playProject();
        DrawStartMenu(play);
    }

    void EditorToolbar::DrawStartMenu(const script::PlayRequests& play)
    {
        if (ImGui::ArrowButton(StartMenuButton, ImGuiDir_Down))
            ImGui::OpenPopup(StartMenuPopup);
        ImGui::SetItemTooltip("Other ways to start the game");
        if (!ImGui::BeginPopup(StartMenuPopup))
            return;
        if (ImGui::MenuItem("Play Scene", "F5") && play.playScene)
            play.playScene();
        ImGui::SetItemTooltip("The edited scene with the project's main module");
        if (ImGui::MenuItem("Play Project", "Shift+F5") && play.playProject)
            play.playProject();
        ImGui::SetItemTooltip("The project's startup scene with its main module");
        if (ImGui::MenuItem("Run Standalone", "Ctrl+F5", false, m_desc.runStandalone != nullptr))
            m_desc.runStandalone();
        ImGui::SetItemTooltip("The game in a process of its own, from the saved files");
        ImGui::EndPopup();
    }

} // namespace elm
