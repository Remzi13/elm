#pragma once

#include "core/Std.hpp"

#include "graphics/render/Command.hpp"
#include "graphics/render/Render.hpp"

namespace elm {
namespace render {
    class MeshManager {
    public:
        bool Init(CommandList* commandList);

        [[nodiscard]] Handler CreateMesh(const MeshData& data);
        [[nodiscard]] const Mesh& GetMesh(Handler handler);
        void PushMesh(Handler handler, const Mesh& mesh);

    private:
        CommandList* m_commandList { nullptr };
        int m_index { 0 };
        UnorderedMap<int, Mesh> m_meshes;
    };
}
}
