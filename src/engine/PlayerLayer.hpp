#pragma once

#include "engine/IEngineLayer.hpp"

#include "core/Std.hpp"

namespace elm {

	class Engine;

	namespace graphics {
		class DebugDrawSystem;
	}

	/// The game alone (docs/project/Проекты.md#Плеер): the project plays from its start in the whole
	/// window, Stop ends the program. The settings are only read. Built with ELM_DEBUG_DRAW it has the debug
	/// drawing with the collider, ray and contact visualizers (channels on and off in .settings); without it
	/// there is no debug drawing at all.
	/// Part of the program elm, not of the engine library: the option is the program's.
	/// Owner: main, thread: update.
	class PlayerLayer final : public IEngineLayer {
	public:
		explicit PlayerLayer(Engine& engine);
		~PlayerLayer() override;

		PlayerLayer(const PlayerLayer&) = delete;
		PlayerLayer& operator=(const PlayerLayer&) = delete;
		PlayerLayer(PlayerLayer&&) = delete;
		PlayerLayer& operator=(PlayerLayer&&) = delete;

		void RegisterSettings(Settings& settings) override;
		[[nodiscard]] auto Init() -> EngineResult<void> override;
		void Shutdown() override;

		void OnProjectOpened(const project::Project& project) override;
		void OnProjectClosed() override;

		void Update() override;
		void BuildOverlay(render::FrameWriter& frame) override;
		void RequestStop() override;

		[[nodiscard]] graphics::DebugDrawSystem* GetDebugDraw() override { return m_debugDraw.get(); }
		void OnFrameRendered(const render::RenderedFrameInfo& info) override;

	private:
		Engine& m_engine;
		/// Only in a build with ELM_DEBUG_DRAW, null otherwise.
		UniquePtr<graphics::DebugDrawSystem> m_debugDraw;
	};

} // namespace elm
