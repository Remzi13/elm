engine_add_library(script
    ScriptSystem.hpp
    ScriptSystem.cpp
    SelfTest.hpp
    SelfTest.cpp

    bindings/Bindings.hpp
    bindings/Entity.hpp
    bindings/CoreBindings.cpp
    bindings/MathBindings.cpp
    bindings/InputBindings.cpp
    bindings/SceneBindings.cpp

    ui/ScriptWindow.hpp
    ui/ScriptWindow.cpp
    ui/ScriptEditor.hpp
    ui/ScriptEditor.cpp
    ui/LuaHighlighter.hpp
    ui/LuaHighlighter.cpp

    lua/Lua.hpp
    lua/State.hpp
    lua/State.cpp
    lua/Stack.hpp
    lua/Function.hpp
    lua/Ref.hpp
    lua/Module.hpp
    lua/Class.hpp
    lua/Bind.hpp
    lua/ApiRegistry.hpp
    lua/ApiRegistry.cpp
)

target_link_libraries(script PUBLIC core lua)
# The engine systems the bindings expose
target_link_libraries(script PRIVATE math input Scene graphics resmgr)
