#pragma once

#include "render/api/ResourceDesc.hpp"

#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"

namespace elm::render {

	[[nodiscard]] Diligent::BIND_FLAGS getBindFlags(BufferType type);
	[[nodiscard]] Diligent::TEXTURE_FORMAT getTextureFormat(TextureFormat format);
	[[nodiscard]] TextureFormat getEngineTextureFormat(Diligent::TEXTURE_FORMAT format);
	[[nodiscard]] Diligent::USAGE getUsage(ResourceUsage usage);
	[[nodiscard]] Diligent::BIND_FLAGS getTextureBindFlags(uint32_t bindFlags);

}
