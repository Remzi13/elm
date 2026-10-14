#include "script/PlayModeController.hpp"
#include "script/ScriptSystem.hpp"

#include "Scene/SceneSnapshot.hpp"
#include "Scene/TestScenes.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "physics/PhysicsSystem.hpp"

namespace elm {

    namespace {

        constexpr const char* LogSource = "PlayModeController";

    } // namespace

    PlayModeController::~PlayModeController() = default;

    void PlayModeController::Play(Scene* scene, graphics::ModelManager* models, PhysicsSystem* /*physics*/, script::ScriptSystem* scripts)
    {
        if (m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        if (scene)
            m_snapshot = MakeUnique<SceneSnapshot>(SceneSnapshot::Take(*scene, models));
        m_playing = true;
        LOG_MESSAGE(log::Category::Core, LogSource, "Play: entity scripts and physics run, Stop brings the scene back");
        if (scripts)
            scripts->OnPlay();
    }

    void PlayModeController::Stop(Scene* scene, graphics::ModelManager* models, PhysicsSystem* physics, script::ScriptSystem* scripts)
    {
        if (!m_playing)
            return;
        ELM_ASSERT_THREAD(Update);

        if (scripts)
            scripts->OnStop();
        m_playing = false;
        if (physics)
            physics->Clear();
        if (m_snapshot && scene)
            std::move(*m_snapshot).Restore(*scene, models);
        m_snapshot.reset();
        LOG_MESSAGE(log::Category::Core, LogSource, "Stop: the scene is back as Play found it");
    }

    void PlayModeController::ApplyPendingRequests(Scene* scene, graphics::ModelManager* models, PhysicsSystem* physics, script::ScriptSystem* scripts)
    {
        const auto request = std::exchange(m_playRequest, PlayRequest::None);
        if (request == PlayRequest::Play)
            Play(scene, models, physics, scripts);
        else if (request == PlayRequest::Stop)
            Stop(scene, models, physics, scripts);
    }

    void PlayModeController::ResetSnapshot() noexcept
    {
        m_snapshot.reset();
    }

} // namespace elm
