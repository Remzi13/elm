#include "editor/ui/SocLabWindow.hpp"

#include "graphics/culling/OcclusionCulling.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace elm {

    namespace {
        ImTextureID ToTextureId(render::TextureHandle texture)
        {
            if (!texture.IsValid())
                return nullptr;
            // Real texture views are aligned pointers, so the lowest bit marks an engine handle
            const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
            return reinterpret_cast<ImTextureID>(value);
        }
    } // namespace

    void SocLabWindow::Render(render::FrameWriter&)
    {
        if (!m_visible) return;

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, 680.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Software Depth Buffer Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!m_culling) {
                ImGui::TextDisabled("No Occlusion Culling System connected.");
            } else {
                const uint32_t depthWidth = m_culling->GetWidth();
                const uint32_t depthHeight = m_culling->GetHeight();
                ImGui::Text("Resolution: %ux%u", depthWidth, depthHeight);
                ImGui::SameLine();
                auto falseColor = m_settings.Get<bool>(Settings::Category::Render,
                    OcclusionCulling::DepthFalseColorSetting);
                if (ImGui::Checkbox("False Color (Heatmap)", &falseColor)) {
                    m_settings.Set(Settings::Category::Render, OcclusionCulling::DepthFalseColorSetting, falseColor);
                }

                const ImTextureID texture = ToTextureId(m_culling->GetDepthPreviewTexture().GetHandle());
                if (texture && depthHeight > 0) {
                    const float aspect = static_cast<float>(depthWidth) / static_cast<float>(depthHeight);
                    const float width = ImGui::GetContentRegionAvail().x;
                    const float height = width / aspect;
                    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
                    ImGui::Image(texture, ImVec2(width, height));
                    if (ImGui::IsItemHovered()) {
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const uint32_t px = static_cast<uint32_t>(((mouse.x - imagePos.x) / width) * static_cast<float>(depthWidth));
                        const uint32_t py = static_cast<uint32_t>(((mouse.y - imagePos.y) / height) * static_cast<float>(depthHeight));
                        if (px < depthWidth && py < depthHeight) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Pixel: (%u, %u)", px, py);
                            ImGui::Text("Normalized Depth: %.4f", m_culling->GetDepth(px, py));
                            ImGui::EndTooltip();
                        }
                    }
                }
            }
        }

        if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
            int mode = static_cast<int>(m_settings.Get<uint32_t>(Settings::Category::Render,
                OcclusionCulling::VisualModeSetting));
            bool modeChanged = false;
            if (ImGui::RadioButton("Hide Culled (Draw Visible Only)", &mode, 0)) modeChanged = true;
            if (ImGui::RadioButton("Highlight Culled (Red Ghost)", &mode, 1)) modeChanged = true;
            if (ImGui::RadioButton("Occluders Only", &mode, 2)) modeChanged = true;
            if (modeChanged) {
                m_settings.Set(Settings::Category::Render, OcclusionCulling::VisualModeSetting,
                    static_cast<uint32_t>(mode));
            }
        }

        if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_culling) {
                const auto& s = m_culling->GetStats();
                ImGui::Text("Total Objects:     %u", s.totalObjects);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", s.visibleCount);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", s.frustumCulledCount);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", s.occlusionCulledCount);
                ImGui::Spacing();
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "Culled: %.1f%%", s.cullingRatioPercent);
                ImGui::ProgressBar(s.cullingRatioPercent / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
                ImGui::Separator();
                ImGui::Text("CPU Rasterize Time: %.1f us (%.3f ms)", s.rasterizeTimeUs, s.rasterizeTimeUs / 1000.0f);
                ImGui::Text("CPU Query / Test:   %.1f us (%.3f ms)", s.queryTimeUs, s.queryTimeUs / 1000.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time: %.1f us (%.3f ms)",
                    s.totalCullingTimeUs, s.totalCullingTimeUs / 1000.0f);
            } else {
                ImGui::TextDisabled("No Occlusion Culling System connected.");
            }
        }

        if (ImGui::CollapsingHeader("Camera & Controls")) {
            ImGui::Separator();
            ImGui::TextDisabled("Controls:");
            ImGui::BulletText("Right Mouse Button + Drag: Look around");
            ImGui::BulletText("W / A / S / D: Move forward / left / back / right");
            ImGui::BulletText("E / Q: Move Up / Down");
            ImGui::BulletText("Left Shift: Sprint (2.5x speed)");
        }
        ImGui::End();
    }

} // namespace elm
