#include "graphics/render/MeshManager.h"

namespace elm {
namespace render {

    bool MeshManager::Init(CommandList* commandList)
    {
        m_commandList = commandList;
        return m_commandList != nullptr;
    }

    Handler MeshManager::CreateMesh(const MeshData& data)
    {
        auto handler = Handler { m_index++ };
        m_commandList->Push(command::CreateMesh { handler, data });
        return handler;
    }

    void MeshManager::PushMesh(Handler handler, const Mesh& mesh)
    {
        m_meshes[handler.index] = mesh;
    }

    const Mesh& MeshManager::GetMesh(Handler handler)
    {
        return m_meshes[handler.index];
    }

} // namespace render
}
