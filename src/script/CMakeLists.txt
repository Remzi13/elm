set(ENTITY_SRC
    entity/ScriptClass.hpp
    entity/ScriptClass.cpp
    entity/ScriptDefaults.hpp
    entity/ScriptDefaults.cpp
    entity/EntityScriptList.hpp
    entity/EntityScriptList.cpp
    entity/EntityScripts.hpp
    entity/EntityScripts.cpp
)

set(BINDINGS_SRC
    bindings/Bindings.hpp
    bindings/Components.hpp
    bindings/LogBindings.cpp
    bindings/TimeBindings.cpp
    bindings/SettingsBindings.cpp
    bindings/ScriptBindings.cpp
    bindings/MathBindings.cpp
    bindings/InputBindings.cpp
    bindings/SceneBindings.cpp
    bindings/ModelBindings.cpp
    bindings/CameraBindings.cpp
    bindings/EngineBindings.cpp
    bindings/PhysicsBindings.cpp
    bindings/DebugBindings.cpp
)

set(LUA_SRC
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

set(MODULE_SRC 
    module/ModulePaths.hpp
    module/ModulePaths.cpp
    module/ModuleLoader.hpp
    module/ModuleLoader.cpp
    module/MainModule.hpp
    module/MainModule.cpp
)

set(VALUES_SRC
    values/Entity.hpp
    values/ScriptValues.hpp
    values/ScriptValues.cpp
)

set(ALL_SRC
    EngineContext.hpp
    ScriptSystem.hpp
    ScriptSystem.cpp
    PlayMode.hpp
    PlayMode.cpp
    ProjectPlayStart.hpp
    ProjectPlayStart.cpp
    SelfTest.hpp
    SelfTest.cpp

    reload/HotReload.hpp
    reload/HotReload.cpp

    console/ScriptConsole.hpp
    console/ScriptConsole.cpp
)

set(ALL_SRC ${ALL_SRC} ${ENTITY_SRC} ${BINDINGS_SRC} ${LUA_SRC} ${MODULE_SRC} ${VALUES_SRC})

engine_add_library(script
    ${ALL_SRC}
)

source_group("Bindings" FILES
    ${BINDINGS_SRC}
)

source_group("Entity" FILES
    ${ENTITY_SRC}
)

source_group("Lua" FILES
    ${LUA_SRC}
)

source_group("Module" FILES
    ${MODULE_SRC}
)

source_group("Values" FILES
    ${VALUES_SRC}
)

target_link_libraries(script PUBLIC core lua)
# The engine systems the bindings expose
target_link_libraries(script PRIVATE math input Scene graphics resource physics project)
