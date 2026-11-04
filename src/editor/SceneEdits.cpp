#include "editor/SceneEdits.hpp"

#include <utility>

namespace elm {

    namespace {

        template <SceneComponent T>
        std::optional<T> FindComponent(const Scene& scene, NodeId node)
        {
            const ComponentStore<T>* store = scene.FindComponents<T>();
            const T* component = store ? store->Find(node) : nullptr;
            return component ? std::optional<T>(*component) : std::nullopt;
        }

        template <SceneComponent T>
        void PutComponent(Scene& scene, NodeId node, const std::optional<T>& component)
        {
            if (component)
                scene.Components<T>().Emplace(node, *component);
            else if (scene.FindComponents<T>())
                scene.Components<T>().Remove(node);
        }

    } // namespace

    SceneEdits::SceneEdits(Scene& scene)
        : m_scene(scene)
    {
        Rebaseline();
    }

    void SceneEdits::SetFrozen(bool frozen)
    {
        if (m_frozen == frozen)
            return;
        m_frozen = frozen;
        if (!m_frozen)
            Rebaseline();
    }

    void SceneEdits::BeginFrame(NodeId watched)
    {
        m_watched = watched;
        m_watchedBefore = Capture(m_scene, watched);
    }

    void SceneEdits::EndFrame(bool gestureHeld)
    {
        if (m_frozen)
            return;
        if (m_scene.StructureRevision() != m_baselineRevision) {
            // The baseline already has every recorded edit: it is the scene before this frame's
            Push(StructureStep { std::move(m_baseline) });
            Rebaseline();
            return;
        }
        const auto after = Capture(m_scene, m_watched);
        if (after && m_watchedBefore && *after != *m_watchedBefore) {
            RecordNode(*after, gestureHeld);
            return;
        }
        m_gestureOpen = m_gestureOpen && gestureHeld;
    }

    void SceneEdits::RecordNode(const NodeState& after, bool gestureHeld)
    {
        NodeStep* last = m_undo.empty() ? nullptr : std::get_if<NodeStep>(&m_undo.back());
        if (m_gestureOpen && last && last->node == m_watched) {
            // The step that was saved changes: nothing leads back to the saved scene
            if (m_savedDepth == m_undo.size())
                m_savedDepth.reset();
            last->after = after;
        } else {
            Push(NodeStep { m_watched, *m_watchedBefore, after });
        }
        Apply(m_baseline, m_watched, after);
        m_watchedBefore = after;
        m_gestureOpen = gestureHeld;
    }

    void SceneEdits::Push(Step step)
    {
        // The saved scene was among the steps undone, they go now
        if (m_savedDepth && *m_savedDepth > m_undo.size())
            m_savedDepth.reset();
        m_redo.clear();
        m_undo.push_back(std::move(step));
        if (m_undo.size() > MaxSteps) {
            m_undo.pop_front();
            if (m_savedDepth == size_t { 0 })
                m_savedDepth.reset();
            else if (m_savedDepth)
                --*m_savedDepth;
        }
    }

    bool SceneEdits::Undo()
    {
        if (!CanUndo())
            return false;
        Step step = std::move(m_undo.back());
        m_undo.pop_back();
        Reverse(step, true);
        m_redo.push_back(std::move(step));
        return true;
    }

    bool SceneEdits::Redo()
    {
        if (!CanRedo())
            return false;
        Step step = std::move(m_redo.back());
        m_redo.pop_back();
        Reverse(step, false);
        m_undo.push_back(std::move(step));
        return true;
    }

    void SceneEdits::Reverse(Step& step, bool undo)
    {
        if (auto* node = std::get_if<NodeStep>(&step)) {
            // A node removed by an edit outside the history stays removed
            if (m_scene.Find(node->node))
                Apply(m_scene, node->node, undo ? node->before : node->after);
        } else {
            auto& structure = std::get<StructureStep>(step);
            Scene current = m_scene.Clone();
            m_scene.RestoreFrom(std::move(structure.other));
            structure.other = std::move(current);
        }
        Rebaseline();
    }

    void SceneEdits::Clear()
    {
        m_undo.clear();
        m_redo.clear();
        m_savedDepth = 0;
        Rebaseline();
    }

    void SceneEdits::MarkSaved()
    {
        m_savedDepth = m_undo.size();
    }

    void SceneEdits::Rebaseline()
    {
        m_baseline = m_scene.Clone();
        m_baselineRevision = m_scene.StructureRevision();
        // An undo in the middle of a UI frame is not an edit of that frame
        m_watchedBefore = Capture(m_scene, m_watched);
        m_gestureOpen = false;
    }

    auto SceneEdits::Capture(const Scene& scene, NodeId node) -> std::optional<NodeState>
    {
        const Scene::Node* found = scene.Find(node);
        if (!found)
            return std::nullopt;
        return NodeState { found->name, found->localTransform, FindComponent<ScriptComponent>(scene, node),
            FindComponent<RigidBody>(scene, node), FindComponent<Collider>(scene, node), FindComponent<ModelComponent>(scene, node) };
    }

    void SceneEdits::Apply(Scene& scene, NodeId node, const NodeState& state)
    {
        (void)scene.SetName(node, state.name);
        (void)scene.SetLocalTransform(node, state.localTransform);
        PutComponent(scene, node, state.script);
        PutComponent(scene, node, state.rigidBody);
        PutComponent(scene, node, state.collider);
        PutComponent(scene, node, state.model);
    }

} // namespace elm
