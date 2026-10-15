#include "EngineApp.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/AssetPaths.hpp"
#include "core/Log.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "script/ScriptSystem.hpp"
#include "script/SelfTest.hpp"

namespace {

    /// Resolves the asset root from the settings file without starting the engine.
    void ApplyFileSystemSettings()
    {
        elm::Settings settings;
        elm::FileSystem::RegisterSettings(settings);
        const elm::Path settingsPath = elm::Settings::DefaultFilePath();
        if (auto loaded = settings.Load(settingsPath); !loaded)
            std::cerr << "[Warning] " << loaded.error().message << std::endl;
        elm::FileSystem::ApplySettings(settings, settingsPath.Parent());
    }

    /// --dump-script-api <file>: writes the LuaLS definitions of the engine's Lua API.
    int DumpScriptApi(const char* fileName)
    {
        using namespace elm;
        const auto meta = script::ScriptSystem::GenerateApiMeta();
        if (!meta) {
            std::cerr << meta.error().message << std::endl;
            return 1;
        }
        const Path path = FileSystem::Resolve(fileName);
        std::error_code error;
        FileSystem::CreateDirectories(path.Parent(), error);
        auto stream = FileSystem::OpenForWriting(path, std::ios::out | std::ios::binary);
        stream.write(meta->data(), static_cast<std::streamsize>(meta->size()));
        if (!stream) {
            std::cerr << "Cannot write " << path.string() << std::endl;
            return 1;
        }
        std::cout << "Script API written to " << path.string() << std::endl;
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    using namespace elm;

    // Tool modes: run without a window and exit
    for (int i = 1; i < argc; ++i) {
        const bool selfTest = std::strcmp(argv[i], "--script-selftest") == 0;
        const bool dumpApi = std::strcmp(argv[i], "--dump-script-api") == 0;
        if (!selfTest && !dumpApi)
            continue;
        ApplyFileSystemSettings();
        core::registerThread(core::ThreadRole::Update);
        if (selfTest)
            return script::RunSelfTests(AssetPaths::Scripts());
        return DumpScriptApi(i + 1 < argc ? argv[i + 1] : "assets/scripts/.meta/elm.lua");
    }

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
