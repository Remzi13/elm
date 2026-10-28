engine_add_library(project
    AssetPath.hpp
    AssetPath.cpp
    AssetType.hpp
    AssetType.cpp
    ProjectAssets.hpp
    ProjectAssets.cpp
    ProjectDesc.hpp
    Project.hpp
    Project.cpp
)

target_include_directories(project PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(project PUBLIC core)
