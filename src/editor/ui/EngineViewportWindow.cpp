#include "editor/ui/EngineViewportWindow.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

#include "core/Log.hpp"
#include "graphics/Camera.hpp"
#include "graphics/debug/DebugDraw.hpp"
#include "render/ViewPort.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/Scene.hpp"

#include "imgui.h"

#include <span>

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

        /// Icons of the view menu: symbols of the icon font (EditorStyle), in UTF-8, with the id after ###.
        constexpr const char* MenuIcon = "\xE2\x98\xB0###ViewMenu";      // U+2630
        constexpr const char* MoveIcon = "\xE2\x9C\xA5###Move";          // U+2725
        constexpr const char* RotateIcon = "\xE2\x86\xBB###Rotate";      // U+21BB
        constexpr const char* ScaleIcon = "\xE2\x87\xB2###Scale";        // U+21F2
        constexpr const char* WorldIcon = "\xE2\x8A\x95###GizmoSpace";   // U+2295
        constexpr const char* LocalIcon = "\xE2\x8A\x99###GizmoSpace";   // U+2299
        constexpr const char* DebugIcon = "\xE2\x9A\x91###DebugSection"; // U+2691
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

        const Scene::Node* selected = scene.Find(m_deps.selection.node);
        // A node the user hid gets no gizmo; a node without a model is not hidden
        const auto* nodeModels = scene.FindComponents<ModelComponent>();
        const ModelComponent* selectedModel = selected && nodeModels ? nodeModels->Find(selected->id) : nullptr;
        const bool selectedShown = selected && (!selectedModel || selectedModel->visible);

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
                    UpdateGizmo(selected->id, input);
                else if (m_gizmo)
                    m_gizmo->Reset();
                if (m_hovered && !playing)
                    HandleMouse(input);
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
        if (m_menuOpen) {
            ImGui::SameLine();
            DrawGizmoSection();
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
            FrameNode(m_deps.camera, m_deps.scene, m_deps.models, m_deps.selection.node);
    }

    void EngineViewportWindow::HandleMouse(const graphics::GizmoInput& input)
    {
        // A press on a handle of the gizmo is its drag, not a pick
        const bool onGizmo = m_gizmo && (m_gizmo->IsDragging() || m_gizmo->GetHovered() != graphics::GizmoAxis::None);
        if (input.pressed && !onGizmo)
            m_deps.selection.node = PickNode(m_deps.scene, m_deps.models, input.ray);
        if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f)
            m_deps.camera.Translate(m_deps.camera.GetForwardDirection() * (wheel * WheelStep));
    }

    void EngineViewportWindow::UpdateGizmo(NodeId node, const graphics::GizmoInput& input)
    {
        if (!m_gizmo)
            m_gizmo.emplace(m_deps.debugDraw);
        // A gizmo switched off in the Debug menu is not there to grab either
        if (!m_deps.debugDraw.IsEnabled(m_gizmo->Channel())) {
            m_gizmo->Reset();
            return;
        }
        Scene& scene = m_deps.scene;
        const Matrix4x4& world = scene.WorldTransforms()[scene.IndexOf(node)];
        if (const auto moved = m_gizmo->Update(world, m_deps.gizmo, input); moved && !scene.SetWorldTransform(node, *moved)) {
            ERROR_MESSAGE(log::Category::Core, "EngineViewportWindow",
                "Unable to update the selected node transform because its parent transform is singular");
        }
        m_gizmo->Draw(m_deps.debugDraw);
    }

} // namespace elm
