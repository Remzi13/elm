#include "script/PlayMode.hpp"

#include "script/ScriptSystem.hpp"

#include "Scene/SceneSnapshot.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "physics/PhysicsSystem.hpp"

#include <utility>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "PlayMode";

    } // namespace

    PlayMode::PlayMode(Scene& scene, graphics::ModelManager* models, PhysicsSystem* physics, ScriptSystem& scripts) noexcept
        : m_scene(scene)
        , m_models(models)
        , m_physics(physics)
        , m_scripts(scripts)
    {
    }

    PlayMode::~PlayMode() = default;

    void PlayMode::RegisterSettings(Settings& settings)
    {
        settings.Register(Settings::Category::Script, PlayOnStartSetting, "Start in Play mode", false);
    }

    void PlayMode::Play()
    {
        if (m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        m_snapshot = MakeUnique<SceneSnapshot>(SceneSnapshot::Take(m_scene, m_models));
        m_playing = true;
        LOG_MESSAGE(log::Category::Script, LogSource, "Play: entity scripts and physics run, Stop brings the scene back");
        // The entity scripts start on the next update
        m_scripts.OnPlay();
    }

    void PlayMode::Stop()
    {
        if (!m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        // The scripts end while the scene they played in is still there
        m_scripts.OnStop();
        m_playing = false;
        if (m_physics)
            m_physics->Clear();
        if (m_snapshot)
            std::move(*m_snapshot).Restore(m_scene, m_models);
        m_snapshot.reset();
        LOG_MESSAGE(log::Category::Script, LogSource, "Stop: the scene is back as Play found it");
    }

    void PlayMode::ApplyRequest()
    {
        const Request request = std::exchange(m_request, Request::None);
        if (request == Request::Play)
            Play();
        else if (request == Request::Stop)
            Stop();
    }

    PlayRequests PlayMode::Requests()
    {
        return { [this] { RequestPlay(); }, [this] { RequestStop(); }, [this] { return IsPlaying(); } };
    }

} // namespace elm::script
