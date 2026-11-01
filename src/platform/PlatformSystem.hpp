#pragma once

#include "core/Error.hpp"

#include "input/Input.hpp"

#include "platform/Window.hpp"

namespace elm::platform {

    /// Contact with the OS (docs/core/Системы.md): the main window and its input, with the default axes the
    /// scripts read (elm.input.axis). Owner: the program (Engine for now). Thread: update, which is the main
    /// thread: GLFW events belong to it.
    class PlatformSystem {
    public:
        PlatformSystem() = default;

        PlatformSystem(const PlatformSystem&) = delete;
        PlatformSystem& operator=(const PlatformSystem&) = delete;
        PlatformSystem(PlatformSystem&&) = delete;
        PlatformSystem& operator=(PlatformSystem&&) = delete;

        /// Opens the window and attaches the input to it.
        [[nodiscard]] auto Init(Size size, StringView title) -> EngineResult<void>;
        /// Closes the window; after the systems that render into it shut down.
        void Shutdown();

        /// The window's events of the frame: first in the frame.
        void PollEvents();
        /// The frame's input to the subscribers, by priority.
        void DispatchInput();
        /// The window was asked to close.
        [[nodiscard]] bool ShouldClose() const;

        [[nodiscard]] Window& GetWindow() noexcept { return m_window; }
        [[nodiscard]] Input& GetInput() noexcept { return m_input; }

    private:
        Window m_window;
        Input m_input;
    };

} // namespace elm::platform
