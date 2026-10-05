#pragma once

#if defined(_MSC_VER)
#define ELM_DEBUG_BREAK() __debugbreak()
#else
#define ELM_DEBUG_BREAK() asm volatile("int $3");
#endif

#define ELM_ASSERT(cond)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            ELM_DEBUG_BREAK();                                   \
        }                                                        \
    } while (0)

// Checks that the calling thread holds the role (elm::core::ThreadRole), debug builds only
#if defined(NDEBUG)
#define ELM_ASSERT_THREAD(role) ((void)0)
#else
#include "core/Threading.hpp"
#define ELM_ASSERT_THREAD(role) ELM_ASSERT(::elm::core::isThread(::elm::core::ThreadRole::role))
#endif
