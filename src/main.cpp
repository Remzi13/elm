#include "Engine/Core/EngineApp.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "==================================================" << std::endl;
    std::cout << " Starting C++23 Cross-Platform 3D Engine Core " << std::endl;
    std::cout << " Diligent Engine | Jolt Physics | Dear ImGui " << std::endl;
    std::cout << "==================================================" << std::endl;

    Engine::EngineApp app;

    // Initialize application using C++23 std::expected error checking
    const auto initResult = app.Init(1280, 720, "C++23 3D Engine - Diligent Engine & Jolt Physics");
    if (!initResult) {
        std::cerr << "[Fatal Error] Failed to initialize engine: " << initResult.error().message << std::endl;
        return static_cast<int>(initResult.error().code);
    }

    // Run engine loop
    const auto runResult = app.Run();
    if (!runResult) {
        std::cerr << "[Fatal Error] Engine runtime failure: " << runResult.error().message << std::endl;
        return static_cast<int>(runResult.error().code);
    }

    app.Shutdown();
    std::cout << "[Main] Application terminated gracefully." << std::endl;
    return 0;
}
