#pragma once

#include <filesystem>

#include "core/Error.hpp"
#include "resmgr/Model.hpp"
#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class Serializer {
    public:
        [[nodiscard]] static auto SaveModel(const Model& model, const std::filesystem::path& path) -> EngineResult<void>;
        [[nodiscard]] static auto SaveScene(const Scene& scene, const std::filesystem::path& path) -> EngineResult<void>;
        [[nodiscard]] static auto LoadScene(Scene& scene, render::RenderResources& resources, const std::filesystem::path& path) -> EngineResult<void>;

    private:
        [[nodiscard]] static auto WriteFile(const std::filesystem::path& path, StringView contents) -> EngineResult<void>;
        [[nodiscard]] static auto ReadFile(const std::filesystem::path& path) -> EngineResult<String>;
    };
}