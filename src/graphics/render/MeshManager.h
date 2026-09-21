#pragma once

#include "core/Std.hpp"

#include "graphics/render/Command.hpp"
#include "graphics/render/Render.hpp"

namespace elm {
namespace render {
    class MeshManager {
    public:
        bool Init(CommandList* commandList);

        [[nodiscard]] core::Handler CreateMesh(const MeshData& data);
        void DestroyMesh(const core::Handler& handler);
        [[nodiscard]] const Mesh& GetMesh(core::Handler handler) const;
        [[nodiscard]] const Mesh& GetMeshByData(core::Handler meshData) const;
        void PushMesh(core::Handler handler, const core::Handler& meshData, const Mesh& mesh);
        void PopMesh(core::Handler handler, Mesh& mesh);

    private:
        Mesh m_emptyMesh;
        CommandList* m_commandList { nullptr };
        core::Handler::ValueType m_index { 0 };
        Vector<std::pair<core::Handler, std::pair<core::Handler, Mesh>>> m_meshes;
    };

    [[nodiscard]] core::Handler createMesh(const MeshData& data);
    void destroyMesh(const core::Handler& handler);
}
}
