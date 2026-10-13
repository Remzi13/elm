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

auto AssetPaths::Scripts() -> Path
{
    return Root() / "scripts";
}

} // namespace elm
