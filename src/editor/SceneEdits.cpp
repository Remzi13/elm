#include "editor/SceneEdits.hpp"

#include <algorithm>
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

    void SceneEdits::BeginFrame(std::span<const NodeId> watched)
    {
        m_watched.clear();
        for (const NodeId node : watched)
            m_watched.push_back(WatchedNode { node, {} });
        CaptureWatched();
    }

    void SceneEdits::CaptureWatched()
    {
        std::erase_if(m_watched, [&](WatchedNode& watched) {
            auto state = Capture(m_scene, watched.node);
            if (state)
                watched.before = std::move(*state);
            return !state;
        });
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
        Vector<NodeChange> changes;
        for (const WatchedNode& watched : m_watched) {
            auto after = Capture(m_scene, watched.node);
            if (after && *after != watched.before)
                changes.push_back(NodeChange { watched.node, watched.before, std::move(*after) });
        }
        if (!changes.empty()) {
            RecordNodes(std::move(changes), gestureHeld);
            return;
        }
        m_gestureOpen = m_gestureOpen && gestureHeld;
    }

    void SceneEdits::RecordNodes(Vector<NodeChange> changes, bool gestureHeld)
    {
        for (const NodeChange& change : changes)
            Apply(m_baseline, change.node, change.after);
        CaptureWatched();
        NodeStep* last = m_undo.empty() ? nullptr : std::get_if<NodeStep>(&m_undo.back());
        m_gestureOpen = m_gestureOpen && last;
        if (!m_gestureOpen) {
            Push(NodeStep { std::move(changes) });
            m_gestureOpen = gestureHeld;
            return;
        }
        // The step that was saved changes: nothing leads back to the saved scene
        if (m_savedDepth == m_undo.size())
            m_savedDepth.reset();
        // The held widget goes on: a node it changed before keeps its first before
        for (NodeChange& change : changes) {
            const auto earlier = std::find_if(last->changes.begin(), last->changes.end(), [&](const NodeChange& recorded) { return recorded.node == change.node; });
            if (earlier != last->changes.end())
                earlier->after = std::move(change.after);
            else
                last->changes.push_back(std::move(change));
        }
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
        if (auto* nodes = std::get_if<NodeStep>(&step)) {
            for (const NodeChange& change : nodes->changes) {
                // A node removed by an edit outside the history stays removed
                if (m_scene.Find(change.node))
                    Apply(m_scene, change.node, undo ? change.before : change.after);
            }
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
        CaptureWatched();
        m_gestureOpen = false;
    }

    auto SceneEdits::Capture(const Scene& scene, NodeId node) -> std::optional<NodeState>
    {
        const Scene::Node* found = scene.Find(node);
        if (!found)
            return std::nullopt;
        return NodeState { found->name, found->localTransform, FindComponent<ScriptComponent>(scene, node),
            FindComponent<RigidBody>(scene, node), FindComponent<Collider>(scene, node), FindComponent<ModelComponent>(scene, node),
            FindComponent<CameraComponent>(scene, node), FindComponent<LightComponent>(scene, node) };
    }

    void SceneEdits::Apply(Scene& scene, NodeId node, const NodeState& state)
    {
        (void)scene.SetName(node, state.name);
        (void)scene.SetLocalTransform(node, state.localTransform);
        PutComponent(scene, node, state.script);
        PutComponent(scene, node, state.rigidBody);
        PutComponent(scene, node, state.collider);
        PutComponent(scene, node, state.model);
        PutComponent(scene, node, state.camera);
        PutComponent(scene, node, state.light);
    }

} // namespace elm
