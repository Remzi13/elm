#include "render/api/FrameWriter.hpp"

namespace elm::render {

void FrameWriter::AddDebugVertices(DebugStyle style, std::span<const DebugVertex> vertices)
{
    if (!m_frame || vertices.empty() || m_frame->sceneViews.empty())
        return;

    DebugGeometry& debug = m_frame->debug;
    SceneView& view = m_frame->sceneViews.back();
    const auto firstVertex = static_cast<uint32_t>(debug.vertices.size());
    const auto vertexCount = static_cast<uint32_t>(vertices.size());
    debug.vertices.insert(debug.vertices.end(), vertices.begin(), vertices.end());

    // The vertices just before these are the end of the last batch, so the same style extends it
    if (view.debugBatchCount > 0 && debug.batches.back().style == style) {
        debug.batches.back().vertexCount += vertexCount;
        return;
    }
    debug.batches.push_back({ style, firstVertex, vertexCount });
    ++view.debugBatchCount;
}

} // namespace elm::render
