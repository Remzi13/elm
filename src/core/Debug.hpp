#pragma once

#define ELM_DEBUG_BREAK() __debugbreak()

#define ELM_ASSERT(cond)                                         \
    do {                                                         \
        if (!(cond)) {                                           \
            ELM_DEBUG_BREAK();                                   \
        }                                                        \
    } while (0)
