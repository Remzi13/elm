#pragma once

#include "core/Error.hpp"
#include "core/Settings.hpp"

#include "render/Render.hpp"
#include "render/api/FrameWriter.hpp"

#include "platform/Window.hpp"

#include "editor/ui/ImGuiRenderer.hpp"
#include "editor/ui/DockSpaceView.hpp"
#include "editor/ui/EditorToolbar.hpp"
#include "editor/ui/IImGuiWindow.hpp"
#include "editor/ui/ProjectMenu.hpp"
#include "editor/ui/SceneMenu.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <utility>

struct ImGuiViewport;
struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

namespace elm {

    // Threading:
    //  - main thread: Init, BuildFrame, Shutdown (render thread must be stopped), all ImGui/GLFW calls
    //  - render thread: Render executes the render graph using captured frame data
    // Platform (GLFW) windows are destroyed on the main thread only after the render thread released their swap chains.
    class ImGuiSystem {
    public:
        ImGuiSystem();
        ~ImGuiSystem();

        ImGuiSystem(const ImGuiSystem&) = delete;
        ImGuiSystem& operator=(const ImGuiSystem&) = delete;

        /// ImGui and its renderer; the windows Editor adds after.
        [[nodiscard]] auto Init(platform::Window& window, render::Render& renderer, StringView title) -> elm::EngineResult<void>;
        // Main thread: runs UI logic and captures viewport events and draw data into frame.
        // Windows may add their own scene views (previews) to the frame.
        void BuildFrame(render::FrameWriter& frame);
        void Shutdown();

        // Scene viewport state of the last built frame: camera input goes to the scene only while it is active
        [[nodiscard]] bool IsSceneViewportHovered() const noexcept;
        [[nodiscard]] bool IsSceneViewportFocused() const noexcept;
        /// A widget of the last built frame is held: a drag, typing, the viewport under the mouse button.
        [[nodiscard]] bool IsWidgetHeld() const;

        // Window management
        void AddWindow(UniquePtr<IImGuiWindow> window);
        /// A window that a menu item opens (Project Hub, Settings): not in the Window menu, its visibility is not
        /// saved.
        void AddDialogWindow(UniquePtr<IImGuiWindow> window);
        /// A window bound to the open project: CloseProjectWindows destroys it.
        void AddProjectWindow(UniquePtr<IImGuiWindow> window);
        /// Destroys the project windows; the windows of the next project open as these were.
        void CloseProjectWindows();
        /// The open project the File and Project menus show and how it asks the engine for another one.
        void SetProjectMenu(ProjectMenuDesc project);
        /// The open scene the File and Edit menus work on; empty without a project.
        void SetSceneMenu(const SceneMenuDeps& deps);
        /// What Play, Stop and the Debug menu of the menu bar work on.
        void SetToolbar(const EditorToolbarDesc& desc);
        /// Replaces the scene with a scene file at the start of the next frame, outside of any window; asks
        /// first when the scene has unsaved changes.
        void OpenSceneFile(const Path& file);
        [[nodiscard]] std::span<const UniquePtr<IImGuiWindow>> GetWindows() const noexcept { return m_windows; }

        template <typename T>
        [[nodiscard]] T* GetWindow() const
        {
            for (const auto& window : m_windows) {
                if (auto* ptr = dynamic_cast<T*>(window.get())) {
                    return ptr;
                }
            }
            return nullptr;
        }

    private:
        // ImGui platform callbacks, called on the main thread
        static void OnRendererCreateWindow(ImGuiViewport* viewport);
        static void OnRendererDestroyWindow(ImGuiViewport* viewport);
        static void OnPlatformDestroyWindow(ImGuiViewport* viewport);
        static float GetViewportDpiScale(ImGuiViewport* viewport);

        // imgui.ini section [Elm][...]: window visibility and the main window placement
        static void* OnSettingsReadOpen(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
        static void OnSettingsReadLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
        static void OnSettingsApplyAll(ImGuiContext* context, ImGuiSettingsHandler* handler);
        static void OnSettingsWriteAll(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

        // Main thread
        void CaptureViewports(render::OverlayFrame& frame);
        void DestroyReleasedPlatformWindows(bool all);
        void ApplySavedVisibility(IImGuiWindow& window) const;
        /// A layout saved before Inspector existed: it opens beside Scene Hierarchy instead of floating over it.
        /// Before the windows of the frame, when Scene Hierarchy is docked already.
        void DockInspectorOnFirstUse();
        void ApplyMainWindowPlacement();
        void TrackMainWindowPlacement();

        struct MainWindowPlacement {
            int x { 0 };
            int y { 0 };
            int width { 0 };
            int height { 0 };
            bool maximized { false };
            bool valid { false };
        };

        // Platform window whose destruction waits until the render thread releases its swap chain
        struct PendingPlatformWindow {
            void* platformUserData { nullptr };
            void* platformHandle { nullptr };
            void* platformHandleRaw { nullptr };
            uint64_t releaseSequence { 0 };
        };

        GLFWwindow* m_window { nullptr };
        UniquePtr<render::ImGuiRenderer> m_renderer;
        bool m_rendererInitialized { false };
        bool m_imguiContextCreated { false };
        bool m_initialized { false };
        bool m_glfwInitialized { false };
        String m_title;
        String m_iniFilePath;

        DockSpaceView m_dockSpace;
        ProjectMenu m_projectMenu;
        SceneMenu m_sceneMenu;
        EditorToolbar m_toolbar;
        bool m_inspectorPlaced { false };
        Vector<UniquePtr<IImGuiWindow>> m_windows;
        // Drawn after m_windows, out of the Window menu
        Vector<UniquePtr<IImGuiWindow>> m_dialogWindows;
        // The windows of m_windows that CloseProjectWindows destroys
        Vector<const IImGuiWindow*> m_projectWindows;
        // Visibility read from imgui.ini, also for windows added after the load
        UnorderedMap<String, bool> m_savedVisibility;
        // Last restored (not maximized, not minimized) placement of the main window
        MainWindowPlacement m_mainWindowPlacement;

        // --- Main thread ---
        Vector<render::OverlaySurfaceEvent> m_pendingViewportEvents;
        Vector<PendingPlatformWindow> m_pendingPlatformWindows;
        uint64_t m_destroyedViewportCount { 0 };
        void (*m_platformDestroyWindow)(ImGuiViewport*) { nullptr };
        bool m_destroyPlatformWindowsImmediately { false };

        /// Counts the Destroy events the render thread applied (GetReleasedSurfaceCount), from this baseline.
        const render::Render* m_render { nullptr };
        uint64_t m_releasedSurfaceBaseline { 0 };
    };

} // namespace elm
