#include "script/ScriptSystem.hpp"

#include "Scene/SceneSnapshot.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "physics/PhysicsSystem.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptSystem";

    } // namespace

    void ScriptSystem::Play()
    {
        if (m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        if (m_engine.scene)
            m_snapshot = MakeUnique<SceneSnapshot>(SceneSnapshot::Take(*m_engine.scene, m_engine.models));
        m_playing = true;
        LOG_MESSAGE(log::Category::Script, LogSource, "Play: entity scripts and physics run, Stop brings the scene back");
        // The entity scripts start on the next Update
        CallHook("on_play", 0.0f, false);
    }

    void ScriptSystem::Stop()
    {
        if (!m_playing)
            return;
        ELM_ASSERT_THREAD(Update);
        ELM_ASSERT(m_entityPassDepth == 0 && "Stop from inside a Lua call: use RequestStop");

        // The scripts end while the scene they played in is still there
        DestroyAllEntityScripts();
        CallHook("on_stop", 0.0f, false);
        m_playing = false;
        // Instances made by those calls go too: none runs in Edit mode
        ReleaseEntityScripts();
        if (m_engine.physics)
            m_engine.physics->Clear();
        if (m_snapshot && m_engine.scene)
            std::move(*m_snapshot).Restore(*m_engine.scene, m_engine.models);
        m_snapshot.reset();
        LOG_MESSAGE(log::Category::Script, LogSource, "Stop: the scene is back as Play found it");
    }

} // namespace elm::script
