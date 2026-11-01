engine_add_library(resource
    ResourceSystem.hpp
    ResourceSystem.cpp
    JsonValue.hpp
    JsonValue.cpp
    AssetImport.hpp
    AssetImport.cpp
    ProjectTemplate.hpp
    ProjectTemplate.cpp
    ProjectBuilder.hpp
    ProjectBuilder.cpp
    SerializationError.hpp
    Serializer.hpp
    Serializer.cpp
    GltfExporter.hpp
    GltfExporter.cpp
)

target_include_directories(resource PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ResourceSystem keeps the open project: project is in its interface
target_link_libraries(resource PUBLIC core Scene model project PRIVATE glaze::glaze)
