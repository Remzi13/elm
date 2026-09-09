#include "graphics/render/DynamicLinearAllocator.hpp"

#include "graphics/render/backends/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"

namespace elm::render {

	void DynamicLinearAllocator::Init( Diligent::IRenderDevice* device, const char* name, BufferType type, int capacity )
	{
		m_renderDevice = device;
		m_capacity = capacity;
				
		Diligent::BufferDesc desc;
		desc.Name = name;
		desc.Size = capacity;
        desc.BindFlags = getBindFlags(type);
		desc.Usage = Diligent::USAGE_DYNAMIC;
		desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;

		m_renderDevice->CreateBuffer( desc, nullptr, &m_buffer );
	}

    void DynamicLinearAllocator::Release() {
        m_buffer->Release();
        m_buffer = nullptr;
    }

    void DynamicLinearAllocator::Flush( Diligent::IDeviceContext* pContext ) {
        // Если буфер был замапплен в текущем кадре, обязательно отмапливаем его
        if ( m_data && pContext && m_buffer ) {
            pContext->UnmapBuffer( m_buffer, Diligent::MAP_WRITE );
            m_data = nullptr;
            m_offset = 0;
        }        
    }

    Allocation DynamicLinearAllocator::Allocate( Diligent::IDeviceContext* context, uint64_t size, uint64_t alignment ) {
        // Выравнивание смещения (alignment requirement)
        uint64_t alignedOffset = ( m_offset + ( alignment - 1 ) ) & ~( alignment - 1 );

        if ( alignedOffset + size > m_capacity ) {
            // Если буфер переполнен в кадре — логируем ошибку или обрабатываем расширение
            return Allocation{};
        }

        // Замаппить буфер единовременно при первом allocate за кадр с флагом DISCARD
        if ( m_offset == 0 || !m_data ) {
            context->MapBuffer( m_buffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, m_data );
        }

        Allocation alloc;
        alloc.buffer = m_buffer;
        alloc.offset = alignedOffset;
        alloc.pCPUAddress = static_cast<uint8_t*>( m_data ) + alignedOffset;

        m_offset = alignedOffset + size;

        return alloc;
    }

}