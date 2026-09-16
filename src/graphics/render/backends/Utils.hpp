#pragma once 

#include "graphics/render/Render.hpp"

#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"

namespace elm::render {

	Diligent::BIND_FLAGS getBindFlags(BufferType type); 
	Diligent::TEXTURE_FORMAT getTextureFormat(TextureFormat format);
	Diligent::USAGE getUsage(TextureUsage usage);
	Diligent::BIND_FLAGS getTextureBindFlags(uint32_t bindFlags);

}