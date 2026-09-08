#include "graphics/render/BufferManager.hpp"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"

namespace elm::render {

	namespace {
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

	BufferManager::BufferManager(){		
	}

	bool BufferManager::Init(Diligent::IRenderDevice* renderDevice)
	{
		m_renderDevice = renderDevice;
		return true;
	}

	BufferHandler BufferManager::createBuffer(const BufferInfo& info) {
				
		Diligent::BufferDesc VBDesc;
		VBDesc.Name = info.name.c_str();
		VBDesc.Usage = Diligent::USAGE_IMMUTABLE;
		VBDesc.BindFlags = getBindFlags(info.type);
		VBDesc.Size = info.size;
		Diligent::BufferData VBData;
		VBData.pData = info.data;
		VBData.DataSize = VBDesc.Size;
		Diligent::IBuffer* buffer{ nullptr };
		m_renderDevice->CreateBuffer(VBDesc, &VBData, &buffer);

		BufferHandler handler;
		handler.index = ++m_currentIndex;
		m_buffers.emplace(handler.index, buffer);
		return handler;
	}

	Diligent::IBuffer* BufferManager::getBufferImpl(const BufferHandler& handler) const
	{
		auto it = m_buffers.find(handler.index);
		if (it != m_buffers.end())
			return it->second;
		return nullptr;
	}

	void BufferManager::destroyBuffer(BufferHandler handler) {
        auto it = m_buffers.find(handler.index);
        if (it != m_buffers.end()) {
            it->second->Release();
            m_buffers.erase(it);
        }   		
	}

	void BufferManager::clear() {
		for (auto& pair : m_buffers) {
			pair.second->Release();
		}
		m_buffers.clear();
	}

}