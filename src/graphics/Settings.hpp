#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <variant>
#include <mutex>

#include "core/Std.hpp"

namespace elm
{
	constexpr const char* CULLING_RESOLUTION_WIDTH = "culling/Resolution/Width";
	constexpr const char* CULLING_RESOLUTION_HEIGHT = "culling/Resolution/Height";
	constexpr const char* CULLING_ENABLE_FRUSTUM_CULLING = "culling/EnableFrustumCulling";
	constexpr const char* CULLING_ENABLE_OCCLUSION_CULLING = "culling/EnableOcclusionCulling";
	constexpr const char* CULLING_DEPTH_BIAS = "culling/DepthBias";
	constexpr const char* CULLING_VISUAL_MODE = "culling/VisualMode";

	class Settings
	{
	public:
		enum class Category
		{
			Core,
			Render,
			Physics,
		};

		using Value = std::variant<uint32_t, float, String, bool >;

	private:
		using Storage = UnorderedMap<String, Value>;

	public:
		Settings() = default;

		template<typename T>
		void Set(Category category, StringView path, T&& value)
		{
			m_settings[1][MakeKey(category, path)] = Value(std::forward<T>(value));
		}

		template<typename T>
		T Get(Category category, StringView path, T defaultValue = T{}) const
		{
			const auto& settings = m_settings[0];

			auto it = settings.find(MakeKey(category, path));

			if (it == settings.end())
				return defaultValue;

			if (const auto* value = std::get_if<T>(&it->second))
				return *value;

			return defaultValue;
		}

		void Flash()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_settings[0] = m_settings[1];
		}

	private:
		static String MakeKey(Category category, StringView path);

	private:
		std::mutex m_mutex;
		std::array<Storage, 2> m_settings;
	};
}