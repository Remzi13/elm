#include "graphics/MeshDataStorage.hpp"

namespace elm {

namespace {

    class MeshDataStorage {

    public:
        core::Handler Put(const MeshData& data)
        {
            for (const auto& m : m_data) {
                if (m.second == data)
                    return m.first;
            }
            core::Handler handler(m_index++, core::Handler::Resource);
            m_data.emplace(handler, data);
            return handler;
        }
        const MeshData& Get(const core::Handler& handler) const
        {
            return m_data.at(handler);
        }

    private:
        int m_index { 0 };
        UnorderedMap<core::Handler, MeshData> m_data;
    } g_meshData;
}

core::Handler storeMeshData(const MeshData& data)
{
    return g_meshData.Put(data);
}

const MeshData& getMeshData(const core::Handler& handler)
{
    return g_meshData.Get(handler);
}

}
