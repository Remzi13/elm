#pragma once

#include "graphics/ModelManager.hpp"

#include "Scene/Scene.hpp"

namespace elm {

/// A scene and the models it uses as they were, to come back to: Play mode takes one, Stop restores it.
class SceneSnapshot {
public:
    /// models may be null (tools, tests): then only the scene is kept.
    [[nodiscard]] static SceneSnapshot Take(const Scene& scene, const graphics::ModelManager* models);

    /// Puts the scene back with its node ids, so references held from before the snapshot work again.
    /// Models removed since are inserted again (with new handles the nodes are pointed at); models
    /// added since that no node of the restored scene uses are removed.
    void Restore(Scene& scene, graphics::ModelManager* models) &&;

private:
    SceneSnapshot() = default;

    struct SavedModel {
        graphics::ModelHandler handle;
        graphics::Model model;
        Path file;
    };

    Scene m_scene;
    Vector<SavedModel> m_models;
};

} // namespace elm
