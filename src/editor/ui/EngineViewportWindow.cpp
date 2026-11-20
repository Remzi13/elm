#include "editor/ui/EngineViewportWindow.hpp"

#include "editor/CameraPilot.hpp"
#include "editor/NodeEditing.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

#include "core/Log.hpp"
#include "graphics/Camera.hpp"
#include "graphics/debug/DebugDraw.hpp"
#include "math/Rotation.hpp"
#include "render/ViewPort.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/Scene.hpp"

#include "imgui.h"

#include <span>
#include <utility>

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

        /// The fill of the selection rectangle over the view, its border is opaque.
        constexpr float MarqueeFillAlpha = 0.15f;
        /// Icons of the view menu: symbols of the icon font (EditorStyle), in UTF-8, with the id after ###.
        constexpr const char* MenuIcon = "\xE2\x98\xB0###ViewMenu";      // U+2630
        constexpr const char* MoveIcon = "\xE2\x9C\xA5###Move";          // U+2725
        constexpr const char* RotateIcon = "\xE2\x86\xBB###Rotate";      // U+21BB
        constexpr const char* ScaleIcon = "\xE2\x87\xB2###Scale";        // U+21F2
        constexpr const char* WorldIcon = "\xE2\x8A\x95###GizmoSpace";   // U+2295
        constexpr const char* LocalIcon = "\xE2\x8A\x99###GizmoSpace";   // U+2299
        constexpr const char* DebugIcon = "\xE2\x9A\x91###DebugSection"; // U+2691
        constexpr const char* PilotIcon = "\xE2\x9C\x88";                // U+2708
        /// Behind the view menu and its buttons: the scene shows through.
        constexpr ImU32 MenuBackColor = IM_COL32(18, 18, 22, 90);
        constexpr float MenuControlAlpha = 0.35f;
        /// Under the mouse a control is easier to see, yet not solid.
        constexpr float MenuHoveredAlpha = 0.7f;
        constexpr float MenuPadding = 6.0f;
        /// World units the camera moves along its view per notch of the wheel.
        constexpr float WheelStep = 1.0f;

        /// Under every label, one pixel down and right: readable on light and dark scenes.
        constexpr ImU32 LabelShadowColor = IM_COL32(0, 0, 0, 200);

        /// Debug labels over the image of the view at position with size, centered on their points.
        /// A label behind the camera is not shown; the scene does not hide labels.
        void DrawDebugLabels(std::span<const graphics::DebugLabel> labels, const Camera& camera, ImVec2 position, ImVec2 size)
        {
            if (labels.empty())
                return;
            const Matrix4x4 viewProjection = camera.GetViewProjectionMatrix();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
            for (const graphics::DebugLabel& label : labels) {
                const Vector4 clip = viewProjection.TransformPoint(label.position);
                if (clip.w <= 0.0f)
                    continue;
                // NDC y goes up, screen y down
                const ImVec2 textSize = ImGui::CalcTextSize(label.text.c_str());
                const float x = position.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x - textSize.x * 0.5f;
                const float y = position.y + (0.5f - clip.y / clip.w * 0.5f) * size.y - textSize.y * 0.5f;
                const ImVec4 color { label.color.x, label.color.y, label.color.z, label.color.w };
                drawList->AddText(ImVec2(x + 1.0f, y + 1.0f), LabelShadowColor, label.text.c_str());
                drawList->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), label.text.c_str());
            }
            drawList->PopClipRect();
        }

        /// The mouse over the image of the view at position with size, for the gizmo.
        graphics::GizmoInput ReadGizmoInput(const Camera& camera, ImVec2 position, ImVec2 size, bool overView)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            // Screen y goes down, NDC y up
            const float ndcX = (mouse.x - position.x) / size.x * 2.0f - 1.0f;
            const float ndcY = 1.0f - (mouse.y - position.y) / size.y * 2.0f;
            return {
                camera.GetRayThrough(ndcX, ndcY),
                camera.GetPosition(),
                overView,
                overView && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                ImGui::IsMouseDown(ImGuiMouseButton_Left),
            };
        }

    } // namespace

    EngineViewportWindow::EngineViewportWindow(Settings& settings, const EngineViewportDeps& deps)
        : IImGuiWindow(settings)
        , m_deps(deps)
    {
    }

    void EngineViewportWindow::Render(render::FrameWriter&)
    {
        Scene& scene = m_deps.scene;
        Camera& camera = m_deps.camera;
        render::ViewPort& viewPort = m_deps.viewPort;
        m_hovered = false;
        m_focused = false;
        // Comes back only on a frame that shows the view: a hidden one would end it with a pick of its own
        const std::optional<ImVec2> marqueeStart = std::exchange(m_marqueeStart, std::nullopt);
        if (!m_visible) return;

        ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (m_focused)
            HandleSceneEditShortcuts(m_deps.edits, m_deps.sceneFile);
        // In Play the keys and the mouse are the game's
        const bool playing = m_deps.play.isPlaying && m_deps.play.isPlaying();
        if (m_focused && !playing)
            HandleKeys();

        const Scene::Node* selected = scene.Find(m_deps.selection.Active());
        // A node the user hid gets no gizmo; a node without a model is not hidden
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        const ModelComponent* selectedModel = selected && nodeModels ? nodeModels->Find(selected->id) : nullptr;
        // The piloted camera is where the view is: its handles would surround the eye
        const bool selectedShown = selected && (!selectedModel || selectedModel->visible) && selected->id != m_deps.pilot.Piloted();

        if (auto texture = ToTextureId(viewPort.GetColorTexture())) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const auto* viewport = ImGui::GetWindowViewport();
            const float dpiScale = viewport ? viewport->DpiScale : 1.0f;
            if (available.x > 0.0f && available.y > 0.0f) {
                const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
                viewPort.SetSize({ static_cast<uint32_t>(available.x * dpiScale),
                    static_cast<uint32_t>(available.y * dpiScale) });
                camera.SetAspect(available.x / available.y);
                ImGui::Image(texture, available);
                // A widget over the image takes the mouse: without one, dragging on it moves the window
                ImGui::SetCursorScreenPos(imagePosition);
                // The view menu lies on the canvas and takes the mouse there
                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("##ViewportCanvas", available, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                // Popups and other windows on top of the image block the hover
                const bool overCanvas = ImGui::IsItemHovered();
                // Under the view menu: it is drawn after them
                DrawDebugLabels(m_deps.debugDraw.GetPublishedLabels(), camera, imagePosition, available);
                m_hovered = !DrawViewMenu(imagePosition) && overCanvas;

                const graphics::GizmoInput input = ReadGizmoInput(camera, imagePosition, available, m_hovered);
                if (selectedShown)
                    UpdateGizmo(input);
                else if (m_gizmo)
                    m_gizmo->Reset();
                // In Play the mouse is the game's
                if (!playing)
                    m_marqueeStart = marqueeStart;
                if (m_hovered && !playing)
                    HandleMouse(input);
                // The drag goes on outside the view
                if (m_marqueeStart)
                    UpdateMarquee(input, imagePosition, available);
            }
        }
        ImGui::End();
    }

    bool EngineViewportWindow::DrawViewMenu(ImVec2 corner)
    {
        // The background goes under the controls, though their size is known only after them
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);
        ImVec4 button = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        ImVec4 buttonHovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        ImVec4 frameHovered = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
        button.w *= MenuControlAlpha;
        frame.w *= MenuControlAlpha;
        buttonHovered.w *= MenuHoveredAlpha;
        frameHovered.w *= MenuHoveredAlpha;
        ImGui::PushStyleColor(ImGuiCol_Button, button);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, frame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameHovered);
        ImGui::SetCursorScreenPos(ImVec2(corner.x + MenuPadding * 2.0f, corner.y + MenuPadding * 2.0f));
        ImGui::BeginGroup();
        if (ui::ToggleButton(MenuIcon, m_menuOpen))
            m_menuOpen = !m_menuOpen;
        ImGui::SetItemTooltip("The controls of the view");
        DrawPilotButton();
        if (m_menuOpen) {
            ImGui::SameLine();
            DrawGizmoSection();
            ImGui::SameLine();
            DrawCameraSection();
            ImGui::SameLine();
            DrawDebugSection();
        }
        ImGui::EndGroup();
        ImGui::PopStyleColor(4);
        const ImVec2 from { ImGui::GetItemRectMin().x - MenuPadding, ImGui::GetItemRectMin().y - MenuPadding };
        const ImVec2 to { ImGui::GetItemRectMax().x + MenuPadding, ImGui::GetItemRectMax().y + MenuPadding };
        drawList->ChannelsSetCurrent(0);
        drawList->AddRectFilled(from, to, MenuBackColor, ImGui::GetStyle().FrameRounding);
        drawList->ChannelsMerge();
        return ImGui::IsMouseHoveringRect(from, to);
    }

    void EngineViewportWindow::DrawPilotButton()
    {
        const Scene::Node* piloted = m_deps.scene.Find(m_deps.pilot.Piloted());
        if (!piloted)
            return;
        ImGui::SameLine();
        const String label = String(PilotIcon) + "  " + piloted->name + "###Pilot";
        if (ui::ToggleButton(label.c_str(), true))
            m_deps.pilot.Stop();
        ImGui::SetItemTooltip("Piloting: the view looks through this camera, right mouse + WASD move it. Click or Esc to stop");
    }

    void EngineViewportWindow::DrawGizmoSection()
    {
        GizmoSettings& gizmo = m_deps.gizmo;
        constexpr struct {
            const char* label;
            const char* tooltip;
            GizmoOperation operation;
        } Operations[] {
            { MoveIcon, "Move: the gizmo moves the node (W)", GizmoOperation::Translate },
            { RotateIcon, "Rotate: the gizmo rotates the node (E)", GizmoOperation::Rotate },
            { ScaleIcon, "Scale: the gizmo scales the node, always along its own axes (R)", GizmoOperation::Scale },
        };
        for (const auto& item : Operations) {
            if (ui::ToggleButton(item.label, gizmo.operation == item.operation))
                gizmo.operation = item.operation;
            ImGui::SetItemTooltip("%s", item.tooltip);
            ImGui::SameLine();
        }
        const bool world = gizmo.space == GizmoSpace::World;
        if (ImGui::Button(world ? WorldIcon : LocalIcon))
            gizmo.space = world ? GizmoSpace::Local : GizmoSpace::World;
        ImGui::SetItemTooltip(world ? "World: the gizmo follows the axes of the world. Click for the node's own" : "Local: the gizmo follows the node's own axes. Click for the world's");

        constexpr const char* SnapIcon = "\xE2\x96\xA6###GizmoSnap"; // U+25A6 ▦
        ImGui::SameLine();
        if (ui::ToggleButton(SnapIcon, gizmo.snap))
            gizmo.snap = !gizmo.snap;
        ImGui::SetItemTooltip(gizmo.snap ? "Snap is ON (grid: %.2g, angle: %.0f deg). Click to toggle, right-click for settings, hold Ctrl while dragging"
                                         : "Snap is OFF. Click to toggle, right-click for settings, hold Ctrl while dragging",
            gizmo.snapTranslate, gizmo.snapRotate);
        if (ImGui::BeginPopupContextItem("GizmoSnapPopup")) {
            ImGui::TextDisabled("Snap Settings");
            ImGui::Separator();
            ImGui::Checkbox("Snap Enabled", &gizmo.snap);
            ImGui::DragFloat("Grid Step", &gizmo.snapTranslate, 0.1f, 0.05f, 50.0f, "%.2f");
            ImGui::DragFloat("Angle Step", &gizmo.snapRotate, 1.0f, 1.0f, 90.0f, "%.0f deg");
            ImGui::EndPopup();
        }
    }

    void EngineViewportWindow::DrawCameraSection()
    {
        constexpr const char* CameraPresetIcon = "\xE2\x97\x8E###CameraPresets"; // U+25CE ◎
        constexpr float Pi = 3.14159265358979323846f;
        if (ImGui::Button(CameraPresetIcon))
            ImGui::OpenPopup("CameraPresetsPopup");
        ImGui::SetItemTooltip("Camera view presets and speed");
        if (ImGui::BeginPopup("CameraPresetsPopup")) {
            ImGui::TextDisabled("View Presets");
            if (ImGui::MenuItem("Top View (XZ)")) {
                const auto pos = m_deps.camera.GetPosition();
                m_deps.camera.SetPosition({ pos.x, (std::max)(pos.y, 15.0f), pos.z });
                m_deps.camera.SetOrientation(0.0f, -1.55f);
            }
            if (ImGui::MenuItem("Front View (XY)")) {
                const auto pos = m_deps.camera.GetPosition();
                m_deps.camera.SetPosition({ pos.x, pos.y, (std::min)(pos.z, -10.0f) });
                m_deps.camera.SetOrientation(0.0f, 0.0f);
            }
            if (ImGui::MenuItem("Side View (ZY)")) {
                const auto pos = m_deps.camera.GetPosition();
                m_deps.camera.SetPosition({ (std::min)(pos.x, -10.0f), pos.y, pos.z });
                m_deps.camera.SetOrientation(Pi * 0.5f, 0.0f);
            }
            ImGui::Separator();
            ImGui::DragFloat("Move Speed", &m_deps.camera.moveSpeed, 0.5f, 0.5f, 200.0f, "%.1f");
            ImGui::EndPopup();
        }
    }

    void EngineViewportWindow::DrawDebugSection()
    {
        if (ui::ToggleButton(DebugIcon, m_debugSectionOpen))
            m_debugSectionOpen = !m_debugSectionOpen;
        ImGui::SetItemTooltip("Debug draw: what the view draws over the scene");
        if (!m_debugSectionOpen)
            return;
        // Checkbox per debug draw channel, grouped by the part of the name before the first '/'
        String group;
        for (const auto& channel : m_deps.debugDraw.GetChannels()) {
            const String channelGroup = channel.name.substr(0, channel.name.find('/'));
            if (channelGroup != group) {
                ImGui::TextDisabled("%s", channelGroup.c_str());
                group = channelGroup;
            }
            bool enabled = channel.enabled;
            ImGui::PushID(channel.name.c_str());
            if (ImGui::Checkbox(channel.label.c_str(), &enabled))
                m_deps.debugDraw.SetEnabled(channel.channel, enabled);
            ImGui::PopID();
        }
    }

    void EngineViewportWindow::HandleKeys()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || io.KeyCtrl || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;
        GizmoSettings& gizmo = m_deps.gizmo;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            gizmo.operation = GizmoOperation::Translate;
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            gizmo.operation = GizmoOperation::Rotate;
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            gizmo.operation = GizmoOperation::Scale;
        else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            FrameNodes(m_deps.camera, m_deps.scene, m_deps.models, m_deps.selection.Nodes());
        else if (ImGui::IsKeyPressed(ImGuiKey_End, false))
            SnapNodesToGround(m_deps.scene, m_deps.models, m_deps.selection.Nodes());
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_deps.pilot.Stop();
    }

    void EngineViewportWindow::UpdateMarquee(const graphics::GizmoInput& input, ImVec2 imagePosition, ImVec2 size)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 start = *m_marqueeStart;
        const ImVec2 end = io.MousePos;
        const ImVec2 min((std::min)(start.x, end.x), (std::min)(start.y, end.y));
        const ImVec2 max((std::max)(start.x, end.x), (std::max)(start.y, end.y));
        const bool dragged = max.x - min.x > io.MouseDragThreshold || max.y - min.y > io.MouseDragThreshold;
        if (input.down) {
            if (dragged) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_DragDropTarget, MarqueeFillAlpha));
                drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
            }
            return;
        }
        m_marqueeStart.reset();
        if (!dragged) {
            const NodeId picked = PickNode(m_deps.scene, m_deps.models, input.ray);
            SelectPicked(picked ? std::span { &picked, 1 } : std::span<const NodeId> {}, false);
            return;
        }
        // Screen y goes down, NDC y up
        const auto ndcX = [&](float x) { return (x - imagePosition.x) / size.x * 2.0f - 1.0f; };
        const auto ndcY = [&](float y) { return 1.0f - (y - imagePosition.y) / size.y * 2.0f; };
        const ViewRect rect { ndcX(min.x), ndcY(max.y), ndcX(max.x), ndcY(min.y) };
        SelectPicked(PickNodesInRect(m_deps.scene, m_deps.models, m_deps.camera.GetViewProjectionMatrix(), rect), true);
    }

    void EngineViewportWindow::SelectPicked(std::span<const NodeId> picked, bool dragged)
    {
        Selection& selection = m_deps.selection;
        // A Ctrl click past every node keeps the selection
        if (!ImGui::GetIO().KeyCtrl) {
            selection.Clear();
        } else if (!dragged && !picked.empty() && selection.Contains(picked.front())) {
            selection.Remove(picked.front());
            return;
        }
        for (const NodeId node : picked)
            selection.Add(node);
    }

    void EngineViewportWindow::HandleMouse(const graphics::GizmoInput& input)
    {
        // A press on a handle of the gizmo is its drag, not a pick
        const bool onGizmo = m_gizmo && (m_gizmo->IsDragging() || m_gizmo->GetHovered() != graphics::GizmoAxis::None);
        if (input.pressed && !onGizmo)
            m_marqueeStart = ImGui::GetIO().MousePos;
        if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                constexpr float SpeedFactor = 1.25f;
                const float factor = wheel > 0.0f ? SpeedFactor : (1.0f / SpeedFactor);
                m_deps.camera.moveSpeed = std::clamp(m_deps.camera.moveSpeed * factor, 0.5f, 200.0f);
            } else {
                m_deps.camera.Translate(m_deps.camera.GetForwardDirection() * (wheel * WheelStep));
            }
        }
    }

    void EngineViewportWindow::UpdateGizmo(const graphics::GizmoInput& input)
    {
        if (!m_gizmo)
            m_gizmo.emplace(m_deps.debugDraw);
        // A gizmo switched off in the Debug menu is not there to grab either
        if (!m_deps.debugDraw.IsEnabled(m_gizmo->Channel())) {
            m_gizmo->Reset();
            return;
        }
        const bool wasDragging = m_gizmo->IsDragging();
        if (!wasDragging)
            m_gizmoTarget.handles = m_gizmoTarget.start = FindGizmoPivot();
        GizmoSettings effectiveSettings = m_deps.gizmo;
        if (ImGui::GetIO().KeyCtrl)
            effectiveSettings.snap = !effectiveSettings.snap;
        const auto moved = m_gizmo->Update(m_gizmoTarget.handles, effectiveSettings, input);
        // The nodes as the drag found them: every frame of it moves them from there
        if (!wasDragging && m_gizmo->IsDragging()) {
            const Scene& scene = m_deps.scene;
            m_gizmoTarget.nodes.clear();
            for (const NodeId node : TopmostNodes(scene, m_deps.selection.Nodes()))
                m_gizmoTarget.nodes.push_back(NodeStart { node, scene.WorldTransforms()[scene.IndexOf(node)] });
        }
        if (moved)
            MoveGizmoTarget(*moved);
        m_gizmo->Draw(m_deps.debugDraw);
    }

    Matrix4x4 EngineViewportWindow::FindGizmoPivot() const
    {
        const Scene& scene = m_deps.scene;
        const auto selected = m_deps.selection.Nodes();
        Matrix4x4 pivot = scene.WorldTransforms()[scene.IndexOf(m_deps.selection.Active())];
        const auto group = selected.size() > 1 ? GroupBounds(scene, m_deps.models, selected) : std::nullopt;
        if (group) {
            const Vector3 center = group->GetCenter();
            pivot(0, 3) = center.x;
            pivot(1, 3) = center.y;
            pivot(2, 3) = center.z;
        }
        return pivot;
    }

    void EngineViewportWindow::MoveGizmoTarget(const Matrix4x4& handles)
    {
        m_gizmoTarget.handles = handles;
        const auto startInverse = math::Inverse(m_gizmoTarget.start);
        bool moved = true;
        for (const NodeStart& start : m_gizmoTarget.nodes) {
            // The node the handles stand on goes exactly with them: a snapped move lands it on the grid
            const bool onHandles = start.world == m_gizmoTarget.start;
            if (!onHandles && !startInverse) {
                moved = false;
                continue;
            }
            const Matrix4x4 world = onHandles ? handles : handles * *startInverse * start.world;
            moved = m_deps.scene.SetWorldTransform(start.node, world) && moved;
        }
        if (!moved)
            ERROR_MESSAGE(log::Category::Core, "EngineViewportWindow", "Unable to move a selected node: a transform is singular");
    }

} // namespace elm
