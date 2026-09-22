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
