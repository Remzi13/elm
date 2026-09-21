#include "graphics/render/MeshManager.h"

#include "graphics/MeshDataStorage.hpp"

namespace elm {
namespace render {

    namespace {
        MeshManager* g_meshManager { nullptr };
    }

    bool MeshManager::Init(CommandList* commandList)
    {        
        m_commandList = commandList;
        g_meshManager = this;
        return m_commandList != nullptr;
    }

    core::Handler MeshManager::CreateMesh(const MeshData& data)
    {
        const auto& meshData = storeMeshData(data);
        for ( const auto& m : m_meshes )
        {
            if ( m.second.first == meshData )
            {
                return m.first;
            }
        }
        auto handler = core::Handler(m_index++, core::Handler::Render);
        m_commandList->Push(command::CreateMesh { handler, meshData });
        return handler;
    }

    void MeshManager::PushMesh(core::Handler handler, const core::Handler& meshData, const Mesh& mesh)
    {
        m_meshes.push_back(std::make_pair(handler, std::make_pair(meshData, mesh)));
    }

    const Mesh& MeshManager::GetMesh(core::Handler handler)
    {
        for ( const auto& m : m_meshes )
        {
            if (m.first == handler)
                return m.second.second;
        }
        return m_emptyMesh;
    }

    core::Handler createMesh( const MeshData& data )
    {
        return g_meshManager->CreateMesh(data);
    }

} // namespace render
}
