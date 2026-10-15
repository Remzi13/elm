#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include "Scene/ScriptComponent.hpp"

#include "script/EngineContext.hpp"

#include <array>

namespace elm::script {

    class EntityScripts;
    class HotReload;
    class ModulePaths;
    class ScriptDefaults;

    /// What the Inspector works with; the parts outlive the window.
    struct InspectorWindowDesc {
        EntityScripts& entityScripts;
        ScriptDefaults& defaults;
        const ModulePaths& paths;
        HotReload& hotReload;
        PlayRequests play;
        /// Shows the velocity of a body in Play mode, may be null.
        const PhysicsSystem* physics { nullptr };
    };

    /// Components of the node selected in Scene Hierarchy:
    ///  - Script: the module and its props, an editor per type of the module's default (overridden
    ///    props are marked and can be reset); in Play mode the state of the instance;
    ///  - RigidBody and Collider fields;
    ///  - Add Component, the x of a section header removes the component.
    /// The bar on top switches Edit and Play mode; the window provides PlayControls for the viewport.
    /// Owner: ImGuiSystem (made by ScriptSystem::CreateEditorWindows), thread: update (UI).
    class InspectorWindow final : public IImGuiWindow {
    public:
        InspectorWindow(Settings& settings, const InspectorWindowDesc& desc);

        [[nodiscard]] StringView GetName() const override { return "Inspector"; }
        void Render(ImGuiWindowContext& context) override;

    protected:
        void OnAttach() override;

    private:
        void DrawPlayBar();
        void DrawScript(Scene& scene, NodeId node);
        void DrawProps(Scene& scene, NodeId node, const String& module);
        void DrawRigidBody(Scene& scene, NodeId node);
        void DrawCollider(ImGuiWindowContext& context, NodeId node, size_t nodeIndex);
        void DrawAddComponent(ImGuiWindowContext& context, NodeId node, size_t nodeIndex);
        /// An editor for the value, of its type; true when the value changed.
        bool DrawValue(const char* label, ScriptValue& value, Scene& scene);
        void TogglePlay();
        [[nodiscard]] bool IsPlaying() const;

        EntityScripts& m_entityScripts;
        ScriptDefaults& m_defaults;
        const ModulePaths& m_paths;
        HotReload& m_hotReload;
        PlayRequests m_play;
        const PhysicsSystem* m_physics { nullptr };
        Vector<MessageBus::Subscription> m_querySubscriptions;
        /// For Add Component > Script, listed when the popup opens.
        Vector<String> m_modules;
        std::array<char, 128> m_moduleFilter { };
    };

} // namespace elm::script
