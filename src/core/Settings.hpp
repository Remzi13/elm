#pragma once

#include <filesystem>
#include <mutex>
#include <variant>

#include "core/Std.hpp"
#include "core/Error.hpp"

namespace elm
{
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

		struct Entry
		{
			Category category;
			String path;
			String label;
			Value value;
		};

	public:
		Settings();

		template<typename T>
		void Register(Category category, StringView path, StringView label, T defaultValue)
		{
			const String key = MakeKey(category, path);
			std::lock_guard<std::mutex> lock(m_mutex);

			const auto entry = m_entries.find(key);
			if (entry == m_entries.end()) {
				m_entries.emplace(key, Entry{
					category,
					String(path.begin(), path.end()),
					String(label.begin(), label.end()),
					Value(std::move(defaultValue))
				});
				return;
			}

			entry->second.category = category;
			entry->second.path = String(path.begin(), path.end());
			entry->second.label = String(label.begin(), label.end());
		}

		template<typename T>
		void Set(Category category, StringView path, T&& value)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const String key = MakeKey(category, path);
			const auto entry = m_entries.find(key);
			if (entry != m_entries.end()) {
				entry->second.value = Value(std::forward<T>(value));
				return;
			}

			m_entries.emplace(key, Entry{
				category,
				String(path.begin(), path.end()),
				String(path.begin(), path.end()),
				Value(std::forward<T>(value))
			});
		}

		template<typename T>
		T Get(Category category, StringView path, T defaultValue = T{}) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_entries.find(MakeKey(category, path));

			if (it == m_entries.end())
				return defaultValue;

			if (const auto* value = std::get_if<T>(&it->second.value))
				return *value;

			return defaultValue;
		}

		[[nodiscard]] auto GetEntries() const -> Vector<Entry>;
		[[nodiscard]] static auto DefaultFilePath() -> std::filesystem::path;
		[[nodiscard]] auto Load(const std::filesystem::path& path) -> EngineResult<void>;
		[[nodiscard]] auto Save(const std::filesystem::path& path) const -> EngineResult<void>;

	private:
		static String MakeKey(Category category, StringView path);

	private:
		mutable std::mutex m_mutex;
		UnorderedMap<String, Entry> m_entries;
	};
}