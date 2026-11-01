#pragma once

#include "core/Std.hpp"
#include "core/MessageBus.hpp"
#include "core/Settings.hpp"

#include "graphics/Camera.hpp"
#include "graphics/ModelManager.hpp"

#include "Scene/Scene.hpp"

#include <optional>
#include <utility>

namespace elm {

	struct FrameStats;

	namespace render {
		class RenderSystem;
		class ViewPort;
		class FrameWriter;
	}

	namespace graphics {
		class DebugDraw;
	}

	struct ImGuiWindowContext {
		render::RenderSystem& renderSystem;
		Scene& scene;
        graphics::ModelManager& models;
		Camera& camera;
		const FrameStats& stats;
		render::ViewPort& viewPort;
		/// Frame being built: windows may add their own scene views to it.
		render::FrameWriter& frame;
		/// Debug primitives of the engine view and their channels.
		graphics::DebugDraw& debugDraw;
	};

	class IImGuiWindow {
	public:
		IImGuiWindow(Settings& settings) : m_settings(settings) {}
		virtual ~IImGuiWindow() = default;

		virtual void Render(ImGuiWindowContext& context) = 0;
		[[nodiscard]] virtual StringView GetName() const = 0;

		[[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
		void SetVisible(bool visible) noexcept { m_visible = visible; }
		bool* GetVisiblePtr() noexcept { return &m_visible; }

	protected:
		template <typename Query>
		[[nodiscard]] std::optional<Query> Request() const {
			return m_messageBus->Request<Query>();
		}

		template <typename Query, typename Handler>
		[[nodiscard]] MessageBus::Subscription RegisterQuery(Handler&& handler) {
			return m_messageBus->RegisterQuery<Query>(std::forward<Handler>(handler));
		}

		virtual void OnAttach() {}

		Settings& m_settings;
		bool m_visible{ true };

	private:
		friend class ImGuiSystem;

		void Attach(MessageBus& messageBus) {
			m_messageBus = &messageBus;
			OnAttach();
		}

		MessageBus* m_messageBus{ nullptr };
	};

} // namespace Engine
