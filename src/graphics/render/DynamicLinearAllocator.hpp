#pragma once

#include "core/Std.hpp"

namespace Diligent {
	struct IRenderDevice;
	struct IBuffer;
	struct IDeviceContext;
}

namespace elm::render {

	struct Allocation {
		Diligent::IBuffer* buffer{ nullptr };
		int offset{ 0 };
		void* pCPUAddress{ nullptr };
	};

	class DynamicLinearAllocator {

	public:
		DynamicLinearAllocator() = default;
		~DynamicLinearAllocator() = default;

		void Init(Diligent::IRenderDevice* device, const char* name, int capacity);
		void Release();
		
		void Flush( Diligent::IDeviceContext* pContext );

		Allocation Allocate( Diligent::IDeviceContext* pContext, uint64_t size, uint64_t alignment );

	private:
		Diligent::IRenderDevice* m_renderDevice{ nullptr };
		Diligent::IBuffer* m_buffer{ nullptr };
		int m_capacity{ 0 };
		int m_offset{ 0 };
		void* m_data{ nullptr };
	};

}