engine_add_library(editor
    EditorLayer.hpp
    EditorLayer.cpp
    ProjectWindow.hpp
    ProjectWindow.cpp
    ProjectSettingsPanel.hpp
    ProjectSettingsPanel.cpp
    ProjectHubWindow.hpp
    ProjectHubWindow.cpp
)

target_include_directories(editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Editor windows that know the project: graphics itself does not
target_link_libraries(editor PUBLIC core graphics project engine)
# The editor's own actions: new projects, Build Game, Run Standalone
target_link_libraries(editor PRIVATE platform resmgr script)
