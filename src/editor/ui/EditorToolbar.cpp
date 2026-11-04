#include "editor/ui/EditorToolbar.hpp"

#include "graphics/debug/DebugDraw.hpp"

#include "script/EngineContext.hpp"

#include "imgui.h"

namespace elm {

    namespace {

        const ImVec4 PlayingColor { 1.0f, 0.62f, 0.25f, 1.0f };

        /// Between the groups of the toolbar.
        void DrawGap()
        {
            ImGui::Dummy(ImVec2(ImGui::GetFontSize(), 0.0f));
        }

    } // namespace

    void EditorToolbar::Draw()
    {
        DrawDebugMenu();
        DrawGap();
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
        // Ctrl+F5 is Run Standalone of the Project menu
        const bool f5 = !io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F5, false);
        // The buttons come first: they are drawn whether a key was pressed or not
        if (play.isPlaying && play.isPlaying()) {
            if ((ImGui::Button("Stop") || f5) && play.stop)
                play.stop();
            ImGui::SetItemTooltip("Back to the edited scene (F5)");
            ImGui::TextColored(PlayingColor, "Playing");
            ImGui::SetItemTooltip("Changes are undone on Stop");
            return;
        }
        if ((ImGui::Button("Play Project") || (f5 && !io.KeyShift)) && play.playProject)
            play.playProject();
        ImGui::SetItemTooltip("The project's startup scene with its main module (F5)");
        if ((ImGui::Button("Play Scene") || (f5 && io.KeyShift)) && play.playScene)
            play.playScene();
        ImGui::SetItemTooltip("The edited scene with the project's main module (Shift+F5)");
    }

} // namespace elm
