#if defined(_WIN32) || defined(_WIN64)
#define ELM_PLATFORM_WINDOWS 1
#elif defined(linux) || defined(__linux__) || defined(__linux)
#define ELM_PLATFORM_LINUX 1
#endif
