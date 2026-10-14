#include "script/ui/InspectorWindow.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"
#include "graphics/ModelManager.hpp"
#include "graphics/ui/WindowMessages.hpp"
#include "physics/PhysicsSystem.hpp"
#include "script/bindings/Components.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <variant>

namespace elm::script {

    namespace {

        const ImVec4 ErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
        const ImVec4 InfoColor { 0.60f, 0.63f, 0.68f, 1.0f };
        const ImVec4 OkColor { 0.40f, 0.85f, 0.50f, 1.0f };
        const ImVec4 PlayColor { 1.0f, 0.62f, 0.25f, 1.0f };
        /// Props whose value is set on this entity, not taken from the script.
        const ImVec4 OverrideColor { 0.55f, 0.75f, 1.0f, 1.0f };

        int ResizeBuffer(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
                return 0;
            auto& buffer = *static_cast<Vector<char>*>(data->UserData);
            buffer.resize(static_cast<size_t>(data->BufTextLen) + 1);
            data->Buf = buffer.data();
            return 0;
        }

        /// InputText over a String; true when it changed (with EnterReturnsTrue: when Enter was pressed).
        bool InputString(const char* label, String& text, ImGuiInputTextFlags flags = 0)
        {
            Vector<char> buffer(text.begin(), text.end());
            buffer.push_back('\0');
            if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags | ImGuiInputTextFlags_CallbackResize, ResizeBuffer, &buffer))
                return false;
            text.assign(buffer.data(), std::strlen(buffer.data()));
            return true;
        }

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

        ScriptValue* FindOverride(ScriptComponent& script, StringView name)
        {
            for (auto& [key, value] : script.props) {
                if (key == name)
                    return &value;
            }
            return nullptr;
        }

        /// Props stay sorted by name, as they come from Lua and from scene files.
        void SetOverride(ScriptComponent& script, const String& name, ScriptValue value)
        {
            const auto position = std::lower_bound(script.props.begin(), script.props.end(), name,
                [](const auto& prop, const String& key) { return prop.first < key; });
            if (position != script.props.end() && position->first == name)
                position->second = std::move(value);
            else
                script.props.emplace(position, name, std::move(value));
        }

        void RemoveOverride(ScriptComponent& script, StringView name)
        {
            std::erase_if(script.props, [&](const auto& prop) { return prop.first == name; });
        }

        /// A combo of the scene's nodes; true when another one was picked.
        bool PickEntity(const char* label, NodeId& id, Scene& scene)
        {
            const Scene::Node* target = scene.Find(id);
            const String preview = target ? (target->name.empty() ? String("Unnamed") : target->name) : String(id ? "(destroyed)" : "None");
            bool changed = false;
            if (!ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge))
                return false;
            if (ImGui::Selectable("None", !target)) {
                id = { };
                changed = true;
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(scene.nodes.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto index = static_cast<size_t>(row);
                    const String& name = scene.nodes[index].name;
                    ImGui::PushID(row);
                    if (ImGui::Selectable(name.empty() ? "Unnamed" : name.c_str(), target == &scene.nodes[index])) {
                        id = scene.IdOf(index);
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
            return changed;
        }

        /// A filtered list of the modules; the picked one, or empty.
        String PickModule(const Vector<String>& modules, std::array<char, 128>& filter)
        {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##filter", "filter", filter.data(), filter.size());
            if (modules.empty())
                ImGui::TextDisabled("No .lua files in the script folder");
            for (const auto& module : modules) {
                if (ContainsNoCase(module, filter.data()) && ImGui::MenuItem(module.c_str()))
                    return module;
            }
            return { };
        }

        /// The x of a section header asked to remove the component.
        bool ComponentHeader(const char* name, bool& open)
        {
            bool keep = true;
            open = ImGui::CollapsingHeader(name, &keep, ImGuiTreeNodeFlags_DefaultOpen);
            return !keep;
        }

    } // namespace

    InspectorWindow::InspectorWindow(Settings& settings, PlayModeController& playMode, ScriptSystem& scripts)
        : IImGuiWindow(settings)
        , m_playMode(playMode)
        , m_scripts(scripts)
    {
    }

    void InspectorWindow::OnAttach()
    {
        m_querySubscriptions.push_back(RegisterQuery<PlayControls>([this] {
            return PlayControls { m_playMode.IsPlaying(), [this] { TogglePlay(); } };
        }));
    }

    void InspectorWindow::TogglePlay()
    {
        if (m_playMode.IsPlaying())
            m_playMode.RequestStop();
        else
            m_playMode.RequestPlay();
    }

    void InspectorWindow::Render(ImGuiWindowContext& context)
    {
        if (!m_visible)
            return;
        ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }

        DrawPlayBar();
        ImGui::Separator();

        Scene& scene = context.scene;
        const auto selected = Request<SelectedSceneInstance>();
        if (!selected || selected->index >= scene.nodes.size()) {
            ImGui::TextDisabled("Select a node in Scene Hierarchy");
            ImGui::End();
            return;
        }
        const size_t nodeIndex = selected->index;
        const NodeId node = scene.IdOf(nodeIndex);
        const String& name = scene.nodes[nodeIndex].name;
        ImGui::TextUnformatted(name.empty() ? "Unnamed node" : name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("node %zu", nodeIndex);

        ImGui::PushID(static_cast<int>(node.Raw()));
        if (const auto* scripts = scene.FindComponents<ScriptComponent>(); scripts && scripts->Contains(node))
            DrawScript(scene, node);
        if (const auto* bodies = scene.FindComponents<RigidBody>(); bodies && bodies->Contains(node))
            DrawRigidBody(scene, node);
        if (const auto* colliders = scene.FindComponents<Collider>(); colliders && colliders->Contains(node))
            DrawCollider(context, node, nodeIndex);
        DrawAddComponent(context, node, nodeIndex);
        ImGui::PopID();
        ImGui::End();
    }

    void InspectorWindow::DrawPlayBar()
    {
        const bool playing = m_playMode.IsPlaying();
        if (ImGui::Button(playing ? "Stop" : "Play"))
            TogglePlay();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(playing ? "Back to Edit mode: the scene returns to how Play found it"
                                      : "Run physics and entity scripts; Stop brings the scene back");
        ImGui::SameLine();
        if (playing)
            ImGui::TextColored(PlayColor, "Playing: changes are undone on Stop");
        else
            ImGui::TextDisabled("Edit mode: physics and entity scripts stand still");
    }

    // ── Script ──────────────────────────────────────────────────────────────────

    void InspectorWindow::DrawScript(Scene& scene, NodeId node)
    {
        bool open = false;
        if (ComponentHeader("Script", open)) {
            // The instance ends first, as with entity:remove("Script")
            m_scripts.DestroyEntityScripts({ &node, 1 });
            scene.Components<ScriptComponent>().Remove(node);
            return;
        }
        if (!open)
            return;

        String module = scene.Components<ScriptComponent>().Find(node)->module;
        if (InputString("Module", module, ImGuiInputTextFlags_EnterReturnsTrue) && !module.empty())
            scene.Components<ScriptComponent>().Find(node)->module = module;
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) {
            m_modules = m_scripts.ListModules();
            m_moduleFilter.fill('\0');
            ImGui::OpenPopup("PickModule");
        }
        if (ImGui::BeginPopup("PickModule")) {
            if (String picked = PickModule(m_modules, m_moduleFilter); !picked.empty()) {
                scene.Components<ScriptComponent>().Find(node)->module = picked;
                module = std::move(picked);
            }
            ImGui::EndPopup();
        }

        if (!m_playMode.IsPlaying()) {
            ImGui::TextDisabled("Starts on Play");
        } else if (const auto* instance = m_scripts.FindEntityScript(node); instance && instance->faulted) {
            const String error = instance->error;
            const String instanceModule = instance->module;
            ImGui::TextColored(ErrorColor, "Stopped by an error until a module reloads");
            ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton("Reload module"))
                (void)m_scripts.ReloadModule(instanceModule);
        } else if (instance && instance->started) {
            ImGui::TextColored(OkColor, "Running");
        } else {
            ImGui::TextColored(InfoColor, "Starting");
        }

        ImGui::Spacing();
        DrawProps(scene, node, module);
    }

    void InspectorWindow::DrawProps(Scene& scene, NodeId node, const String& module)
    {
        const auto& defaults = m_scripts.ScriptDefaults(module);
        if (!defaults) {
            ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
            ImGui::TextWrapped("%s", defaults.error().message.c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton("Try again"))
                m_scripts.ForgetScriptDefaults();
            return;
        }
        // Loading the module ran its code, which may have changed the scene: the component is looked up again
        const ScriptSystem::ScriptProps props = *defaults;
        if (props.empty())
            ImGui::TextDisabled("The script has no props");

        for (const auto& [name, fallback] : props) {
            ScriptComponent* script = scene.Components<ScriptComponent>().Find(node);
            if (!script)
                return;
            const ScriptValue* overridden = FindOverride(*script, name);
            ScriptValue value = overridden ? *overridden : fallback;

            ImGui::PushID(name.c_str());
            if (overridden)
                ImGui::PushStyleColor(ImGuiCol_Text, OverrideColor);
            const bool changed = DrawValue(name.c_str(), value, scene);
            if (overridden)
                ImGui::PopStyleColor();
            if (changed) {
                SetOverride(*script, name, value);
                m_scripts.SetEntityScriptField(node, name, value);
            }
            if (overridden) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset")) {
                    RemoveOverride(*script, name);
                    m_scripts.SetEntityScriptField(node, name, fallback);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Take the value of the script again");
            }
            ImGui::PopID();
        }

        // Values for props the script does not have (any more): they would fail the instance
        ScriptComponent* script = scene.Components<ScriptComponent>().Find(node);
        for (size_t index = 0; script && index < script->props.size();) {
            const String name = script->props[index].first;
            const bool known = std::any_of(props.begin(), props.end(), [&](const auto& prop) { return prop.first == name; });
            if (known) {
                ++index;
                continue;
            }
            ImGui::PushID(name.c_str());
            ImGui::TextColored(ErrorColor, "%s: not a prop of the script", name.c_str());
            ImGui::SameLine();
            const bool remove = ImGui::SmallButton("Remove");
            ImGui::PopID();
            if (remove)
                script->props.erase(script->props.begin() + static_cast<std::ptrdiff_t>(index));
            else
                ++index;
        }
    }

    bool InspectorWindow::DrawValue(const char* label, ScriptValue& value, Scene& scene)
    {
        return std::visit([&]<typename V>(V& item) -> bool {
            if constexpr (std::is_same_v<V, bool>)
                return ImGui::Checkbox(label, &item);
            else if constexpr (std::is_same_v<V, int64_t>)
                return ImGui::DragScalar(label, ImGuiDataType_S64, &item, 0.2f);
            else if constexpr (std::is_same_v<V, double>)
                return ImGui::DragScalar(label, ImGuiDataType_Double, &item, 0.01f, nullptr, nullptr, "%.3f");
            else if constexpr (std::is_same_v<V, String>)
                return InputString(label, item);
            else if constexpr (std::is_same_v<V, math::Vector3>)
                return ImGui::DragFloat3(label, &item.x, 0.05f);
            else if constexpr (std::is_same_v<V, math::Vector4>)
                return ImGui::ColorEdit4(label, &item.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            else
                return PickEntity(label, item, scene);
        }, value);
    }

    // ── Physics ─────────────────────────────────────────────────────────────────

    void InspectorWindow::DrawRigidBody(Scene& scene, NodeId node)
    {
        bool open = false;
        if (ComponentHeader("RigidBody", open)) {
            scene.Components<RigidBody>().Remove(node);
            return;
        }
        if (!open)
            return;

        RigidBody& body = *scene.Components<RigidBody>().Find(node);
        const char* types[] = { "Static", "Kinematic", "Dynamic" };
        int type = static_cast<int>(body.type);
        if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types)))
            body.type = static_cast<BodyType>(type);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Static stands, Kinematic follows its node and pushes, Dynamic is simulated");

        ImGui::BeginDisabled(body.type != BodyType::Dynamic);
        ImGui::DragFloat("Mass", &body.mass, 0.05f, 0.001f, 1.0e6f, "%.3f kg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Gravity scale", &body.gravityScale, 0.01f, -100.0f, 100.0f, "%.2f");
        ImGui::SliderFloat("Linear damping", &body.linearDamping, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Angular damping", &body.angularDamping, 0.0f, 1.0f, "%.3f");
        ImGui::EndDisabled();
        ImGui::DragFloat("Friction", &body.friction, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Restitution", &body.restitution, 0.0f, 1.0f, "%.2f");

        const auto* physics = m_scripts.Engine().physics;
        if (m_playMode.IsPlaying() && physics) {
            if (const auto velocity = physics->GetLinearVelocity(node))
                ImGui::TextDisabled("Velocity %.2f, %.2f, %.2f", velocity->x, velocity->y, velocity->z);
            else
                ImGui::TextDisabled("No body: it needs a Collider");
        }
    }

    void InspectorWindow::DrawCollider(ImGuiWindowContext& context, NodeId node, size_t nodeIndex)
    {
        Scene& scene = context.scene;
        bool open = false;
        if (ComponentHeader("Collider", open)) {
            scene.Components<Collider>().Remove(node);
            return;
        }
        if (!open)
            return;

        Collider& collider = *scene.Components<Collider>().Find(node);
        const char* shapes[] = { "Box", "Sphere", "Capsule" };
        int shape = static_cast<int>(collider.shape);
        if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes)))
            collider.shape = static_cast<ColliderShape>(shape);
        switch (collider.shape) {
        case ColliderShape::Box:
            ImGui::DragFloat3("Half extents", &collider.halfExtents.x, 0.01f, 0.0f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            break;
        case ColliderShape::Sphere:
            ImGui::DragFloat("Radius", &collider.radius, 0.01f, 0.0f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            break;
        case ColliderShape::Capsule:
            ImGui::DragFloat("Radius", &collider.radius, 0.01f, 0.0f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Half height", &collider.halfHeight, 0.01f, 0.0f, 1.0e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            break;
        }
        ImGui::DragFloat3("Offset", &collider.offset.x, 0.01f);
        ImGui::Checkbox("Trigger", &collider.trigger);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reports the bodies that enter it (on_trigger_enter) and pushes nothing");
        if (ImGui::Button("Fit to model")) {
            Collider fitted = bindings::FitColliderToModel(context.models.Get(scene.nodes[nodeIndex].model));
            fitted.shape = collider.shape;
            fitted.trigger = collider.trigger;
            collider = fitted;
        }
        if (const auto* bodies = scene.FindComponents<RigidBody>(); !bodies || !bodies->Contains(node))
            ImGui::TextDisabled("No RigidBody: a static body");
    }

    // ── Add Component ───────────────────────────────────────────────────────────

    void InspectorWindow::DrawAddComponent(ImGuiWindowContext& context, NodeId node, size_t nodeIndex)
    {
        Scene& scene = context.scene;
        ImGui::Separator();
        if (ImGui::Button("Add Component")) {
            m_modules = m_scripts.ListModules();
            m_moduleFilter.fill('\0');
            ImGui::OpenPopup("AddComponent");
        }
        if (!ImGui::BeginPopup("AddComponent"))
            return;

        auto has = [&](const IComponentStore* store) { return store && store->Contains(node); };
        if (ImGui::MenuItem("RigidBody", nullptr, false, !has(scene.FindComponents<RigidBody>())))
            scene.Components<RigidBody>().Emplace(node);
        if (ImGui::MenuItem("Collider", nullptr, false, !has(scene.FindComponents<Collider>())))
            scene.Components<Collider>().Emplace(node, bindings::FitColliderToModel(context.models.Get(scene.nodes[nodeIndex].model)));
        if (has(scene.FindComponents<ScriptComponent>())) {
            ImGui::MenuItem("Script", nullptr, false, false);
        } else if (ImGui::BeginMenu("Script")) {
            // In Play mode the instance starts on the next update
            if (const String module = PickModule(m_modules, m_moduleFilter); !module.empty())
                scene.Components<ScriptComponent>().Emplace(node, ScriptComponent { module, { } });
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

} // namespace elm::script
