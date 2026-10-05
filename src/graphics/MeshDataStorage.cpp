#include "graphics/MeshDataStorage.hpp"

namespace elm {
    
namespace {

    class MeshDataStorage {

    public:
        MeshDataHandle Put(const MeshData& data)
        {
            for ([[maybe_unused]] const auto& [handle, existingData] : m_data) {
                if (existingData == data) {
                    return handle;
                }
            }

            auto handle = MeshDataHandle::FromRaw(m_nextHandleIndex++);            
            m_data.emplace(handle, data);
            return handle;
        }
        const MeshData& Get(const MeshDataHandle& handle) const
        {
            if (auto it = m_data.find(handle); it != m_data.end()) {
                return it->second;
            }
            return m_void;
        }

    private:        
        MeshData m_void;
        uint32_t m_nextHandleIndex { 1 };
        UnorderedMap<MeshDataHandle, MeshData> m_data;
    } g_meshData;
}

MeshDataHandle storeMeshData(const MeshData& data)
{
    return g_meshData.Put(data);
}

const MeshData& getMeshData(const MeshDataHandle& handler)
{
    return g_meshData.Get(handler);
}

}
