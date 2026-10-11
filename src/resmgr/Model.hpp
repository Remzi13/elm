#pragma once

#include "core/Handler.hpp"
#include "render/api/RenderResources.hpp"

#include <algorithm>

namespace elm::resmgr {

    struct ModelTag;
    using ModelHandler = core::Handle<ModelTag>;

    class Model {
    public:
        struct Submesh {
            render::MeshDataPtr data;
            render::Mesh mesh;
            Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
            String name;
        };

        explicit Model(String modelName = {})
            : name(std::move(modelName))
        {
        }

        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;
        Model(Model&&) noexcept = default;
        Model& operator=(Model&&) noexcept = default;

        void AddSubmesh(MeshData meshData,
            Vector4 submeshColor = { 1.0f, 1.0f, 1.0f, 1.0f }, String submeshName = {})
        {
            auto data = MakeShared<const MeshData>(std::move(meshData));
            submeshes.push_back({ std::move(data), {}, submeshColor, std::move(submeshName) });
        }

        void Upload(render::RenderResources& resources)
        {
            for (auto& submesh : submeshes) {
                if (submesh.data && !submesh.mesh.IsValid())
                    submesh.mesh = render::Mesh(resources, submesh.data);
            }
        }

        [[nodiscard]] AABB GetLocalBounds() const noexcept
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

        String name;
        Vector<Submesh> submeshes;
    };

} // namespace elm::resmgr
