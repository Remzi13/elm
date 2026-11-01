engine_add_library(model
    ModelHandler.hpp
    Model.hpp
    Model.cpp
    ModelCatalog.hpp
    ModelCatalog.cpp
)

target_include_directories(model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Models on the CPU: below the scene and the resources, without the GPU (graphics::ModelMeshes)
target_link_libraries(model PUBLIC core math)
