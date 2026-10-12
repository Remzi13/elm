#include "graphics/Model.hpp"

namespace elm::graphics {

    Model::Model(String modelName)
        : name(std::move(modelName))
    {
    }

    void Model::AddSubmesh(MeshData meshData, Vector4 submeshColor, String submeshName)
    {
        AddSubmesh(MakeShared<const MeshData>(std::move(meshData)), submeshColor, std::move(submeshName));
    }

    void Model::AddSubmesh(MeshDataPtr meshData, Vector4 submeshColor, String submeshName)
    {
        submeshes.push_back({ std::move(meshData), submeshColor, std::move(submeshName) });
    }

    [[nodiscard]] AABB Model::GetLocalBounds() const noexcept
    {
        AABB bounds;
        for (const auto& submesh : submeshes) {
            if (const auto& data = submesh.data) {
                bounds.minBounds.x = std::min(bounds.minBounds.x, data->localBounds.minBounds.x);
                bounds.minBounds.y = std::min(bounds.minBounds.y, data->localBounds.minBounds.y);
                bounds.minBounds.z = std::min(bounds.minBounds.z, data->localBounds.minBounds.z);
                bounds.maxBounds.x = std::max(bounds.maxBounds.x, data->localBounds.maxBounds.x);
                bounds.maxBounds.y = std::max(bounds.maxBounds.y, data->localBounds.maxBounds.y);
                bounds.maxBounds.z = std::max(bounds.maxBounds.z, data->localBounds.maxBounds.z);
            }
        }
        return bounds;
    }

    AABB ComputeBounds(const Vector<Vertex>& vertices) noexcept
    {
        AABB bounds;
        for (const auto& vertex : vertices) {
            bounds.minBounds.x = std::min(bounds.minBounds.x, vertex.position.x);
            bounds.minBounds.y = std::min(bounds.minBounds.y, vertex.position.y);
            bounds.minBounds.z = std::min(bounds.minBounds.z, vertex.position.z);
            bounds.maxBounds.x = std::max(bounds.maxBounds.x, vertex.position.x);
            bounds.maxBounds.y = std::max(bounds.maxBounds.y, vertex.position.y);
            bounds.maxBounds.z = std::max(bounds.maxBounds.z, vertex.position.z);
        }
        return bounds;
    }

    /// Bakes an affine transform into the geometry. Normals use the inverse transpose,
    /// mirroring transforms flip the triangle winding so faces keep pointing outwards.
    MeshData TransformMeshData(const MeshData& source, const Matrix4x4& transform)
    {
        const auto& m = transform;
        // Cofactors of the upper 3x3: proportional to its inverse transpose
        const float c00 = m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1);
        const float c01 = m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2);
        const float c02 = m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0);
        const float c10 = m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2);
        const float c11 = m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0);
        const float c12 = m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1);
        const float c20 = m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1);
        const float c21 = m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2);
        const float c22 = m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
        const float determinant = m(0, 0) * c00 + m(0, 1) * c01 + m(0, 2) * c02;
        const float normalSign = determinant < 0.0f ? -1.0f : 1.0f;

        MeshData result;
        result.vertices.reserve(source.vertices.size());
        for (const auto& vertex : source.vertices) {
            Vertex transformed = vertex;
            const Vector4 position = transform.TransformPoint(vertex.position);
            transformed.position = Vector3 { position.x, position.y, position.z };
            const Vector3& n = vertex.normal;
            const Vector3 normal {
                c00 * n.x + c01 * n.y + c02 * n.z,
                c10 * n.x + c11 * n.y + c12 * n.z,
                c20 * n.x + c21 * n.y + c22 * n.z
            };
            if (normal.Length() > 0.0f)
                transformed.normal = normal.Normalized() * normalSign;
            result.vertices.push_back(transformed);
        }

        result.indices = source.indices;
        if (determinant < 0.0f) {
            for (size_t index = 0; index + 2 < result.indices.size(); index += 3)
                std::swap(result.indices[index + 1], result.indices[index + 2]);
        }
        result.localBounds = ComputeBounds(result.vertices);
        return result;
    }
    }