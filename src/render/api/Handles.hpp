#pragma once

#include "core/Handler.hpp"

namespace elm::render {
     
    struct TextureTag;
    struct BufferTag;
    struct MeshTag;
    struct ShaderTag;
    struct PipelineTag;

    using TextureHandle = core::Handle<TextureTag>;
    using BufferHandle = core::Handle<BufferTag>;
    using MeshHandle = core::Handle<MeshTag>;
    using ShaderHandle = core::Handle<ShaderTag>;
    using PipelineHandle = core::Handle<PipelineTag>;

    /// Presentation surface: 0 is the main window, other ids belong to secondary (UI) windows.
    using SurfaceId = uint32_t;
    inline constexpr SurfaceId MainSurface = 0;

} // namespace elm::render

