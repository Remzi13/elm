#pragma once

#include "core/Std.hpp"

#include "math/Primitivs.hpp"
#include "graphics/culling/OcclusionCullingSystem.hpp"

namespace Diligent {
}

namespace elm {
	class RenderTexture;

	struct FrameStats {
		float fps{ 0.0f };
		float deltaTimeMs{ 0.0f };
		uint32_t physicsBodyCount{ 0 };
		Transform boxTransform;
		Transform groundTransform;
	};

	struct ImGuiConfig {
		bool enableFrustumCulling{ true };
		bool enableOcclusionCulling{ true };
		float depthBias{ 0.0f };
		int resolution{ 2 };
		VisualMode visualMode{ VisualMode::HideCulled };
		bool depthPreviewFalseColor{ true };
	};

	struct ImGuiUpdateContext {
		Scene& scene;
		const FrameStats& stats;
		uint32_t width{ 0 };
		uint32_t height{ 0 };
		size_t renderMemory{ 0 };
		RenderTexture* engineViewport{ nullptr };
		uint32_t engineViewportWidth{ 0 };
		uint32_t engineViewportHeight{ 0 };
		RenderTexture* depthPreview{ nullptr };
		const SoftwareDepthBuffer* depthBuffer{ nullptr };
		uint32_t depthPreviewWidth{ 0 };
		uint32_t depthPreviewHeight{ 0 };
		bool depthPreviewFalseColor{ true };
		CullingStats cullingStats;
		bool enableFrustumCulling{ true };
		bool enableOcclusionCulling{ true };
		float depthBias{ 0.0f };
		VisualMode visualMode{ VisualMode::HideCulled };
	};

	class IImGuiWindow {
	public:
		virtual ~IImGuiWindow() = default;

		virtual void Update(const ImGuiUpdateContext& context, ImGuiConfig& config) = 0;
		[[nodiscard]] virtual StringView GetName() const = 0;

		[[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
		void SetVisible(bool visible) noexcept { m_visible = visible; }
		bool* GetVisiblePtr() noexcept { return &m_visible; }

	protected:
		bool m_visible{ true };
	};

} // namespace Engine
