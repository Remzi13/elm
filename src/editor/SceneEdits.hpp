#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/NodeId.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ScriptComponent.hpp"

#include <optional>
#include <variant>

namespace elm {

	/// The user's edits of the scene with undo and redo (docs/core/Системы.md#Правки сцены из редактора). The
	/// windows edit the scene themselves; the history finds what changed around each UI frame:
	///  - a node step: the watched node (the selected one) before and after, its name, local transform and
	///    components. While a widget stays held (a drag, typing, the gizmo) its edits go into one step;
	///  - a structure step: the whole scene before nodes were added, removed or moved (Scene::StructureRevision),
	///    so undo brings removed nodes back with their ids.
	/// Node edits outside a UI frame (scripts) are not steps; a structure change between frames becomes one at the
	/// next frame. Frozen in Play: nothing is recorded,
	/// undo and redo wait; Stop gives the scene back as it was, so the steps fit it again.
	/// Owner: Editor, thread: update. The scene outlives it.
	class SceneEdits {
	public:
		/// Steps kept, the oldest goes first.
		static constexpr size_t MaxSteps = 100;

		explicit SceneEdits(Scene& scene);

		SceneEdits(const SceneEdits&) = delete;
		SceneEdits& operator=(const SceneEdits&) = delete;
		SceneEdits(SceneEdits&&) = delete;
		SceneEdits& operator=(SceneEdits&&) = delete;

		/// Play freezes the history; when it thaws the scene as it is now is the start of the next step.
		void SetFrozen(bool frozen);
		/// Before a UI frame: the node whose edits become node steps; an invalid id watches none.
		void BeginFrame(NodeId watched);
		/// After the UI frame: what changed becomes a step. gestureHeld: a widget is still held, its next
		/// edits of the same node go into the same step.
		void EndFrame(bool gestureHeld);

		[[nodiscard]] bool CanUndo() const noexcept { return !m_frozen && !m_undo.empty(); }
		[[nodiscard]] bool CanRedo() const noexcept { return !m_frozen && !m_redo.empty(); }
		/// False when there is nothing to undo or the history is frozen.
		bool Undo();
		bool Redo();
		/// The steps go: another scene or project. The scene as it is now is the start of the next step, and
		/// counts as its file.
		void Clear();
		/// The scene as it is now is what its file has (Save).
		void MarkSaved();
		/// The steps done since MarkSaved: undoing back to that point makes the scene unmodified again. Edits
		/// that are not steps (scripts, Play) do not count.
		[[nodiscard]] bool IsModified() const noexcept { return m_savedDepth != m_undo.size(); }

	private:
		/// What a node step keeps of a node.
		struct NodeState {
			String name;
			math::Matrix4x4 localTransform;
			std::optional<ScriptComponent> script;
			std::optional<RigidBody> rigidBody;
			std::optional<Collider> collider;
			std::optional<ModelComponent> model;

			bool operator==(const NodeState&) const = default;
		};

		struct NodeStep {
			NodeId node;
			NodeState before;
			NodeState after;
		};

		/// The scene on the other side of the step: before it while it is done, after it while it is undone.
		struct StructureStep {
			Scene other;
		};

		using Step = std::variant<NodeStep, StructureStep>;

		[[nodiscard]] static std::optional<NodeState> Capture(const Scene& scene, NodeId node);
		static void Apply(Scene& scene, NodeId node, const NodeState& state);
		void RecordNode(const NodeState& after, bool gestureHeld);
		void Push(Step step);
		/// Undo or redo of one step; the step then belongs to the other side.
		void Reverse(Step& step, bool undo);
		/// The scene as it is now becomes the start of the next step.
		void Rebaseline();

		Scene& m_scene;
		Deque<Step> m_undo;
		Deque<Step> m_redo;
		/// The scene at the start of the next step: the before of a structure step.
		Scene m_baseline;
		uint64_t m_baselineRevision { 0 };
		NodeId m_watched;
		std::optional<NodeState> m_watchedBefore;
		/// The last node step takes the next edits of its node: the widget that made it is still held.
		bool m_gestureOpen { false };
		bool m_frozen { false };
		/// How many undo steps lead to the saved scene; nothing when no steps lead there any more.
		std::optional<size_t> m_savedDepth { 0 };
	};

} // namespace elm
