#include "graphics/debug/CameraAndLightVisualizer.hpp"

#include "graphics/Camera.hpp"

#include "Scene/CameraComponent.hpp"
#include "Scene/LightComponent.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace elm::graphics {

    namespace {

        constexpr StringView CamerasChannel = "Scene/Cameras";
        constexpr StringView LightsChannel = "Scene/Lights";
        constexpr math::Vector4 CameraColor { 0.4f, 0.8f, 1.0f, 1.0f };
        constexpr math::Vector4 LightDirectionalColor { 1.0f, 1.0f, 0.4f, 1.0f };
        constexpr math::Vector4 LightPointColor { 1.0f, 0.85f, 0.3f, 1.0f };
        constexpr math::Vector4 LightSpotColor { 1.0f, 0.7f, 0.2f, 1.0f };
        constexpr math::Vector4 LightSpotInnerColor { 0.8f, 0.6f, 0.15f, 0.6f };
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float Deg2Rad = Pi / 180.0f;

        /// The frame of a camera node with unit axes: the model keeps its size whatever the node's scale.
        struct CameraFrame {
            math::Vector3 position;
            math::Vector3 right;
            math::Vector3 up;
            math::Vector3 forward;
        };

        /// A part of the camera model: a solid between two rectangles across the view, its back face at
        /// backCenter and its front face length further along the view. In scene units, x right, y up, z forward.
        struct CameraBlock {
            math::Vector3 backCenter;
            float length { 0.0f };
            float backHalfWidth { 0.0f };
            float backHalfHeight { 0.0f };
            float frontHalfWidth { 0.0f };
            float frontHalfHeight { 0.0f };
            math::Vector4 color;
        };

        constexpr math::Vector4 CameraBodyColor { 0.55f, 0.6f, 0.68f, 1.0f };
        constexpr math::Vector4 CameraLensColor { 0.22f, 0.25f, 0.3f, 1.0f };
        /// Body, lens hood widening forward, viewfinder on top.
        constexpr std::array CameraModel {
            CameraBlock { { 0.0f, 0.0f, -0.12f }, 0.24f, 0.1f, 0.075f, 0.1f, 0.075f, CameraBodyColor },
            CameraBlock { { 0.0f, 0.0f, 0.12f }, 0.08f, 0.045f, 0.045f, 0.065f, 0.055f, CameraLensColor },
            CameraBlock { { 0.0f, 0.1f, -0.1f }, 0.1f, 0.035f, 0.025f, 0.035f, 0.025f, CameraBodyColor },
        };
        /// The view arrow starts at the front of the lens.
        constexpr float CameraArrowStart = 0.2f;
        constexpr float CameraArrowLength = 0.25f;
        /// Faces are shaded by a fixed light from above so the solid reads as one.
        constexpr float CameraAmbient = 0.55f;
        constexpr float CameraDiffuse = 0.45f;
        /// How far from its node the model reaches, arrow aside.
        constexpr float CameraModelRadius = 0.25f;
        constexpr math::Vector3 CameraLightDirection { 0.3f, 0.9f, -0.3f };

        /// Corners 0-3 are the back face, 4-7 the front one, both in the order (-x -y), (+x -y), (+x +y), (-x +y).
        std::array<math::Vector3, 8> CornersOf(const CameraFrame& frame, const CameraBlock& block)
        {
            constexpr std::array<float, 4> SignX { -1.0f, 1.0f, 1.0f, -1.0f };
            constexpr std::array<float, 4> SignY { -1.0f, -1.0f, 1.0f, 1.0f };
            const math::Vector3 back = frame.position + frame.right * block.backCenter.x + frame.up * block.backCenter.y
                + frame.forward * block.backCenter.z;
            const math::Vector3 front = back + frame.forward * block.length;
            std::array<math::Vector3, 8> corners;
            for (size_t corner = 0; corner < 4; ++corner) {
                corners[corner] = back + frame.right * (SignX[corner] * block.backHalfWidth) + frame.up * (SignY[corner] * block.backHalfHeight);
                corners[corner + 4] = front + frame.right * (SignX[corner] * block.frontHalfWidth)
                    + frame.up * (SignY[corner] * block.frontHalfHeight);
            }
            return corners;
        }

        /// A quad face of a block, shaded by how much it faces the light; center is the block's, to find outward.
        void DrawFace(DebugDraw& debugDraw, DebugChannel channel, std::span<const math::Vector3, 4> quad, const math::Vector3& center,
            const math::Vector4& color)
        {
            math::Vector3 normal = (quad[1] - quad[0]).Cross(quad[2] - quad[0]).Normalized();
            if (normal.Dot((quad[0] + quad[2]) * 0.5f - center) < 0.0f)
                normal = normal * -1.0f;
            const float light = CameraAmbient + CameraDiffuse * std::max(0.0f, normal.Dot(CameraLightDirection.Normalized()));
            const DebugPen pen { .color = { color.x * light, color.y * light, color.z * light, color.w } };
            debugDraw.Triangle(channel, std::array { quad[0], quad[1], quad[2] }, pen);
            debugDraw.Triangle(channel, std::array { quad[0], quad[2], quad[3] }, pen);
        }

        /// The six faces of a block and its twelve edges in the outline pen.
        void DrawBlock(DebugDraw& debugDraw, DebugChannel channel, const CameraFrame& frame, const CameraBlock& block, const DebugPen& outline)
        {
            const std::array<math::Vector3, 8> corners = CornersOf(frame, block);
            const math::Vector3 center = (corners[0] + corners[6]) * 0.5f;
            DrawFace(debugDraw, channel, std::array { corners[0], corners[3], corners[2], corners[1] }, center, block.color);
            DrawFace(debugDraw, channel, std::array { corners[4], corners[5], corners[6], corners[7] }, center, block.color);
            for (size_t corner = 0; corner < 4; ++corner) {
                const size_t next = (corner + 1) % 4;
                DrawFace(debugDraw, channel, std::array { corners[corner], corners[next], corners[next + 4], corners[corner + 4] }, center,
                    block.color);
                debugDraw.Line(channel, corners[corner], corners[next], outline);
                debugDraw.Line(channel, corners[corner + 4], corners[next + 4], outline);
                debugDraw.Line(channel, corners[corner], corners[corner + 4], outline);
            }
        }

        /// A small solid camera of a fixed size in the scene, looking along the node's +Z, and an arrow of its view.
        void DrawCamera(DebugDraw& debugDraw, DebugChannel channel, const math::Matrix4x4& transform, const DebugPen& pen)
        {
            const CameraFrame frame {
                .position = { transform(0, 3), transform(1, 3), transform(2, 3) },
                .right = math::Vector3 { transform(0, 0), transform(1, 0), transform(2, 0) }.Normalized(),
                .up = math::Vector3 { transform(0, 1), transform(1, 1), transform(2, 1) }.Normalized(),
                .forward = math::Vector3 { transform(0, 2), transform(1, 2), transform(2, 2) }.Normalized(),
            };
            for (const CameraBlock& block : CameraModel)
                DrawBlock(debugDraw, channel, frame, block, pen);
            const math::Vector3 arrowStart = frame.position + frame.forward * CameraArrowStart;
            debugDraw.Arrow(channel, arrowStart, arrowStart + frame.forward * CameraArrowLength, pen);
        }

        void DrawDirectionalLight(DebugDraw& debugDraw, DebugChannel channel,
            const math::Matrix4x4& transform, const DebugPen& pen)
        {
            const math::Vector3 pos { transform(0, 3), transform(1, 3), transform(2, 3) };
            const math::Vector3 forward { transform(0, 2), transform(1, 2), transform(2, 2) };
            const math::Vector3 right { transform(0, 0), transform(1, 0), transform(2, 0) };
            const math::Vector3 up { transform(0, 1), transform(1, 1), transform(2, 1) };

            constexpr float arrowLen = 2.0f;
            constexpr float circleRadius = 0.5f;
            debugDraw.Circle(channel, pos, forward, circleRadius, pen);
            // rays from the circle
            constexpr int rays = 4;
            for (int i = 0; i < rays; ++i) {
                const float angle = static_cast<float>(i) * (2.0f * Pi / static_cast<float>(rays));
                const math::Vector3 offset = right * (circleRadius * std::cos(angle)) + up * (circleRadius * std::sin(angle));
                debugDraw.Arrow(channel, pos + offset, pos + offset + forward * arrowLen, pen);
            }
        }

        void DrawPointLight(DebugDraw& debugDraw, DebugChannel channel, float range,
            const math::Matrix4x4& transform, const DebugPen& pen)
        {
            debugDraw.Sphere(channel, transform, range, pen);
        }

        void DrawSpotLight(DebugDraw& debugDraw, DebugChannel channel, const LightComponent& light,
            const math::Matrix4x4& transform, const DebugPen& outerPen, const DebugPen& innerPen)
        {
            const math::Vector3 pos { transform(0, 3), transform(1, 3), transform(2, 3) };
            const math::Vector3 forward { transform(0, 2), transform(1, 2), transform(2, 2) };
            const math::Vector3 right { transform(0, 0), transform(1, 0), transform(2, 0) };
            const math::Vector3 up { transform(0, 1), transform(1, 1), transform(2, 1) };

            const float outerRad = light.range * std::tan(light.outerConeAngle * Deg2Rad);
            const float innerRad = light.range * std::tan(light.innerConeAngle * Deg2Rad);
            const math::Vector3 tip = pos + forward * light.range;

            // outer cone
            debugDraw.Circle(channel, tip, forward, outerRad, outerPen);
            for (int i = 0; i < 4; ++i) {
                const float angle = static_cast<float>(i) * (Pi * 0.5f);
                const math::Vector3 offset = right * (outerRad * std::cos(angle)) + up * (outerRad * std::sin(angle));
                debugDraw.Line(channel, pos, tip + offset, outerPen);
            }
            // inner cone
            debugDraw.Circle(channel, tip, forward, innerRad, innerPen);
        }

    } // namespace

    CameraAndLightVisualizer::CameraAndLightVisualizer(const Scene& scene, const Camera& view, DebugDraw& debugDraw)
        : m_scene(scene)
        , m_view(view)
        , m_camerasChannel(debugDraw.RegisterChannel(CamerasChannel, "Cameras", true))
        , m_lightsChannel(debugDraw.RegisterChannel(LightsChannel, "Lights", true))
    {
    }

    void CameraAndLightVisualizer::Draw(DebugDraw& debugDraw)
    {
        const bool camerasEnabled = debugDraw.IsEnabled(m_camerasChannel);
        const bool lightsEnabled = debugDraw.IsEnabled(m_lightsChannel);
        if (!camerasEnabled && !lightsEnabled)
            return;

        const auto worldTransforms = m_scene.WorldTransforms();

        if (camerasEnabled) {
            if (const auto* cameras = m_scene.FindComponents<CameraComponent>()) {
                const auto ids = cameras->Ids();
                const auto components = cameras->Components();
                const DebugPen pen { .color = CameraColor };
                for (size_t index = 0; index < components.size(); ++index) {
                    const size_t nodeIndex = m_scene.IndexOf(ids[index]);
                    if (nodeIndex == Scene::NoNode)
                        continue;
                    // The view inside a camera model (Play looks through the scene's camera) would see only the model
                    const math::Matrix4x4& transform = worldTransforms[nodeIndex];
                    const math::Vector3 position { transform(0, 3), transform(1, 3), transform(2, 3) };
                    if ((position - m_view.GetPosition()).Length() < CameraModelRadius)
                        continue;
                    DrawCamera(debugDraw, m_camerasChannel, transform, pen);
                }
            }
        }

        if (lightsEnabled) {
            if (const auto* lights = m_scene.FindComponents<LightComponent>()) {
                const auto ids = lights->Ids();
                const auto components = lights->Components();
                for (size_t index = 0; index < components.size(); ++index) {
                    const size_t nodeIndex = m_scene.IndexOf(ids[index]);
                    if (nodeIndex == Scene::NoNode)
                        continue;
                    const LightComponent& light = components[index];
                    const math::Matrix4x4& transform = worldTransforms[nodeIndex];
                    switch (light.type) {
                    case LightType::Directional: {
                        const DebugPen pen { .color = LightDirectionalColor };
                        DrawDirectionalLight(debugDraw, m_lightsChannel, transform, pen);
                        break;
                    }
                    case LightType::Point: {
                        const DebugPen pen { .color = LightPointColor };
                        DrawPointLight(debugDraw, m_lightsChannel, light.range, transform, pen);
                        break;
                    }
                    case LightType::Spot: {
                        const DebugPen outerPen { .color = LightSpotColor };
                        const DebugPen innerPen { .color = LightSpotInnerColor };
                        DrawSpotLight(debugDraw, m_lightsChannel, light, transform, outerPen, innerPen);
                        break;
                    }
                    }
                }
            }
        }
    }

} // namespace elm::graphics
