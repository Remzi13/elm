#pragma once 

#include "core/Std.hpp"

#include "graphics/render/Render.hpp"

namespace Diligent {
	struct IRenderDevice;
	struct IBuffer;
}

namespace elm::render {
		
	struct BufferHandler {
		int index { -1 };
	};	

	struct BufferInfo {
		String name;
		BufferType type;
		size_t size;
		void* data;
	};

	class BufferManager {

	public:
		BufferManager();

		bool Init(Diligent::IRenderDevice* renderDevice);

		[[nodiscard]] BufferHandler createBuffer(const BufferInfo& info);
		[[nodiscard]] Diligent::IBuffer* getBufferImpl(const BufferHandler& handler) const;
		void destroyBuffer(BufferHandler buffer);

		void clear();

	private:
		Diligent::IRenderDevice* m_renderDevice;
		UnorderedMap<int, Diligent::IBuffer*> m_buffers;
		int m_currentIndex{ 0 };
	};

}