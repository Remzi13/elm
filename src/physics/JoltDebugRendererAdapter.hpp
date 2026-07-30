#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include <vector>
#include <array>

namespace Engine {

struct DebugLine {
    std::array<float, 3> start;
    std::array<float, 3> end;
    uint32_t color;
};

struct DebugTriangle {
    std::array<float, 3> v1;
    std::array<float, 3> v2;
    std::array<float, 3> v3;
    uint32_t color;
};

class JoltDebugRendererAdapter final : public JPH::DebugRenderer {
public:
    JoltDebugRendererAdapter();
    ~JoltDebugRendererAdapter() override;

    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) override;
    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor = JPH::Color::sWhite, float inHeight = 0.5f) override;

    // Batch management
    void ClearBuffers();
    [[nodiscard]] const std::vector<DebugLine>& GetLines() const { return m_lines; }
    [[nodiscard]] const std::vector<DebugTriangle>& GetTriangles() const { return m_triangles; }

protected:
    Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;
    Batch CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const uint32_t* inIndices, int inIndexCount) override;

private:
    std::vector<DebugLine> m_lines;
    std::vector<DebugTriangle> m_triangles;
};

} // namespace Engine
