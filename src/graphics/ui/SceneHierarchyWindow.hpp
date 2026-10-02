#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/WindowMessages.hpp"

namespace elm {

class SceneHierarchyWindow final : public IImGuiWindow {
public:
    using GizmoOperation = elm::GizmoOperation;
    using GizmoSpace = elm::GizmoSpace;

    using IImGuiWindow::IImGuiWindow;

    [[nodiscard]] StringView GetName() const override { return "Scene Hierarchy"; }
    void Render(RenderSystem& renderSystem, Scene& scene, const Camera& camera, const FrameStats& stats) override;

protected:
    void OnAttach() override;

private:
    size_t m_selectedInstance { 0 };
    GizmoOperation m_gizmoOperation { GizmoOperation::Translate };
    bool m_useLocalSpace { false };
    Vector<MessageBus::Subscription> m_querySubscriptions;
};

} // namespace elm