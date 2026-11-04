#pragma once

#include "core/Std.hpp"

#include "imgui.h"

namespace elm::ui {

    /// Widgets of the editor's windows, on top of ImGui: call them inside a window. Thread: update (UI).

    [[nodiscard]] bool InputString(const char* label, String& text, ImGuiInputTextFlags flags = 0);

    /// A button that shows whether its mode is on.
    [[nodiscard]] bool ToggleButton(const char* label, bool active);
    [[nodiscard]] bool SmallToggleButton(const char* label, bool active);

    /// Rows of a label and its value: the labels keep one column, so the values line up and a long label is clipped
    /// in its column instead of pushing the value out. Only when it returns true: PropertyRow... EndPropertyGrid.
    [[nodiscard]] bool BeginPropertyGrid();
    void EndPropertyGrid();
    /// A new row with the label: the next widget is its value, of the width of the column. Name that widget ##...
    void PropertyRow(const char* label);

    /// Three drags for x, y, z of one width, marked with the colors of the axes.
    [[nodiscard]] bool DragAxes(const char* id, float* values, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");

} // namespace elm::ui
