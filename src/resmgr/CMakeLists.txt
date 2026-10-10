engine_add_library(resmgr
    JsonValue.hpp
    JsonValue.cpp
    Model.hpp
    SerializationError.hpp
    Serializer.hpp
    Serializer.cpp
)

target_include_directories(resmgr PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(resmgr PUBLIC core Scene PRIVATE glaze::glaze)
