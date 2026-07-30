#include "Engine/Physics/JoltDebugRendererAdapter.hpp"

namespace Engine {

class DummyBatch : public JPH::RefTargetVirtual {
public:
    void AddRef() override { ++m_refCount; }
    void Release() override { if (--m_refCount == 0) delete this; }
private:
    std::atomic<uint32_t> m_refCount{0};
};

JoltDebugRendererAdapter::JoltDebugRendererAdapter() {
    Initialize();
}

JoltDebugRendererAdapter::~JoltDebugRendererAdapter() = default;

void JoltDebugRendererAdapter::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
    m_lines.push_back(DebugLine{
        .start = { static_cast<float>(inFrom.GetX()), static_cast<float>(inFrom.GetY()), static_cast<float>(inFrom.GetZ()) },
        .end   = { static_cast<float>(inTo.GetX()), static_cast<float>(inTo.GetY()), static_cast<float>(inTo.GetZ()) },
        .color = inColor.GetUInt32()
    });
}

void JoltDebugRendererAdapter::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, [[maybe_unused]] ECastShadow inCastShadow) {
    m_triangles.push_back(DebugTriangle{
        .v1 = { static_cast<float>(inV1.GetX()), static_cast<float>(inV1.GetY()), static_cast<float>(inV1.GetZ()) },
        .v2 = { static_cast<float>(inV2.GetX()), static_cast<float>(inV2.GetY()), static_cast<float>(inV2.GetZ()) },
        .v3 = { static_cast<float>(inV3.GetX()), static_cast<float>(inV3.GetY()), static_cast<float>(inV3.GetZ()) },
        .color = inColor.GetUInt32()
    });
}

void JoltDebugRendererAdapter::DrawText3D([[maybe_unused]] JPH::RVec3Arg inPosition, [[maybe_unused]] const std::string_view& inString, [[maybe_unused]] JPH::ColorArg inColor, [[maybe_unused]] float inHeight) {
    // 3D text overlay debug hook
}

JPH::DebugRenderer::Batch JoltDebugRendererAdapter::CreateTriangleBatch([[maybe_unused]] const Triangle* inTriangles, [[maybe_unused]] int inTriangleCount) {
    return new DummyBatch();
}

JPH::DebugRenderer::Batch JoltDebugRendererAdapter::CreateTriangleBatch([[maybe_unused]] const Vertex* inVertices, [[maybe_unused]] int inVertexCount, [[maybe_unused]] const uint32_t* inIndices, [[maybe_unused]] int inIndexCount) {
    return new DummyBatch();
}

void JoltDebugRendererAdapter::ClearBuffers() {
    m_lines.clear();
    m_triangles.clear();
}

} // namespace Engine
