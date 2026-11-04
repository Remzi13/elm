#pragma once

#include "core/Std.hpp"

#include "math/Primitivs.hpp"

#include "model/ModelHandler.hpp"

#include <algorithm>
#include <cmath>

namespace elm {

    /// Geometry is shared between models and the render thread, it must not change once created.
    using MeshDataPtr = SharedPtr<const MeshData>;

    /// CPU-side model description. Its GPU meshes are graphics::ModelMeshes'.
    class Model {
    public:
        struct Submesh {
            MeshDataPtr data;
            Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
            String name;
        };

        explicit Model(String modelName = {});

        void AddSubmesh(MeshData meshData, Vector4 submeshColor = { 1.0f, 1.0f, 1.0f, 1.0f }, String submeshName = {});
        void AddSubmesh(MeshDataPtr meshData, Vector4 submeshColor = { 1.0f, 1.0f, 1.0f, 1.0f }, String submeshName = {});
        [[nodiscard]] AABB GetLocalBounds() const noexcept;

        String name;
        Vector<Submesh> submeshes;
    };

    [[nodiscard]] AABB ComputeBounds(const Vector<Vertex>& vertices) noexcept;
    /// Bakes an affine transform into the geometry. Normals use the inverse transpose,
    /// mirroring transforms flip the triangle winding so faces keep pointing outwards.
    [[nodiscard]] MeshData TransformMeshData(const MeshData& source, const Matrix4x4& transform);

} // namespace elm
