#pragma once 

#include "core/Std.hpp"

#include "graphics/render/Render.hpp"

namespace Diligent {
	struct IRenderDevice;
    struct IDeviceContext;
	struct IBuffer;
}

namespace elm {
namespace render {

    struct BufferInfo {
        String name;
        BufferType type;
        size_t size;
        void* data;
    };

    class BufferManager {

    public:
        BufferManager();

        bool Init(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext);

        [[nodiscard]] core::Handler CreateBuffer(const BufferInfo& info);
        [[nodiscard]] Diligent::IBuffer* GetBufferImpl(const core::Handler& handler) const;
        void DestroyBuffer(core::Handler buffer);

        void Clear();

    private:
        Diligent::IRenderDevice* m_renderDevice;
        Diligent::IDeviceContext* m_deviceContext;
        UnorderedMap<core::Handler, Diligent::IBuffer*> m_buffers;
        core::Handler::ValueType m_currentIndex { 0 };
    };

}
}