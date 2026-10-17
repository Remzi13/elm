#pragma once

#include "math/Vector.hpp"

#include "graphics/ModelHandler.hpp"

#include "Scene/ComponentTypes.hpp"

namespace elm {

/// How a node is drawn: its model, tinted by color. Culling and the frame read it, the editor and scripts
/// write it. A node without the component has nothing to draw; the component may hold no model when a
/// script gave such a node a color or hid it (entity.color, entity.visible), as on a node with a model.
struct ModelComponent {
    static constexpr ComponentType Type = ComponentType::Model;

    /// Empty or removed from the ModelManager: nothing is drawn.
    graphics::ModelHandler model;
    math::Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
    /// Hidden by the user (editor, scripts). Culling keeps its own result and never changes it.
    bool visible { true };
};

} // namespace elm
