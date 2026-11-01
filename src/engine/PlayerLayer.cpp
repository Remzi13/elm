#include "engine/PlayerLayer.hpp"

#include "engine/Engine.hpp"

#include "graphics/GameView.hpp"
#include "graphics/debug/DebugDrawSystem.hpp"

#if defined(ELM_DEBUG_DRAW)
#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#endif

namespace elm {

	PlayerLayer::PlayerLayer(Engine& engine)
		: m_engine(engine)
	{
#if defined(ELM_DEBUG_DRAW)
		// The channels of the editor's physics visualizers: Physics/Colliders, Triggers, Raycasts, Contacts
		m_debugDraw = MakeUnique<graphics::DebugDrawSystem>(engine.GetSettings());
		graphics::DebugDraw& debugDraw = m_debugDraw->GetDebugDraw();
		m_debugDraw->AddVisualizer(MakeUnique<graphics::ColliderVisualizer>(engine.GetScene(), debugDraw));
		m_debugDraw->AddVisualizer(MakeUnique<graphics::PhysicsVisualizer>(engine.GetPhysics().FrameLog(), debugDraw));
#endif
	}

	PlayerLayer::~PlayerLayer() = default;

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
