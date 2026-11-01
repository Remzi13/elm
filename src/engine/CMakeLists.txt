engine_add_library(engine
    IEngineLayer.hpp
    Engine.hpp
    Engine.cpp
    PlayerLayer.hpp
    PlayerLayer.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# The engine: the systems of a running game, the project and the frame loop
target_link_libraries(engine PUBLIC core math input render platform Scene graphics physics project resmgr script)
