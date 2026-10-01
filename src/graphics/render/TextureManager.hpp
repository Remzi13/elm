#pragma once

#include "core/Std.hpp"
#include "core/Handler.hpp"

#include "graphics/render/Texture.hpp"
#include "graphics/render/CommandQueue.hpp"

#include <mutex>
#include <optional>

namespace elm::render {

	class TextureManager {
	public:
		TextureManager();
		~TextureManager();

		static TextureManager& Get();

		bool Init(CommandQueue* commandQueue);
		void Shutdown();

		// Producer thread: allocate handlers and push commands
		[[nodiscard]] core::Handler CreateTexture(const TextureInfo& info);
		[[nodiscard]] core::Handler AllocateHandler(const TextureInfo& info);
		void UpdateTexture(const core::Handler& handler, const TextureData& textureData);
		void DestroyTexture(const core::Handler& handler);
		void UnregisterTexture(const core::Handler& handler);

		[[nodiscard]] std::optional<TextureInfo> GetTextureInfo(const core::Handler& handler) const;

	private:
		int m_currentIndex{ 0 };

		CommandQueue* m_commandQueue{ nullptr };
		mutable std::mutex m_textureInfosMutex;
		UnorderedMap<core::Handler, TextureInfo> m_textureInfos;
	};

} // namespace elm::render
