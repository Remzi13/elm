#pragma once

#include "Scene/ComponentTypes.hpp"

namespace elm::ui {

    /// The icon of a component in the editor's windows, in UTF-8: one per component, so Scene Hierarchy and
    /// Inspector show it the same way. A symbol of the icon font (EditorStyle, IconGlyphRanges) or of the text font.
    [[nodiscard]] constexpr const char* ComponentIcon(ComponentType type) noexcept
    {
        switch (type) {
        case ComponentType::Model: return "\xE2\x96\xA3";     // U+25A3
        case ComponentType::RigidBody: return "\xE2\x9A\x9B"; // U+269B
        case ComponentType::Collider: return "\xE2\x96\xA2";  // U+25A2
        case ComponentType::Script: return "\xC6\x92";        // U+0192, of the text font
        }
        return "";
    }

} // namespace elm::ui
