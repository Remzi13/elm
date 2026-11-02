engine_add_executable(editor
    Editor.hpp
    Editor.cpp
    EditMode.hpp
    EditMode.cpp
    SceneEdits.hpp
    SceneEdits.cpp
    EditorSelfTest.hpp
    EditorSelfTest.cpp
    ProjectWindow.hpp
    ProjectWindow.cpp
    ProjectSettingsPanel.hpp
    ProjectSettingsPanel.cpp
    ProjectHubWindow.hpp
    ProjectHubWindow.cpp

    # ImGui, the editor windows that do not know the project
    ui/DockSpaceView.cpp
    ui/DockSpaceView.hpp
    ui/EditorStyle.cpp
    ui/EditorStyle.hpp
    ui/EngineViewportWindow.cpp
    ui/EngineViewportWindow.hpp
    ui/FileDialog.cpp
    ui/FileDialog.hpp
    ui/IImGuiWindow.hpp
    ui/ImGuiRenderer.cpp
    ui/ImGuiRenderer.hpp
    ui/ImGuiSystem.cpp
    ui/ImGuiSystem.hpp
    ui/LogWindow.cpp
    ui/LogWindow.hpp
    ui/ModelEditorWindow.cpp
    ui/ModelEditorWindow.hpp
    ui/ProfilerWindow.cpp
    ui/ProfilerWindow.hpp
    ui/ProjectMenu.cpp
    ui/ProjectMenu.hpp
    ui/SceneEditShortcuts.cpp
    ui/SceneEditShortcuts.hpp
    ui/SceneHierarchyWindow.cpp
    ui/SceneHierarchyWindow.hpp
    ui/SettingsWindow.cpp
    ui/SettingsWindow.hpp
    ui/SocLabWindow.cpp
    ui/SocLabWindow.hpp
    ui/EditorServices.hpp

    # The Scripts window, it shows the parts of ScriptSystem through EditorWindowsAccess;
    # its text logic without ImGui (CodeIndent, LuaHighlighter, ErrorLocation) is checked by --editor-selftest
    script/ApiPanel.cpp
    script/ApiPanel.hpp
    script/CodeIndent.cpp
    script/CodeIndent.hpp
    script/ConsolePanel.cpp
    script/ConsolePanel.hpp
    script/ErrorLocation.cpp
    script/ErrorLocation.hpp
    script/LuaHighlighter.cpp
    script/LuaHighlighter.hpp
    script/ModulesPanel.cpp
    script/ModulesPanel.hpp
    script/ScriptEditor.cpp
    script/ScriptEditor.hpp
    script/ScriptEditorWindows.cpp
    script/ScriptEditorWindows.hpp
    script/ScriptStatusBar.cpp
    script/ScriptStatusBar.hpp
    script/ScriptWindow.cpp
    script/ScriptWindow.hpp
    script/ScriptWindowActions.hpp
    script/ScriptWindowStyle.hpp
    main.cpp
)

target_include_directories(editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# The editor and all of its UI: the engine without it has no ImGui
target_link_libraries(editor PUBLIC imgui_glfw imgui_core core graphics project engine)
# GLFW for the ImGui backend; new projects, Build Game, Run Standalone; the Scripts window
target_link_libraries(editor PRIVATE glfw platform render Scene resource script)

