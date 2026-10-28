engine_add_library(editor
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
target_link_libraries(editor PUBLIC core graphics project)
