#include "core/AssetPaths.hpp"

namespace elm {

auto AssetPaths::Root() -> Path
{
    return FileSystem::Resolve("assets");
}

auto AssetPaths::Models() -> Path
{
    return Root() / "models";
}

auto AssetPaths::Scenes() -> Path
{
    return Root() / "scenes";
}

} // namespace elm
