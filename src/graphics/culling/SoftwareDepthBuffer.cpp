#pragma once

#include "graphics/culling/SoftwareDepthBuffer.hpp"
#include <cmath>
#include <algorithm>

namespace Engine {

SoftwareDepthBuffer::SoftwareDepthBuffer(uint32_t width, uint32_t height)
    : m_width(width), m_height(height), m_depthBuffer(static_cast<size_t>(width) * static_cast<size_t>(height), 1.0f) {}

void SoftwareDepthBuffer::Resize(uint32_t width, uint32_t height) {
    if (m_width == width && m_height == height) {
        return;
    }
    m_width = width;
    m_height = height;
    m_depthBuffer.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 1.0f);
}

void SoftwareDepthBuffer::Clear(float clearDepth) {
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), clearDepth);
}

void SoftwareDepthBuffer::RasterizeTriangleScreen(const Vector3& s0, const Vector3& s1, const Vector3& s2,
                                                  float invW0, float invW1, float invW2) {
    // 2D orient / area
    const float area = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
    if (std::abs(area) < 1e-5f) {
        return; // Degenerate triangle
    }

    const float invArea = 1.0f / area;

    // Bounding box
    const float minXF = std::min({s0.x, s1.x, s2.x});
    const float maxXF = std::max({s0.x, s1.x, s2.x});
    const float minYF = std::min({s0.y, s1.y, s2.y});
    const float maxYF = std::max({s0.y, s1.y, s2.y});

    const int minX = std::max(0, static_cast<int>(std::floor(minXF)));
    const int maxX = std::min(static_cast<int>(m_width) - 1, static_cast<int>(std::ceil(maxXF)));
    const int minY = std::max(0, static_cast<int>(std::floor(minYF)));
    const int maxY = std::min(static_cast<int>(m_height) - 1, static_cast<int>(std::ceil(maxYF)));

    if (minX > maxX || minY > maxY) {
        return;
    }

    // Perspective-correct depth interpolation: interpolate (z * invW) and invW
    const float zInvW0 = s0.z * invW0;
    const float zInvW1 = s1.z * invW1;
    const float zInvW2 = s2.z * invW2;

    for (int y = minY; y <= maxY; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);

        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;

            // Barycentric coordinates
            const float w0 = ((s1.x - px) * (s2.y - py) - (s1.y - py) * (s2.x - px)) * invArea;
            const float w1 = ((s2.x - px) * (s0.y - py) - (s2.y - py) * (s0.x - px)) * invArea;
            const float w2 = 1.0f - w0 - w1;

            if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                const float interpolatedInvW = w0 * invW0 + w1 * invW1 + w2 * invW2;
                if (interpolatedInvW > 1e-7f) {
                    const float interpolatedZ = (w0 * zInvW0 + w1 * zInvW1 + w2 * zInvW2) / interpolatedInvW;
                    const size_t pixelIdx = rowOffset + static_cast<size_t>(x);

                    if (interpolatedZ >= 0.0f && interpolatedZ < m_depthBuffer[pixelIdx]) {
                        m_depthBuffer[pixelIdx] = interpolatedZ;
                    }
                }
            }
        }
    }
}

void SoftwareDepthBuffer::RasterizeTriangleClipSpace(const Vector4& p0, const Vector4& p1, const Vector4& p2) {
    constexpr float nearW = 0.01f;

    // Count vertices in front of near plane
    const bool in0 = p0.w >= nearW;
    const bool in1 = p1.w >= nearW;
    const bool in2 = p2.w >= nearW;

    const int inCount = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);
    if (inCount == 0) {
        return; // All behind near plane
    }

    auto clipEdge = [](const Vector4& a, const Vector4& b) -> Vector4 {
        const float t = (nearW - a.w) / (b.w - a.w);
        return {
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            nearW
        };
    };

    auto projectToScreen = [this](const Vector4& cp) -> Vector3 {
        const float invW = 1.0f / cp.w;
        const float ndcX = cp.x * invW;
        const float ndcY = cp.y * invW;
        const float ndcZ = cp.z * invW;

        return {
            (ndcX + 1.0f) * 0.5f * static_cast<float>(m_width),
            (1.0f - ndcY) * 0.5f * static_cast<float>(m_height),
            ndcZ
        };
    };

    if (inCount == 3) {
        // Entire triangle is in front of near plane
        const Vector3 s0 = projectToScreen(p0);
        const Vector3 s1 = projectToScreen(p1);
        const Vector3 s2 = projectToScreen(p2);
        RasterizeTriangleScreen(s0, s1, s2, 1.0f / p0.w, 1.0f / p1.w, 1.0f / p2.w);
    } else if (inCount == 1) {
        // 1 vertex in front, 2 behind: produces 1 clipped triangle
        Vector4 v0 = p0, v1 = p1, v2 = p2;
        if (in1) { std::swap(v0, v1); std::swap(v1, v2); }
        else if (in2) { std::swap(v0, v2); std::swap(v1, v2); }

        const Vector4 c01 = clipEdge(v0, v1);
        const Vector4 c02 = clipEdge(v0, v2);

        const Vector3 s0 = projectToScreen(v0);
        const Vector3 s1 = projectToScreen(c01);
        const Vector3 s2 = projectToScreen(c02);
        RasterizeTriangleScreen(s0, s1, s2, 1.0f / v0.w, 1.0f / c01.w, 1.0f / c02.w);
    } else if (inCount == 2) {
        // 2 vertices in front, 1 behind: produces 2 clipped triangles (quad)
        Vector4 v0 = p0, v1 = p1, v2 = p2;
        if (!in0) { std::swap(v0, v2); std::swap(v0, v1); }
        else if (!in1) { std::swap(v1, v2); std::swap(v0, v1); }

        const Vector4 c02 = clipEdge(v0, v2);
        const Vector4 c12 = clipEdge(v1, v2);

        const Vector3 s0 = projectToScreen(v0);
        const Vector3 s1 = projectToScreen(v1);
        const Vector3 sc02 = projectToScreen(c02);
        const Vector3 sc12 = projectToScreen(c12);

        RasterizeTriangleScreen(s0, s1, sc02, 1.0f / v0.w, 1.0f / v1.w, 1.0f / c02.w);
        RasterizeTriangleScreen(s1, sc12, sc02, 1.0f / v1.w, 1.0f / c12.w, 1.0f / c02.w);
    }
}

void SoftwareDepthBuffer::RasterizeMesh(const std::vector<Vector3>& positions,
                                        const std::vector<uint32_t>& indices,
                                        const Matrix4x4& worldViewProj) {
    if (indices.size() < 3) return;

    // Transform positions to clip space
    std::vector<Vector4> clipPoints(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        clipPoints[i] = worldViewProj.TransformPoint(positions[i]);
    }

    // Rasterize each triangle
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        if (i0 < clipPoints.size() && i1 < clipPoints.size() && i2 < clipPoints.size()) {
            RasterizeTriangleClipSpace(clipPoints[i0], clipPoints[i1], clipPoints[i2]);
        }
    }
}

bool SoftwareDepthBuffer::TestAABB(const AABB& worldAABB, const Matrix4x4& viewProj, float depthBias) const {
    const auto corners = worldAABB.GetCorners();

    float minXF = 1e30f;
    float maxXF = -1e30f;
    float minYF = 1e30f;
    float maxYF = -1e30f;
    float minZ = 1e30f;

    bool allOutsideLeft = true;
    bool allOutsideRight = true;
    bool allOutsideBottom = true;
    bool allOutsideTop = true;
    bool allOutsideNear = true;
    bool allOutsideFar = true;

    for (const auto& c : corners) {
        const Vector4 cp = viewProj.TransformPoint(c);

        if (cp.w <= 0.01f) {
            // Object intersects or is behind the camera near plane -> conservatively visible!
            return false;
        }

        if (cp.x >= -cp.w) allOutsideLeft = false;
        if (cp.x <= cp.w) allOutsideRight = false;
        if (cp.y >= -cp.w) allOutsideBottom = false;
        if (cp.y <= cp.w) allOutsideTop = false;
        if (cp.z >= 0.0f) allOutsideNear = false;
        if (cp.z <= cp.w) allOutsideFar = false;

        const float invW = 1.0f / cp.w;
        const float ndcX = cp.x * invW;
        const float ndcY = cp.y * invW;
        const float ndcZ = cp.z * invW;

        const float sx = (ndcX + 1.0f) * 0.5f * static_cast<float>(m_width);
        const float sy = (1.0f - ndcY) * 0.5f * static_cast<float>(m_height);

        minXF = std::min(minXF, sx);
        maxXF = std::max(maxXF, sx);
        minYF = std::min(minYF, sy);
        maxYF = std::max(maxYF, sy);
        minZ = std::min(minZ, ndcZ);
    }

    // Frustum check: if all corners outside any frustum plane, not occluded by geometry, but culled by frustum
    if (allOutsideLeft || allOutsideRight || allOutsideBottom || allOutsideTop || allOutsideNear || allOutsideFar) {
        return false;
    }

    const int minX = std::max(0, static_cast<int>(std::floor(minXF)));
    const int maxX = std::min(static_cast<int>(m_width) - 1, static_cast<int>(std::ceil(maxXF)));
    const int minY = std::max(0, static_cast<int>(std::floor(minYF)));
    const int maxY = std::min(static_cast<int>(m_height) - 1, static_cast<int>(std::ceil(maxYF)));

    if (minX > maxX || minY > maxY) {
        return false;
    }

    // Test minZ against depth buffer.
    // If minZ is behind or equal to depth buffer in ALL pixels of bounding box, it is OCCLUDED.
    // If minZ is closer than depth buffer at ANY pixel, it is VISIBLE.
    const float testDepth = minZ - depthBias;

    for (int y = minY; y <= maxY; ++y) {
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
        for (int x = minX; x <= maxX; ++x) {
            const float bufferDepth = m_depthBuffer[rowOffset + static_cast<size_t>(x)];
            if (testDepth < bufferDepth) {
                // Closer than depth buffer at this pixel -> VISIBLE!
                return false;
            }
        }
    }

    // All pixels in the bounding box were occluded by closer geometry!
    return true;
}

void SoftwareDepthBuffer::GenerateVisualTexture(std::vector<uint32_t>& outRgba, bool falseColor) const {
    const size_t totalPixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (outRgba.size() != totalPixels) {
        outRgba.resize(totalPixels);
    }

    for (size_t i = 0; i < totalPixels; ++i) {
        const float d = std::clamp(m_depthBuffer[i], 0.0f, 1.0f);

        if (d >= 0.999f) {
            // Background / Sky (dark blue/slate)
            outRgba[i] = 0xFF2A1A14; // RGBA: R=0x14, G=0x1A, B=0x2A, A=0xFF (little-endian uint32)
            continue;
        }

        // Apply non-linear scaling for better depth perception
        const float depthVal = std::clamp(d, 0.0f, 1.0f);

        if (!falseColor) {
            // Grayscale: closer is brighter
            const uint8_t lum = static_cast<uint8_t>((1.0f - depthVal) * 255.0f);
            outRgba[i] = 0xFF000000 | (static_cast<uint32_t>(lum) << 16) | (static_cast<uint32_t>(lum) << 8) | lum;
        } else {
            // False color / Turbo-like heatmap:
            // near (0.0): Cyan/Green -> Mid: Yellow/Orange -> Far: Red/Purple
            const float t = 1.0f - depthVal; // 1.0 = near, 0.0 = far
            uint8_t r = 0, g = 0, b = 0;

            if (t > 0.66f) {
                const float localT = (t - 0.66f) / 0.34f;
                r = static_cast<uint8_t>(localT * 255.0f);
                g = 255;
                b = static_cast<uint8_t>((1.0f - localT) * 255.0f);
            } else if (t > 0.33f) {
                const float localT = (t - 0.33f) / 0.33f;
                r = 255;
                g = static_cast<uint8_t>(localT * 255.0f);
                b = 0;
            } else {
                const float localT = t / 0.33f;
                r = static_cast<uint8_t>(localT * 255.0f);
                g = 0;
                b = static_cast<uint8_t>((1.0f - localT) * 128.0f);
            }

            outRgba[i] = 0xFF000000 | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
        }
    }
}

} // namespace Engine
