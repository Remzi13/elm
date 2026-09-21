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

        [[nodiscard]] core::Handler createBuffer(const BufferInfo& info);
        [[nodiscard]] Diligent::IBuffer* getBufferImpl(const core::Handler& handler) const;
        void destroyBuffer(core::Handler buffer);

        void clear();

    private:
        Diligent::IRenderDevice* m_renderDevice;
        Diligent::IDeviceContext* m_deviceContext;
        UnorderedMap<core::Handler, Diligent::IBuffer*> m_buffers;
        core::Handler::ValueType m_currentIndex { 0 };
    };

}
}