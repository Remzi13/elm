#pragma once

#include "core/Std.hpp"
#include "graphics/render/Render.hpp"

namespace elm::render {

	struct TextureHandler {
		int index{ -1 };

		[[nodiscard]] constexpr bool IsValid() const noexcept { return index >= 0; }
		constexpr auto operator<=>(const TextureHandler&) const noexcept = default;
	};

	struct TextureInfo {
		String name;
		uint32_t width{ 0 };
		uint32_t height{ 0 };
		TextureFormat format{ TextureFormat::RGBA8_UNORM };
		TextureUsage usage{ TextureUsage::Default };
		uint32_t bindFlags{ TextureBindFlags::BindShaderResource };		
	};

	
	struct TextureData {
		Vector<uint8_t> data;
		size_t stride{ 0 };
	};

	class Texture {
	public:
		Texture() = default;
		explicit Texture(const TextureInfo& info);
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		[[nodiscard]] constexpr TextureHandler GetHandler() const noexcept { return m_handler; }
		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_handler.IsValid(); }
		constexpr explicit operator bool() const noexcept { return IsValid(); }

		[[nodiscard]] const TextureInfo& GetInfo() const noexcept { return m_info; }
		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_info.width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_info.height; }
		[[nodiscard]] TextureFormat GetFormat() const noexcept { return m_info.format; }
		[[nodiscard]] TextureUsage GetUsage() const noexcept { return m_info.usage; }
		[[nodiscard]] uint32_t GetBindFlags() const noexcept { return m_info.bindFlags; }
		[[nodiscard]] StringView GetName() const noexcept { return m_info.name; }
			
		void Update(const TextureData& textureData);
		

	private:
		TextureHandler m_handler;
		TextureInfo m_info;
	};

} // namespace elm::render
