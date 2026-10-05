#include "EngineApp.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/Log.hpp"

int main(int argc, char** argv) {
    using namespace elm;

    LOG_MESSAGE( log::Category::Core, "Main", "Starting C++23 Cross-Platform 3D Engine Core..." );
    
    elm::EngineApp app;

    // Initialize application using C++23 std::expected error checking
    const auto initResult = app.Init(1280, 720, "C++23 3D Engine - Diligent Engine & Jolt Physics");
    if (!initResult) {
        std::cerr << "[Fatal Error] Failed to initialize engine: " << initResult.error().message << std::endl;
        return static_cast<int>(initResult.error().code);
    }

    // --scene <preset index> [--instances <count>]: build a test scene on startup
    int scenePreset = -1;
    uint32_t instanceCount = 1500;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0)
            scenePreset = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--instances") == 0)
            instanceCount = static_cast<uint32_t>(std::atoi(argv[i + 1]));
    }
    if (scenePreset >= 0)
        app.LoadTestScene(static_cast<ScenePreset>(scenePreset), instanceCount);

    // Run engine loop
    const auto runResult = app.Run();
    if (!runResult) {
        std::cerr << "[Fatal Error] Engine runtime failure: " << runResult.error().message << std::endl;
        return static_cast<int>(runResult.error().code);
    }

    app.Shutdown();
    LOG_MESSAGE( log::Category::Core, "Main", "Application terminated gracefully." );
    return 0;
}
