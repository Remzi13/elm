#include "engine/PlayerLayer.hpp"

#include "engine/Engine.hpp"

#include "graphics/GameView.hpp"

namespace elm {

	PlayerLayer::PlayerLayer(Engine& engine)
		: m_engine(engine)
	{
	}

	void PlayerLayer::RegisterSettings(Settings&)
	{
	}

	auto PlayerLayer::Init() -> EngineResult<void>
	{
		// The game plays the project from its start: the startup scene and the main module (Play Project)
		m_engine.GetPlayMode().RequestPlay(script::PlayTarget::Project);
		return { };
	}

	void PlayerLayer::Shutdown()
	{
	}

	void PlayerLayer::OnProjectOpened(const project::Project& project)
	{
		m_engine.GetWindow().SetTitle(project.Name());
	}

	void PlayerLayer::OnProjectClosed()
	{
	}

	void PlayerLayer::Update()
	{
	}

	void PlayerLayer::BuildOverlay(render::FrameWriter& frame)
	{
		const Size framebuffer = m_engine.GetWindow().GetFramebufferSize();
		graphics::FitGameView(framebuffer, m_engine.GetViewPort(), m_engine.GetCamera());
		graphics::WriteGameOverlay(m_engine.GetViewPort(), framebuffer, frame.Overlay());
	}

	void PlayerLayer::RequestStop()
	{
		// No Edit mode to go back to
		m_engine.RequestQuit();
	}

	void PlayerLayer::OnFrameRendered(const render::RenderedFrameInfo&)
	{
	}

} // namespace elm
