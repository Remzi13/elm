#pragma once

#include "core/Handler.hpp"
#include "core/Std.hpp"

#include "math/Primitivs.hpp"

namespace elm {

core::Handler storeMeshData(const MeshData& data);
const MeshData& getMeshData(const core::Handler& handler);

}