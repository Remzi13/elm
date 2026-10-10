#pragma once

#include "math/Primitivs.hpp"

namespace elm::resmgr {
    struct Model {
        MeshData meshData;
        Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
        String name;
    };
}
