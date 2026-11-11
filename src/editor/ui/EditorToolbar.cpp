#include "editor/ui/EditorToolbar.hpp"

#include "script/EngineContext.hpp"

#include "imgui.h"

#include <algorithm>

namespace elm {

    namespace {

        const ImVec4 PlayingColor { 1.0f, 0.62f, 0.25f, 1.0f };
        const ImVec4 PlayColor { 0.4f, 0.85f, 0.5f, 1.0f };

        // Icons: symbols of the icon font (EditorStyle), in UTF-8; in a label the id comes after ###
        constexpr const char* PlayButton = "\xE2\x96\xB6###Play";                                 // U+25B6
        constexpr const char* StopButton = "\xE2\x96\xA0###Stop";                                 // U+25A0
        constexpr const char* PlayingMark = "\xE2\x97\x8F";                                       // U+25CF
        constexpr const char* StartMenuButton = "\xE2\x96\xBE###StartMenu";                       // U+25BE
        constexpr const char* PlaySceneItem = "\xE2\x96\xB6  Play Scene###PlayScene";             // U+25B6
        constexpr const char* PlayProjectItem = "\xE2\x96\xB7  Play Project###PlayProject";       // U+25B7
        constexpr const char* RunStandaloneItem = "\xE2\x86\x97  Run Standalone###RunStandalone"; // U+2197
        constexpr const char* StartMenuPopup = "StartMenuPopup";

        /// A square button with the icon in its color.
        [[nodiscard]] bool IconButton(const char* label, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool pressed = ImGui::Button(label, ImVec2(ImGui::GetFrameHeight(), 0.0f));
            ImGui::PopStyleColor();
            return pressed;
        }

    } // namespace

    void EditorToolbar::Draw()
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
        // The middle of the bar, or right after the menus when they reach that far: two square buttons
        const float width = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX() + ImGui::GetFontSize(), (ImGui::GetWindowWidth() - width) * 0.5f));
        // The buttons come first: they are drawn whether a key was pressed or not
        if (playing) {
            if ((IconButton(StopButton, PlayingColor) || playKey) && play.stop)
                play.stop();
            ImGui::SetItemTooltip("Stop: back to the edited scene (F5)");
            ImGui::TextColored(PlayingColor, "%s", PlayingMark);
            ImGui::SetItemTooltip("Playing: changes are undone on Stop");
            return;
        }
        if ((IconButton(PlayButton, PlayColor) || (playKey && !io.KeyShift)) && play.playScene)
            play.playScene();
        ImGui::SetItemTooltip("Play the edited scene with the project's main module (F5)");
        if (playKey && io.KeyShift && play.playProject)
            play.playProject();
        DrawStartMenu(play);
    }

    void EditorToolbar::DrawStartMenu(const script::PlayRequests& play)
    {
        if (ImGui::Button(StartMenuButton, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            ImGui::OpenPopup(StartMenuPopup);
        ImGui::SetItemTooltip("Other ways to start the game");
        if (!ImGui::BeginPopup(StartMenuPopup))
            return;
        if (ImGui::MenuItem(PlaySceneItem, "F5") && play.playScene)
            play.playScene();
        ImGui::SetItemTooltip("The edited scene with the project's main module");
        if (ImGui::MenuItem(PlayProjectItem, "Shift+F5") && play.playProject)
            play.playProject();
        ImGui::SetItemTooltip("The project's startup scene with its main module");
        if (ImGui::MenuItem(RunStandaloneItem, "Ctrl+F5", false, m_desc.runStandalone != nullptr))
            m_desc.runStandalone();
        ImGui::SetItemTooltip("The game in a process of its own, from the saved files");
        ImGui::EndPopup();
    }

} // namespace elm
