#pragma once

namespace elm {

	class SceneEdits;

	/// Ctrl+Z undoes, Ctrl+Y and Ctrl+Shift+Z redo a scene edit; not while a text field takes the keys (it has
	/// its own undo). For the windows that edit the scene, while they have the focus. Thread: update (UI).
	void HandleSceneEditShortcuts(SceneEdits& edits);

} // namespace elm
