engine_add_library(script
    ScriptContext.hpp
    ScriptContext.cpp
    ScriptModuleManager.hpp
    ScriptModuleManager.cpp
    EntityScriptManager.hpp
    EntityScriptManager.cpp
    ScriptConsole.hpp
    ScriptConsole.cpp
    ScriptReflection.hpp
    ScriptReflection.cpp
    PlayModeController.hpp
    PlayModeController.cpp
    ScriptSystem.hpp
    ScriptSystem.cpp
    SelfTest.hpp
    SelfTest.cpp

    bindings/Bindings.hpp
    bindings/Components.hpp
    bindings/Entity.hpp
    bindings/PhysicsBindings.cpp
    bindings/ScriptValues.hpp
    bindings/ScriptValues.cpp
    bindings/CoreBindings.cpp
    bindings/MathBindings.cpp
    bindings/InputBindings.cpp
    bindings/SceneBindings.cpp

    ui/ScriptWindow.hpp
    ui/ScriptWindow.cpp
    ui/InspectorWindow.hpp
    ui/InspectorWindow.cpp
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
target_link_libraries(script PRIVATE math input Scene graphics resmgr physics)
