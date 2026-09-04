#pragma once

#include "graphics/Camera.hpp"
#include "input/Input.hpp"

#include <array>
#include <vector>

namespace elm {

class CameraController : public IInputSubscriber {
public:
    explicit CameraController(Camera& camera) noexcept : m_camera(camera) {}

    void Update(float deltaTime);
    void Update(float deltaTime, const std::vector<elm::InputEvent>& events);

    // IInputSubscriber interface
    bool OnKeyEvent(const KeyEvent& event) override;
    bool OnMouseButtonEvent(const MouseButtonEvent& event) override;
    bool OnMouseMoveEvent(const MouseMoveEvent& event) override;

private:
    Camera& m_camera;
    std::array<bool, 512> m_keys{};
    bool m_rightMouseDown{false};
    bool m_hasMousePosition{false};
    double m_lastMouseX{0.0};
    double m_lastMouseY{0.0};
};

} // namespace elm
