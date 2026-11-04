#pragma once

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>

#define ELM_PROFILE_FRAME() FrameMark
#define ELM_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define ELM_PROFILE_THREAD(name) tracy::SetThreadName(name)
#define ELM_PROFILE_TEXT(text, size) ZoneText(text, size)
#else
#define ELM_PROFILE_FRAME()
#define ELM_PROFILE_SCOPE_N(name)
#define ELM_PROFILE_THREAD(name)
#define ELM_PROFILE_TEXT(text, size)
#endif
