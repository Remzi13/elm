#pragma once

#include "core/Handler.hpp"
#include "core/Std.hpp"

#include "math/Primitivs.hpp"

namespace elm {
	struct MeshDataTag;
    using MeshDataHandle = core::Handle<MeshDataTag>;

	MeshDataHandle storeMeshData(const MeshData& data);
    const MeshData& getMeshData(const MeshDataHandle& handler);
}