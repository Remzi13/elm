#pragma once

#include "graphics/culling/MathTypes.hpp"
#include <vector>
#include <cstdint>

namespace elm  {

struct Vertex {
    Vector3 position;
    Vector3 normal;
    float u{0.0f};
    float v{0.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB localBounds;
};

class GeometryPrimitives {
public:
    static MeshData CreateBox(const Vector3& halfExtents) {
        MeshData mesh;
        const float hx = halfExtents.x;
        const float hy = halfExtents.y;
        const float hz = halfExtents.z;

        mesh.localBounds = AABB{Vector3{-hx, -hy, -hz}, Vector3{hx, hy, hz}};

        // 6 faces * 4 vertices = 24 vertices
        mesh.vertices = {
            // Front face (+Z)
            {{-hx, -hy,  hz}, {0.0f, 0.0f, 1.0f}, 0.0f, 1.0f},
            {{ hx, -hy,  hz}, {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f},
            {{ hx,  hy,  hz}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.0f},
            {{-hx,  hy,  hz}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f},

            // Back face (-Z)
            {{ hx, -hy, -hz}, {0.0f, 0.0f, -1.0f}, 0.0f, 1.0f},
            {{-hx, -hy, -hz}, {0.0f, 0.0f, -1.0f}, 1.0f, 1.0f},
            {{-hx,  hy, -hz}, {0.0f, 0.0f, -1.0f}, 1.0f, 0.0f},
            {{ hx,  hy, -hz}, {0.0f, 0.0f, -1.0f}, 0.0f, 0.0f},

            // Top face (+Y)
            {{-hx,  hy,  hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f},
            {{ hx,  hy,  hz}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f},
            {{ hx,  hy, -hz}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f},
            {{-hx,  hy, -hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f},

            // Bottom face (-Y)
            {{-hx, -hy, -hz}, {0.0f, -1.0f, 0.0f}, 0.0f, 1.0f},
            {{ hx, -hy, -hz}, {0.0f, -1.0f, 0.0f}, 1.0f, 1.0f},
            {{ hx, -hy,  hz}, {0.0f, -1.0f, 0.0f}, 1.0f, 0.0f},
            {{-hx, -hy,  hz}, {0.0f, -1.0f, 0.0f}, 0.0f, 0.0f},

            // Right face (+X)
            {{ hx, -hy,  hz}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
            {{ hx, -hy, -hz}, {1.0f, 0.0f, 0.0f}, 1.0f, 1.0f},
            {{ hx,  hy, -hz}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f},
            {{ hx,  hy,  hz}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.0f},

            // Left face (-X)
            {{-hx, -hy, -hz}, {-1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
            {{-hx, -hy,  hz}, {-1.0f, 0.0f, 0.0f}, 1.0f, 1.0f},
            {{-hx,  hy,  hz}, {-1.0f, 0.0f, 0.0f}, 1.0f, 0.0f},
            {{-hx,  hy, -hz}, {-1.0f, 0.0f, 0.0f}, 0.0f, 0.0f},
        };

        mesh.indices.reserve(36);
        for (uint32_t i = 0; i < 6; ++i) {
            const uint32_t base = i * 4;
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 3);
        }

        return mesh;
    }

    static MeshData CreateCube(float sideLength = 1.0f) {
        const float h = sideLength * 0.5f;
        return CreateBox(Vector3{h, h, h});
    }

    static MeshData CreateWall(float width, float height, float thickness) {
        return CreateBox(Vector3{width * 0.5f, height * 0.5f, thickness * 0.5f});
    }

    static MeshData CreateGroundPlane(float sizeX, float sizeZ) {
        MeshData mesh;
        const float hx = sizeX * 0.5f;
        const float hz = sizeZ * 0.5f;
        mesh.localBounds = AABB{Vector3{-hx, -0.05f, -hz}, Vector3{hx, 0.05f, hz}};

        mesh.vertices = {
            {{-hx, 0.0f,  hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 10.0f},
            {{ hx, 0.0f,  hz}, {0.0f, 1.0f, 0.0f}, 10.0f, 10.0f},
            {{ hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, 10.0f, 0.0f},
            {{-hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f},
        };

        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }
};

} // namespace Engine
