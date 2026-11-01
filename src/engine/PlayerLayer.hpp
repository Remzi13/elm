#pragma once

#include "engine/IEngineLayer.hpp"

namespace elm {

	class Engine;

	/// The game alone (docs/project/Проекты.md#Плеер): the project plays from its start in the whole
	/// window, Stop ends the program. The settings are only read.
	/// Owner: main, thread: update.
	class PlayerLayer final : public IEngineLayer {
	public:
		explicit PlayerLayer(Engine& engine);

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

		void OnFrameRendered(const render::RenderedFrameInfo& info) override;

	private:
		Engine& m_engine;
	};

} // namespace elm
