#include "graphics/render/BufferManager.hpp"

#include "core/Debug.hpp"

#include "graphics/render/backends/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"

namespace elm {
namespace render {

    BufferManager::BufferManager()
    {
    }

    bool BufferManager::Init(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext)
    {
        m_renderDevice = renderDevice;
        m_deviceContext = deviceContext;
        return true;
    }

    core::Handler BufferManager::CreateBuffer(const BufferInfo& info)
    {
        Diligent::BufferDesc VBDesc;
        VBDesc.Name = info.name.c_str();
        // USAGE_DEFAULT позволяет обновлять содержимое через UpdateBuffer
        VBDesc.Usage = Diligent::USAGE_DEFAULT;
        VBDesc.BindFlags = getBindFlags(info.type);
        VBDesc.Size = info.size;

        Diligent::IBuffer* buffer { nullptr };

        // 1. Создаем пустой буфер без первоначальных данных (pData = nullptr)
        // В этом случае Diligent инициализирует ресурс строго в D3D12_RESOURCE_STATE_COMMON
        m_renderDevice->CreateBuffer(VBDesc, nullptr, &buffer);

        // 2. Если данные переданы — загружаем их через контекст устройства
        if (buffer && info.data && info.size > 0 && m_deviceContext) {
            m_deviceContext->UpdateBuffer(
                buffer,
                0,
                info.size,
                info.data,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        core::Handler handler(++m_currentIndex, core::Handler::Render);
        m_buffers.emplace(handler, buffer);
        return handler;
    }

    Diligent::IBuffer* BufferManager::GetBufferImpl(const core::Handler& handler) const
    {
        ELM_ASSERT(handler.GetType() == core::Handler::Type::Render);

        auto it = m_buffers.find(handler);
        if (it != m_buffers.end())
            return it->second;
        return nullptr;
    }

    void BufferManager::DestroyBuffer(core::Handler handler)
    {
        ELM_ASSERT(handler.GetType() == core::Handler::Type::Render);

        auto it = m_buffers.find(handler);
        if (it != m_buffers.end()) {
            it->second->Release();
            m_buffers.erase(it);
        }
    }

    void BufferManager::Clear()
    {
        for (auto& pair : m_buffers) {
            pair.second->Release();
        }
        m_buffers.clear();
    }

}
}