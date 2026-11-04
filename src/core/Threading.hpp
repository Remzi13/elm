#pragma once

namespace elm::core {
    /// Engine threads with a fixed responsibility. API that may only be used from one of them
    /// checks it with ELM_ASSERT_THREAD.
    enum class ThreadRole {
        Update,
        Render,
        Count
    };

    /// Binds the calling thread to the role. Rebinding a role moves it to the calling thread.
    void registerThread(ThreadRole role);
    void unregisterThread(ThreadRole role);
    /// True when the calling thread holds the role, or when no thread has registered it yet.
    [[nodiscard]] bool isThread(ThreadRole role);
} // namespace elm::core
