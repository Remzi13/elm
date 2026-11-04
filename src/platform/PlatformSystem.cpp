#include "platform/PlatformSystem.hpp"

#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

namespace elm::platform {

    auto PlatformSystem::Init(Size size, StringView title) -> EngineResult<void>
    {
        if (auto windowInit = m_window.Create(size, title); !windowInit)
            return MakeUnexpected(windowInit.error());
        m_input.AttachWindow(m_window.GetHandle());

        // Default axes for the scripts (elm.input.axis): WASD moves, E and Q go up and down
        m_input.AddAxisMapping("MoveForward", Key::W, 1.0f);
        m_input.AddAxisMapping("MoveForward", Key::S, -1.0f);
        m_input.AddAxisMapping("MoveRight", Key::D, 1.0f);
        m_input.AddAxisMapping("MoveRight", Key::A, -1.0f);
        m_input.AddAxisMapping("MoveUp", Key::E, 1.0f);
        m_input.AddAxisMapping("MoveUp", Key::Q, -1.0f);
        return {};
    }

    void PlatformSystem::Shutdown()
    {
        m_window.Destroy();
    }

    void PlatformSystem::PollEvents()
    {
        ELM_PROFILE_SCOPE_N("Process Input");
        m_input.BeginFrame();
    }

    void PlatformSystem::DispatchInput()
    {
        m_input.Update();
    }

    bool PlatformSystem::ShouldClose() const
    {
        return m_window.ShouldClose();
    }

} // namespace elm::platform
