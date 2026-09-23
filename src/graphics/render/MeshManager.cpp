#include "graphics/render/MeshManager.h"

#include "core/Debug.hpp"
#include "core/Handler.hpp"

#include "graphics/MeshDataStorage.hpp"

namespace elm {
namespace render {

    namespace {
        MeshManager* g_meshManager { nullptr };
    }

    bool MeshManager::Init(CommandQueue* commandQueue)
    {
        m_commandQueue = commandQueue;
        g_meshManager = this;
        return m_commandQueue != nullptr;
    }

    core::Handler MeshManager::CreateMesh(const MeshData& data)
    {
        const auto& meshData = storeMeshData(data);
        auto handler = core::Handler(m_index++, core::Handler::Type::Render);
        m_commandQueue->Push(command::CreateMesh { handler, meshData });
        return handler;
    }

    void MeshManager::DestroyMesh(const core::Handler& handler)
    {
        ELM_ASSERT(handler.GetType() == core::Handler::Type::Render);

        m_commandQueue->Push(command::DestroyMesh { handler });
    }

    void MeshManager::PushMesh(core::Handler handler, const core::Handler& meshData, const Mesh& mesh)
    {
        ELM_ASSERT(handler.GetType() == core::Handler::Type::Render);
        ELM_ASSERT(meshData.GetType() == core::Handler::Type::Resource);

        m_meshes.push_back(std::make_pair(handler, std::make_pair(meshData, mesh)));
    }

    void MeshManager::PopMesh(core::Handler handler, Mesh& mesh)
    {
        std::erase_if(m_meshes, [handler, &mesh](const auto& p) {
            if (p.first == handler) {
                mesh = p.second.second;
                return true;
            }
            return false;
        });

        for (const auto& m : m_meshes) {
            if (m.second.second == mesh) {
                mesh = m_emptyMesh;
                return;
            }
        }
    }

    const Mesh& MeshManager::GetMesh(core::Handler handler) const
    {
        ELM_ASSERT(handler.GetType() == core::Handler::Type::Render);

        for (const auto& m : m_meshes) {
            if (m.first == handler)
                return m.second.second;
        }
        return m_emptyMesh;
    }

    const Mesh& MeshManager::GetMeshByData(core::Handler meshData) const
    {
        ELM_ASSERT(meshData.GetType() == core::Handler::Type::Resource);

        for (const auto& m : m_meshes) {
            if (m.second.first == meshData)
                return m.second.second;
        }
        return m_emptyMesh;
    }

    core::Handler createMesh(const MeshData& data)
    {
        return g_meshManager->CreateMesh(data);
    }

    void destroyMesh(const core::Handler& mesh)
    {
        g_meshManager->DestroyMesh(mesh);
    }

} // namespace render
}
