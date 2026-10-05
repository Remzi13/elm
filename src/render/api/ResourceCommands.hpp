#pragma once

#include "render/api/Handles.hpp"
#include "render/api/ResourceDesc.hpp"
#include "render/CommandList.hpp"

#include <variant>

namespace elm::render {

    /// Resource operations recorded by RenderResources (any thread) and executed by the render backend
    /// before the frame they were submitted with is drawn.
    namespace command::resource {
        struct CreateTexture {
            TextureHandle handle;
            TextureDesc desc;
        };
        struct ResizeTexture {
            TextureHandle handle;
            uint32_t width { 0 };
            uint32_t height { 0 };
        };
        struct UploadTexture {
            TextureHandle handle;
            TextureData data;
        };
        struct DestroyTexture {
            TextureHandle handle;
        };
        struct CreateBuffer {
            BufferHandle handle;
            BufferDesc desc;
            Vector<uint8_t> initialData;
        };
        struct UploadBuffer {
            BufferHandle handle;
            uint64_t offset { 0 };
            Vector<uint8_t> data;
        };
        struct DestroyBuffer {
            BufferHandle handle;
        };
        struct CreateMesh {
            MeshHandle handle;
            MeshDataPtr data;
        };
        struct DestroyMesh {
            MeshHandle handle;
        };
    }

    using ResourceCommand = std::variant<
        command::resource::CreateTexture,
        command::resource::ResizeTexture,
        command::resource::UploadTexture,
        command::resource::DestroyTexture,
        command::resource::CreateBuffer,
        command::resource::UploadBuffer,
        command::resource::DestroyBuffer,
        command::resource::CreateMesh,
        command::resource::DestroyMesh>;

    using ResourceCommandList = BasicCommandList<ResourceCommand>;

} // namespace elm::render
