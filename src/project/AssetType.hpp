#pragma once

#include "core/FileSystem.hpp"

#include <cstdint>
#include <optional>

namespace elm::project {

    /// What a file of the assets folder is, by its extension (docs/project/Проекты.md).
    enum class AssetType : uint8_t {
        Scene,
        Model,
        Script,
        /// An imported original: .gltf or .glb.
        SourceModel,
        Texture,
        Other
    };

    inline constexpr AssetType AssetTypes[] = { AssetType::Scene, AssetType::Model, AssetType::Script, AssetType::SourceModel,
        AssetType::Texture, AssetType::Other };

    [[nodiscard]] constexpr StringView AssetTypeName(AssetType type)
    {
        switch (type) {
        case AssetType::Scene: return "Scene";
        case AssetType::Model: return "Model";
        case AssetType::Script: return "Script";
        case AssetType::SourceModel: return "Source model";
        case AssetType::Texture: return "Texture";
        case AssetType::Other: return "Other";
        }
        return "Other";
    }

    [[nodiscard]] AssetType AssetTypeOf(const Path& file);

} // namespace elm::project
