#pragma once

#include "core/Error.hpp"

namespace elm {

	class Settings;

	namespace graphics {
		class DebugDrawSystem;
	}

	namespace project {
		class Project;
	}

	namespace render {
		class FrameWriter;
		struct RenderedFrameInfo;
	}

	/// What a program puts on top of the Engine: the editor (EditorLayer) or the game alone (PlayerLayer).
	/// The engine calls it from its frame loop; it reaches the engine through the Engine& it was made with.
	/// Owner: main, outlives Engine::Shutdown. Thread: update, except OnFrameRendered (render).
	class IEngineLayer {
	public:
		virtual ~IEngineLayer() = default;

		/// Before the settings file is read.
		virtual void RegisterSettings(Settings& settings) = 0;
		/// After the window, rendering and physics are up.
		[[nodiscard]] virtual auto Init() -> EngineResult<void> = 0;
		/// With the render thread stopped, before the render system shuts down.
		virtual void Shutdown() = 0;

		/// The engine opened the project: its scripts are started.
		virtual void OnProjectOpened(const project::Project& project) = 0;
		/// Before the engine clears the scene, models and bodies of the closed project.
		virtual void OnProjectClosed() = 0;

		/// First in the update of a frame, before Play/Stop requests are applied.
		virtual void Update() = 0;
		/// The frame's overlay: the editor UI, or the game view.
		virtual void BuildOverlay(render::FrameWriter& frame) = 0;
		/// Stop asked for by the scripts.
		virtual void RequestStop() = 0;

		/// The debug drawing of the engine view, owned by the layer for as long as the engine runs; the
		/// engine asks once, in Init. Null: none at all (a game built without ELM_DEBUG_DRAW), the scripts'
		/// elm.debug draws nothing.
		[[nodiscard]] virtual graphics::DebugDrawSystem* GetDebugDraw() = 0;

		/// Render thread: the frame was drawn.
		virtual void OnFrameRendered(const render::RenderedFrameInfo& info) = 0;
	};

} // namespace elm
