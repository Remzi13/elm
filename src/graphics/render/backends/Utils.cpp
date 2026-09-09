#include "graphics/render/backends/Utils.hpp"


namespace elm::render {

	Diligent::BIND_FLAGS getBindFlags(BufferType type) {
		switch (type) {
		case BufferType::VertexBuffer:
			return Diligent::BIND_VERTEX_BUFFER;
		case BufferType::IndexBuffer:
			return Diligent::BIND_INDEX_BUFFER;
		case BufferType::UniformBuffer:
			return Diligent::BIND_UNIFORM_BUFFER;
		default:
			return Diligent::BIND_NONE;
		}
	}

}