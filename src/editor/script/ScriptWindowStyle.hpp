#pragma once

#include "imgui.h"

namespace elm::script::style {

    // Text colors of the Scripts window
    inline const ImVec4 CommandColor { 0.55f, 0.75f, 1.0f, 1.0f };
    inline const ImVec4 OutputColor { 0.90f, 0.90f, 0.90f, 1.0f };
    inline const ImVec4 ErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
    inline const ImVec4 InfoColor { 0.60f, 0.63f, 0.68f, 1.0f };
    inline const ImVec4 OkColor { 0.40f, 0.85f, 0.50f, 1.0f };
    inline const ImVec4 WarningColor { 1.0f, 0.78f, 0.35f, 1.0f };

} // namespace elm::script::style
