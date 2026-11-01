#include "editor/EditMode.hpp"

#include "core/Log.hpp"

#include "Scene/SceneSnapshot.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

namespace elm {

	namespace {

		constexpr const char* LogSource = "EditMode";

	} // namespace

	EditMode::EditMode(script::PlayMode& play, Scene& scene, graphics::ModelManager& models, script::ScriptSystem& scripts)
		: m_play(play)
		, m_scene(scene)
		, m_models(models)
		, m_scripts(scripts)
	{
	}

	EditMode::~EditMode() = default;

	void EditMode::ApplyRequest()
	{
		switch (m_play.GetRequest()) {
		case script::PlayMode::Request::Play:
			Play();
			break;
		case script::PlayMode::Request::Stop:
			m_play.ApplyRequest();
			RestoreEditedScene();
			break;
		case script::PlayMode::Request::None:
			break;
		}
	}

	void EditMode::Play()
	{
		if (m_play.IsPlaying()) {
			m_play.ApplyRequest();
			return;
		}
		m_snapshot = MakeUnique<SceneSnapshot>(SceneSnapshot::Take(m_scene, &m_models));
		m_play.ApplyRequest();
		// No start for the target (the startup scene could not be read): nothing changed
		if (!m_play.IsPlaying())
			m_snapshot.reset();
	}

	void EditMode::Stop()
	{
		if (!m_play.IsPlaying())
			return;
		m_play.Stop();
		RestoreEditedScene();
	}

	void EditMode::RestoreEditedScene()
	{
		if (!m_snapshot)
			return;
		std::move(*m_snapshot).Restore(m_scene, &m_models);
		m_snapshot.reset();
		// The game's Lua state goes with it: Edit gets a fresh one, without the main module
		if (auto reset = m_scripts.Reset(); !reset)
			ERROR_MESSAGE(log::Category::Script, LogSource, "%s", reset.error().message.c_str());
		LOG_MESSAGE(log::Category::Script, LogSource, "Edit: the scene is back as Play found it");
	}

} // namespace elm
