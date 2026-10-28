engine_add_library(resmgr
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

target_include_directories(resmgr PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(resmgr PUBLIC core Scene PRIVATE project glaze::glaze)
