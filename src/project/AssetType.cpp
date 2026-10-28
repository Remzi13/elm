#include "project/AssetType.hpp"

#include <cctype>

namespace elm::project {

    namespace {

        String LowerExtension(const Path& file)
        {
            String extension(file.Extension().string().c_str());
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return extension;
        }

    } // namespace

    AssetType AssetTypeOf(const Path& file)
    {
        const String extension = LowerExtension(file);
        if (extension == ".scene")
            return AssetType::Scene;
        if (extension == ".model")
            return AssetType::Model;
        if (extension == ".lua")
            return AssetType::Script;
        if (extension == ".gltf" || extension == ".glb")
            return AssetType::SourceModel;
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".dds")
            return AssetType::Texture;
        return AssetType::Other;
    }

} // namespace elm::project
