#include "editor/ui/SceneHierarchyWindow.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

#include "graphics/Camera.hpp"

#include "model/ModelCatalog.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ScriptComponent.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace elm {
    namespace {

        using StatusKind = SceneStatus::Kind;

        constexpr const char* NodeDragPayload = "SCENE_NODE_DRAG";
        constexpr const char* AddNodePopup = "AddNodePopup";
        constexpr const char* ModelFileExtensions = ".model;.gltf;.glb";

        /// Icons: symbols of the icon font (EditorStyle), in UTF-8; in a label the id comes after ###.
        constexpr const char* AddNodeButton = "\xE2\x9C\x9A###AddNode";               // U+271A
        constexpr const char* ShownIcon = "\xE2\x97\x89";                             // U+25C9
        constexpr const char* HiddenIcon = "\xE2\x97\x8B";                            // U+25CB
        constexpr const char* AddChildItem = "\xE2\x9C\x9A  Add Child###Add Child";   // U+271A
        constexpr const char* RenameItem = "\xE2\x9C\x8E  Rename###Rename";           // U+270E
        constexpr const char* DuplicateItem = "\xE2\x9D\x90  Duplicate###Duplicate";  // U+2750
        constexpr const char* UnparentItem = "\xE2\x86\xB0  Move to Root###Unparent"; // U+21B0
        constexpr const char* DeleteItem = "\xE2\x9C\x96  Delete###Delete Node";      // U+2716

        /// A column of the tree: the icon of a component, for each node that has one.
        struct ComponentChip {
            const char* icon;
            const char* name;
        };
        constexpr std::array<ComponentChip, 4> ComponentChips { {
            { "\xE2\x96\xA3", "Model" },     // U+25A3
            { "\xE2\x9A\x9B", "RigidBody" }, // U+269B
            { "\xE2\x96\xA2", "Collider" },  // U+25A2
            { "\xE2\x9C\x8E", "Script" },    // U+270E
        } };
        /// The row of ComponentChips that a node with a model has: its visibility is the model's.
        constexpr size_t ModelChip = 0;
        /// The icon of visibility stands before the chips.
        constexpr size_t RowIconCount = ComponentChips.size() + 1;

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                       [&](char a, char b) { return lower(a) == lower(b); })
                != text.end();
        }

        String NodeDisplayName(const Scene::Node& node, size_t index)
        {
            if (!node.name.empty())
                return node.name;
            return String("Node ") + std::to_string(index).c_str();
        }

        template <SceneComponent T>
        bool HasComponent(const Scene& scene, NodeId node)
        {
            const ComponentStore<T>* store = scene.FindComponents<T>();
            return store && store->Contains(node);
        }

        float ChipSlotWidth()
        {
            return ImGui::GetFontSize() + 2.0f;
        }

    } // namespace

    struct SceneHierarchyWindow::TreeRow {
        NodeId id;
        bool isRoot { true };
        /// The user hid its model.
        bool isHidden { false };
        /// The node's name, its place in the scene when it has none.
        String name;
        /// What ImGui gets: the name and the id that keeps the row when the name changes.
        String label;
        /// By ComponentChips.
        std::array<bool, ComponentChips.size()> hasComponent {};
        /// The rows of its children, in the order of the scene.
        Vector<size_t> children;
        /// The search matches the node or a node below it. Every row without a search.
        bool matchesFilter { true };
    };

    SceneHierarchyWindow::SceneHierarchyWindow(Settings& settings, const SceneHierarchyDeps& deps, Path assets, Path models)
        : IImGuiWindow(settings)
        , m_deps(deps)
        , m_assets(std::move(assets))
        , m_models(std::move(models))
    {
    }

    void SceneHierarchyWindow::Render(render::FrameWriter&)
    {
        if (!m_visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(320.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Name, &m_visible)) {
            ImGui::End();
            return;
        }
        // The node is selected by now, so the history sees the change as its edit
        if (const NodeId toggled = std::exchange(m_visibilityToggle, NodeId {})) {
            if (ModelComponent* model = m_deps.scene.Components<ModelComponent>().Find(toggled))
                model->visible = !model->visible;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            HandleSceneEditShortcuts(m_deps.edits, m_deps.sceneFile);
            HandleKeys();
        }
        DrawToolbar();
        DrawTree(ReadTreeRows());
        // After EndChild the tree is the last item: it is where models are dropped
        AcceptDroppedModel();
        DrawModelDialog();
        ImGui::End();
    }

    void SceneHierarchyWindow::HandleKeys()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || !m_deps.scene.Find(m_deps.selection.node))
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
            StartRename(m_deps.selection.node);
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            RemoveSelected();
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            DuplicateSelected();
        // In Play the camera is the game's
        else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false) && !(m_deps.play.isPlaying && m_deps.play.isPlaying()))
            FrameNode(m_deps.camera, m_deps.scene, m_deps.models, m_deps.selection.node);
    }

    void SceneHierarchyWindow::DrawToolbar()
    {
        const float addWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addWidth - ImGui::GetStyle().ItemSpacing.x);
        const String hint = "Search " + String(std::to_string(m_deps.scene.NodeCount()).c_str()) + " nodes...";
        ImGui::InputTextWithHint("##TreeFilter", hint.c_str(), m_filter, sizeof(m_filter));
        ImGui::SameLine();
        if (ImGui::Button(AddNodeButton, ImVec2(addWidth, 0.0f)))
            ImGui::OpenPopup(AddNodePopup);
        ImGui::SetItemTooltip("Add a node");
        if (ImGui::BeginPopup(AddNodePopup)) {
            DrawAddNodeItems(NodeId {});
            ImGui::EndPopup();
        }
    }

    Vector<SceneHierarchyWindow::TreeRow> SceneHierarchyWindow::ReadTreeRows() const
    {
        const Scene& scene = m_deps.scene;
        const auto nodes = scene.Nodes();
        const auto* modelComponents = scene.FindComponents<ModelComponent>();
        Vector<TreeRow> rows(nodes.size());
        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const Scene::Node& node = nodes[nodeIndex];
            TreeRow& row = rows[nodeIndex];
            row.id = node.id;
            row.isRoot = !node.parent;
            row.name = NodeDisplayName(node, nodeIndex);
            row.label = row.name + "##node" + std::to_string(node.id.Raw()).c_str();
            const ModelComponent* model = modelComponents ? modelComponents->Find(node.id) : nullptr;
            row.isHidden = model && !model->visible;
            row.hasComponent = { model != nullptr, HasComponent<RigidBody>(scene, node.id), HasComponent<Collider>(scene, node.id),
                HasComponent<ScriptComponent>(scene, node.id) };
            const size_t parentIndex = scene.IndexOf(node.parent);
            if (parentIndex != Scene::NoNode)
                rows[parentIndex].children.push_back(nodeIndex);
        }
        // A parent comes before its children: from the end, the children of a row are done before it
        for (size_t nodeIndex = nodes.size(); nodeIndex-- > 0;) {
            TreeRow& row = rows[nodeIndex];
            row.matchesFilter = ContainsNoCase(nodes[nodeIndex].name, m_filter)
                || std::any_of(row.children.begin(), row.children.end(), [&](size_t child) { return rows[child].matchesFilter; });
        }
        return rows;
    }

    Vector<SceneHierarchyWindow::VisibleRow> SceneHierarchyWindow::ListVisibleRows(const Vector<TreeRow>& rows) const
    {
        Vector<VisibleRow> visible;
        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            if (rows[rowIndex].isRoot)
                AddVisibleRow(rowIndex, 0, rows, visible);
        }
        return visible;
    }

    void SceneHierarchyWindow::AddVisibleRow(size_t rowIndex, int depth, const Vector<TreeRow>& rows, Vector<VisibleRow>& visible) const
    {
        const TreeRow& row = rows[rowIndex];
        if (!row.matchesFilter)
            return;
        visible.push_back(VisibleRow { rowIndex, depth });
        // A search shows every match: rows the user closed stay closed for after it
        if (!IsSearching() && !m_openRows.contains(row.id.Raw()))
            return;
        for (const size_t child : row.children)
            AddVisibleRow(child, depth + 1, rows, visible);
    }

    void SceneHierarchyWindow::DrawTree(const Vector<TreeRow>& rows)
    {
        ImGui::BeginChild("TreeScrollArea", ImVec2(0.0f, 0.0f), false);
        const Vector<VisibleRow> visible = ListVisibleRows(rows);
        if (ImGui::BeginTable("TreeRows", 2, ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Components", ImGuiTableColumnFlags_WidthFixed, ChipSlotWidth() * RowIconCount);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    DrawRow(visible[static_cast<size_t>(index)], rows);
            }
            ImGui::EndTable();
        }
        DrawEmptySpace();
        ImGui::EndChild();
    }

    void SceneHierarchyWindow::DrawRow(const VisibleRow& visible, const Vector<TreeRow>& rows)
    {
        const TreeRow& row = rows[visible.row];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (m_deps.selection.node == row.id)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));

        const float indent = static_cast<float>(visible.depth) * ImGui::GetTreeNodeToLabelSpacing();
        if (visible.depth > 0)
            ImGui::Indent(indent);
        if (m_renaming == row.id)
            DrawRenameField(row);
        else
            DrawRowName(row);
        if (visible.depth > 0)
            ImGui::Unindent(indent);

        ImGui::TableNextColumn();
        DrawRowIcons(row);
    }

    void SceneHierarchyWindow::DrawRowIcons(const TreeRow& row)
    {
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float slot = ChipSlotWidth();
        const float height = ImGui::GetTextLineHeight();
        const bool clicked = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        /// Draws the icon in its slot; true while the mouse is over it.
        auto drawIcon = [&](size_t index, const char* icon, ImGuiCol color, const char* tooltip) {
            const ImVec2 min(cellMin.x + slot * static_cast<float>(index), cellMin.y);
            const ImVec2 max(min.x + slot, min.y + height);
            drawList->AddText(ImVec2(min.x + (slot - ImGui::CalcTextSize(icon).x) * 0.5f, min.y), ImGui::GetColorU32(color), icon);
            const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
            if (hovered)
                ImGui::SetTooltip("%s", tooltip);
            return hovered;
        };
        // What a node without a model shows is not its own to hide
        if (row.hasComponent[ModelChip]) {
            const bool overEye = row.isHidden ? drawIcon(0, HiddenIcon, ImGuiCol_TextDisabled, "Hidden: click to show")
                                              : drawIcon(0, ShownIcon, ImGuiCol_Text, "Shown: click to hide");
            if (overEye && clicked)
                m_visibilityToggle = row.id;
        }
        for (size_t chip = 0; chip < ComponentChips.size(); ++chip) {
            if (row.hasComponent[chip])
                (void)drawIcon(chip + 1, ComponentChips[chip].icon, ImGuiCol_TextDisabled, ComponentChips[chip].name);
        }
        const ImVec2 cellMax(cellMin.x + slot * static_cast<float>(RowIconCount), cellMin.y + height);
        // The icons are a part of the row
        if (clicked && ImGui::IsMouseHoveringRect(cellMin, cellMax))
            Select(row.id);
        ImGui::Dummy(ImVec2(cellMax.x - cellMin.x, height));
    }

    void SceneHierarchyWindow::DrawRowName(const TreeRow& row)
    {
        const bool hasChildren = !row.children.empty();
        const bool canClose = hasChildren && !IsSearching();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (!canClose)
            flags |= ImGuiTreeNodeFlags_Leaf;
        else
            ImGui::SetNextItemOpen(m_openRows.contains(row.id.Raw()));

        if (row.isHidden)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool isOpen = ImGui::TreeNodeEx(row.label.c_str(), flags);
        if (row.isHidden)
            ImGui::PopStyleColor();
        if (canClose) {
            if (isOpen)
                m_openRows.insert(row.id.Raw());
            else
                m_openRows.erase(row.id.Raw());
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            Select(row.id);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload(NodeDragPayload, &row.id, sizeof(NodeId));
            ImGui::Text("Move %s", row.name.c_str());
            ImGui::EndDragDropSource();
        }
        AcceptNodeDrop(row.id);
        DrawRowMenu(row);
    }

    void SceneHierarchyWindow::DrawRenameField(const TreeRow& row)
    {
        if (!m_renameFocused)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        (void)ui::InputString("##Rename", m_renameText, ImGuiInputTextFlags_AutoSelectAll);
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool left = m_renameFocused && !ImGui::IsItemActive();
        m_renameFocused = m_renameFocused || ImGui::IsItemActive();
        if (cancelled || left || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (!cancelled)
                (void)m_deps.scene.SetName(row.id, m_renameText);
            m_renaming = NodeId {};
        }
    }

    void SceneHierarchyWindow::AcceptNodeDrop(NodeId newParent)
    {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NodeDragPayload)) {
            NodeId dragged;
            std::memcpy(&dragged, payload->Data, sizeof(NodeId));
            if (dragged != newParent) {
                const auto result = m_deps.scene.SetParent(dragged, newParent);
                if (result == Scene::SetParentResult::Cycle) {
                    m_deps.document.Report(StatusKind::Failure, "Cannot reparent: the target is a child of the dragged node.");
                } else if (result == Scene::SetParentResult::SingularParent) {
                    m_deps.document.Report(StatusKind::Failure, "Cannot reparent: singular parent transform.");
                } else {
                    m_openRows.insert(newParent.Raw());
                    m_deps.document.Report(StatusKind::Info, "Node reparented.");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    void SceneHierarchyWindow::DrawRowMenu(const TreeRow& row)
    {
        if (!ImGui::BeginPopupContextItem())
            return;
        Select(row.id);
        ImGui::TextDisabled("%s", row.name.c_str());
        ImGui::Separator();
        if (ImGui::BeginMenu(AddChildItem)) {
            DrawAddNodeItems(row.id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(RenameItem, "F2"))
            StartRename(row.id);
        if (ImGui::MenuItem(DuplicateItem, "Ctrl+D"))
            DuplicateSelected();
        if (!row.isRoot && ImGui::MenuItem(UnparentItem))
            (void)m_deps.scene.SetParent(row.id, NodeId {});
        ImGui::Separator();
        if (ImGui::MenuItem(DeleteItem, "Del"))
            RemoveSelected();
        ImGui::EndPopup();
    }

    void SceneHierarchyWindow::DrawEmptySpace()
    {
        Scene& scene = m_deps.scene;
        // Below the rows: a click selects nothing, a dragged node becomes a root. Some of it stays when the rows
        // fill the tree
        const ImVec2 emptySpace = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(emptySpace.x, (std::max)(emptySpace.y, ImGui::GetFrameHeight())));
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
            m_deps.selection.node = NodeId {};
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NodeDragPayload)) {
                NodeId draggedId;
                std::memcpy(&draggedId, payload->Data, sizeof(NodeId));
                const Scene::Node* dragged = scene.Find(draggedId);
                // A root stays as it is: no step in the history
                if (dragged && dragged->parent) {
                    (void)scene.SetParent(draggedId, NodeId {});
                    m_deps.document.Report(StatusKind::Info, "Node moved to scene root.");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextWindow("TreeEmptyAreaContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            DrawAddNodeItems(NodeId {});
            ImGui::EndPopup();
        }
    }

    void SceneHierarchyWindow::DrawAddNodeItems(NodeId parent)
    {
        Scene& scene = m_deps.scene;
        ModelCatalog& models = m_deps.models;
        NodeId added;
        if (ImGui::MenuItem("Empty Node"))
            added = scene.AddNode("Empty Node", math::Matrix4x4::Identity(), parent);
        if (ImGui::BeginMenu("3D Primitives")) {
            for (const ModelPrimitive primitive : ModelPrimitives) {
                if (ImGui::MenuItem(ModelPrimitiveName(primitive)))
                    added = AddPrimitiveNode(scene, models, primitive, parent);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Model from Disk...")) {
            m_modelDialogParent = parent;
            m_modelDialog.Open("Select Model File", ModelFileExtensions, m_models);
        }
        if (models.Size() > 0 && ImGui::BeginMenu("From Loaded Model")) {
            models.ForEach([&](ModelHandler handle, const Model& model) {
                const String name = model.name.empty() ? String("Model") : model.name;
                ImGui::PushID(static_cast<int>(handle.Raw()));
                if (ImGui::MenuItem(name.c_str()))
                    added = AddModelNode(scene, handle, name, parent);
                ImGui::PopID();
            });
            ImGui::EndMenu();
        }
        if (added) {
            if (parent)
                m_openRows.insert(parent.Raw());
            Select(added);
        }
    }

    void SceneHierarchyWindow::DrawModelDialog()
    {
        const auto file = m_modelDialog.Draw();
        if (!file)
            return;
        // A .model of the project stays its file; anything else is imported into the project
        const auto model = ModelFromFile(m_deps.models, *file, m_assets);
        if (!model) {
            m_deps.document.Report(StatusKind::Failure, "Failed to load model: " + model.error().message);
            return;
        }
        const String name = m_deps.models.Get(*model)->name;
        if (m_modelDialogParent)
            m_openRows.insert(m_modelDialogParent.Raw());
        Select(AddModelNode(m_deps.scene, *model, name, m_modelDialogParent));
        m_deps.document.Report(StatusKind::Info, "Added node \"" + name + "\".");
    }

    void SceneHierarchyWindow::AcceptDroppedModel()
    {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelFilePayload)) {
            const Path file(StringView(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize)));
            // A model of the project stays its file, anything else is imported
            const auto model = ModelFromFile(m_deps.models, file, m_assets);
            if (model) {
                const String name = m_deps.models.Get(*model)->name;
                Select(AddModelNode(m_deps.scene, *model, name, NodeId {}));
                m_deps.document.Report(StatusKind::Info, "Added node \"" + name + "\".");
            } else {
                m_deps.document.Report(StatusKind::Failure, "Failed to add the model: " + model.error().message);
            }
        }
        ImGui::EndDragDropTarget();
    }

    void SceneHierarchyWindow::Select(NodeId node)
    {
        if (m_renaming != node)
            m_renaming = NodeId {};
        m_deps.selection.node = node;
    }

    void SceneHierarchyWindow::StartRename(NodeId node)
    {
        const Scene::Node* found = m_deps.scene.Find(node);
        if (!found)
            return;
        m_renaming = node;
        m_renameText = found->name;
        m_renameFocused = false;
    }

    void SceneHierarchyWindow::DuplicateSelected()
    {
        m_deps.selection.node = DuplicateNode(m_deps.scene, m_deps.selection.node);
        m_deps.document.Report(StatusKind::Info, "Duplicated node.");
    }

    void SceneHierarchyWindow::RemoveSelected()
    {
        m_deps.selection.node = RemoveNodeFromPlace(m_deps.scene, m_deps.selection.node);
    }

} // namespace elm
