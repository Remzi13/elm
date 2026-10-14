#pragma once

namespace elm {

	// Dark gray editor theme: neutral gray panels and headers, blue for hover, selection and checks.
	// Call with an ImGui context; the fonts must be added before the font atlas is built.
	void ApplyEditorStyle();
	void LoadEditorFonts();

} // namespace elm
