#include "editor/ui/SceneHierarchyWindow.hpp"

#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ui/EditorWidgets.hpp"
#include "editor/ui/SceneEditShortcuts.hpp"

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

        /// A column of the tree: the letter of a component, for each node that has one.
        struct ComponentChip {
            const char* letter;
            const char* name;
        };
        constexpr std::array<ComponentChip, 4> ComponentChips { {
            { "M", "Model" },
            { "R", "RigidBody" },
            { "C", "Collider" },
            { "S", "Script" },
        } };

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

        ImVec4 StatusColor(StatusKind kind)
        {
            switch (kind) {
            case StatusKind::Success: return ImVec4(0.4f, 0.85f, 0.5f, 1.0f);
            case StatusKind::Failure: return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            case StatusKind::Info: break;
            }
            return ImVec4(0.7f, 0.7f, 0.75f, 1.0f);
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
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            HandleSceneEditShortcuts(m_deps.edits, m_deps.sceneFile);
            HandleKeys();
        }
        DrawToolbar();
        DrawTree(ReadTreeRows());
        // After EndChild the tree is the last item: it is where models are dropped
        AcceptDroppedModel();
        DrawStatusLine();
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
    }

    void SceneHierarchyWindow::DrawToolbar()
    {
        const float addWidth = ImGui::CalcTextSize("+ Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##TreeFilter", "Search nodes...", m_filter, sizeof(m_filter));
        ImGui::SameLine();
        if (ImGui::Button("+ Add"))
            ImGui::OpenPopup(AddNodePopup);
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
        // The status line and the node count
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        ImGui::BeginChild("TreeScrollArea", ImVec2(0.0f, -footerHeight), false);
        const Vector<VisibleRow> visible = ListVisibleRows(rows);
        if (ImGui::BeginTable("TreeRows", 2, ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Components", ImGuiTableColumnFlags_WidthFixed, ChipSlotWidth() * ComponentChips.size());
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
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float slot = ChipSlotWidth();
        const float height = ImGui::GetTextLineHeight();
        for (size_t chip = 0; chip < ComponentChips.size(); ++chip) {
            if (!row.hasComponent[chip])
                continue;
            const ImVec2 min(cellMin.x + slot * static_cast<float>(chip), cellMin.y);
            const ImVec2 max(min.x + slot - 2.0f, min.y + height);
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
            const float textX = min.x + (max.x - min.x - ImGui::CalcTextSize(ComponentChips[chip].letter).x) * 0.5f;
            drawList->AddText(ImVec2(textX, min.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), ComponentChips[chip].letter);
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max))
                ImGui::SetTooltip("%s", ComponentChips[chip].name);
        }
        const ImVec2 cellMax(cellMin.x + slot * static_cast<float>(ComponentChips.size()), cellMin.y + height);
        // The letters are a part of the row
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(cellMin, cellMax))
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
        if (ImGui::BeginMenu("Add Child")) {
            DrawAddNodeItems(row.id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Rename", "F2"))
            StartRename(row.id);
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            DuplicateSelected();
        if (!row.isRoot && ImGui::MenuItem("Unparent (Move to Root)"))
            (void)m_deps.scene.SetParent(row.id, NodeId {});
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Node", "Del"))
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

    void SceneHierarchyWindow::DrawStatusLine()
    {
        const SceneStatus& status = m_deps.document.Status();
        if (status.text.empty()) {
            ImGui::NewLine();
        } else {
            ImGui::TextColored(StatusColor(status.kind), "%s", status.text.c_str());
            ImGui::SetItemTooltip("%s", status.text.c_str());
        }
        ImGui::TextDisabled("%zu nodes", m_deps.scene.NodeCount());
        constexpr const char* MenuHint = "Right click: menu";
        const float hintWidth = ImGui::CalcTextSize(MenuHint).x;
        if (ImGui::GetContentRegionAvail().x > hintWidth * 2.0f) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - hintWidth);
            ImGui::TextDisabled("%s", MenuHint);
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
