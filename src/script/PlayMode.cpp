#include "script/PlayMode.hpp"

#include "script/ScriptSystem.hpp"

#include "Scene/Scene.hpp"
#include "core/Debug.hpp"
#include "model/ModelCatalog.hpp"
#include "core/Log.hpp"
#include "physics/PhysicsSystem.hpp"

#include <utility>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "PlayMode";

    } // namespace

    PlayMode::PlayMode(Scene& scene, ModelCatalog* models, PhysicsSystem* physics, ScriptSystem& scripts) noexcept
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

    void PlayMode::SetStartSource(PlayStartSource source)
    {
        m_startSource = std::move(source);
    }

    void PlayMode::Play(PlayStart start)
    {
        if (m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        // As a player starts the game: a fresh Lua state, the start scene, then the main module
        if (auto reset = m_scripts.Reset(); !reset)
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s", reset.error().message.c_str());
        if (start.scene)
            ReplaceScene(std::move(*start.scene));
        m_playing = true;
        // A broken main module is reported, the scene still plays
        (void)m_scripts.Start(start.mainModule);
        LOG_MESSAGE(log::Category::Script, LogSource, "Play: scripts and physics run");
        // The entity scripts start on the next update
        m_scripts.OnPlay();
    }

    void PlayMode::ReplaceScene(SceneAsset asset)
    {
        m_scene.Clear();
        // Models of the scene before stay: the editor puts its nodes back after Stop
        const auto handlers = m_models ? m_models->InsertAll(std::move(asset.models), asset.modelFiles)
                                       : Vector<ModelHandler> { };
        (void)m_scene.Append(std::move(asset), handlers);
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
        LOG_MESSAGE(log::Category::Script, LogSource, "Stop: scripts and physics stand still");
    }

    void PlayMode::RequestPlay(PlayTarget target) noexcept
    {
        m_request = Request::Play;
        m_requestedTarget = target;
    }

    void PlayMode::ApplyRequest()
    {
        const Request request = std::exchange(m_request, Request::None);
        if (request == Request::Stop) {
            Stop();
            return;
        }
        if (request != Request::Play || m_playing)
            return;
        if (!m_startSource) {
            Play({ });
            return;
        }
        if (auto start = m_startSource(m_requestedTarget))
            Play(std::move(*start));
    }

    PlayRequests PlayMode::Requests()
    {
        return { [this] { RequestPlay(PlayTarget::Project); }, [this] { RequestPlay(PlayTarget::Scene); },
            [this] { RequestStop(); }, [this] { return IsPlaying(); } };
    }

} // namespace elm::script
