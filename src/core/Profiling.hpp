#pragma once

#if defined(TRACY_ENABLE)
    #include <tracy/Tracy.hpp>

    #define ELM_PROFILE_FRAME()          FrameMark
    #define ELM_PROFILE_FRAME_N(name)    FrameMarkNamed(name)
    #define ELM_PROFILE_SCOPE()          ZoneScoped
    #define ELM_PROFILE_SCOPE_N(name)    ZoneScopedN(name)
    #define ELM_PROFILE_SCOPE_C(color)   ZoneScopedC(color)
    #define ELM_PROFILE_SCOPE_NC(name, color) ZoneScopedNC(name, color)
    #define ELM_PROFILE_THREAD(name)     tracy::SetThreadName(name)
#else
    #define ELM_PROFILE_FRAME()
    #define ELM_PROFILE_FRAME_N(name)
    #define ELM_PROFILE_SCOPE()
    #define ELM_PROFILE_SCOPE_N(name)
    #define ELM_PROFILE_SCOPE_C(color)
    #define ELM_PROFILE_SCOPE_NC(name, color)
    #define ELM_PROFILE_THREAD(name)
#endif
