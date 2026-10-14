#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include "script/ScriptSystem.hpp"

#include <array>

namespace elm::script {

    /// Components of the node selected in Scene Hierarchy:
    ///  - Script: the module and its props, an editor per type of the module's default (overridden
    ///    props are marked and can be reset); in Play mode the state of the instance;
    ///  - RigidBody and Collider fields;
    ///  - Add Component, the x of a section header removes the component.
    /// The bar on top switches Edit and Play mode; the window provides PlayControls for the viewport.
    class InspectorWindow final : public IImGuiWindow {
    public:
        InspectorWindow(Settings& settings, ScriptSystem& scripts);

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

        ScriptSystem& m_scripts;
        Vector<MessageBus::Subscription> m_querySubscriptions;
        /// For Add Component > Script, listed when the popup opens.
        Vector<String> m_modules;
        std::array<char, 128> m_moduleFilter { };
    };

} // namespace elm::script
