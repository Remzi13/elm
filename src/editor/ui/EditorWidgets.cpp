#include "editor/ui/EditorWidgets.hpp"

#include <cstring>

namespace elm::ui {

    namespace {

        /// The ImGui id of the table of a grid, the same for all of them: sections have their own id scopes.
        constexpr const char* PropertyGridId = "Grid";
        constexpr int AxisCount = 3;
        constexpr ImU32 AxisColors[AxisCount] { IM_COL32(226, 75, 74, 255), IM_COL32(99, 153, 34, 255), IM_COL32(55, 138, 221, 255) };
        constexpr float AxisMarkWidth = 2.0f;
        /// In font sizes: fits "Angular Damping" in the editor's font.
        constexpr float LabelColumnWidth = 7.5f;

        int ResizeInputBuffer(ImGuiInputTextCallbackData* callback)
        {
            if (callback->EventFlag != ImGuiInputTextFlags_CallbackResize)
                return 0;
            auto& buffer = *static_cast<Vector<char>*>(callback->UserData);
            buffer.resize(static_cast<size_t>(callback->BufTextLen) + 1);
            callback->Buf = buffer.data();
            return 0;
        }

        void PushActiveColors()
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

    } // namespace

    bool InputString(const char* label, String& text, ImGuiInputTextFlags flags)
    {
        Vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags | ImGuiInputTextFlags_CallbackResize, ResizeInputBuffer, &buffer))
            return false;
        text.assign(buffer.data(), std::strlen(buffer.data()));
        return true;
    }

    bool ToggleButton(const char* label, bool active)
    {
        if (active)
            PushActiveColors();
        const bool pressed = ImGui::Button(label);
        if (active)
            ImGui::PopStyleColor(2);
        return pressed;
    }

    bool SmallToggleButton(const char* label, bool active)
    {
        if (active)
            PushActiveColors();
        const bool pressed = ImGui::SmallButton(label);
        if (active)
            ImGui::PopStyleColor(2);
        return pressed;
    }

    bool BeginPropertyGrid()
    {
        if (!ImGui::BeginTable(PropertyGridId, 2, ImGuiTableFlags_NoSavedSettings))
            return false;
        ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * LabelColumnWidth);
        ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void EndPropertyGrid()
    {
        ImGui::EndTable();
    }

    void PropertyRow(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    bool DragAxes(const char* id, float* values, float speed, float min, float max, const char* format)
    {
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - spacing * (AxisCount - 1)) / AxisCount;
        const ImGuiSliderFlags flags = min < max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
        bool changed = false;
        ImGui::PushID(id);
        for (int axis = 0; axis < AxisCount; ++axis) {
            ImGui::PushID(axis);
            if (axis > 0)
                ImGui::SameLine(0.0f, spacing);
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##Axis", &values[axis], speed, min, max, format, flags);
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(itemMin, ImVec2(itemMin.x + AxisMarkWidth, itemMax.y), AxisColors[axis]);
            ImGui::PopID();
        }
        ImGui::PopID();
        return changed;
    }

} // namespace elm::ui
