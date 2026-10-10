#include "graphics/Settings.hpp"

#include "core/Unexpected.hpp"
#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>

namespace elm {

	Settings::Settings() = default;

	auto Settings::GetEntries() const -> Vector<Entry>
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Vector<Entry> entries;
		entries.reserve(m_entries.size());

		for (const auto& [key, entry] : m_entries) {
			(void)key;
			entries.push_back(entry);
		}

		std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
			return left.label < right.label;
		});
		return entries;
	}

	auto Settings::DefaultFilePath() -> std::filesystem::path
	{
		const std::filesystem::path sourceRoot =
			std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
		std::error_code error;
		if (std::filesystem::exists(sourceRoot / "CMakeLists.txt", error) && !error)
			return sourceRoot / ".settings";

		return std::filesystem::path{ ".settings" };
	}

	auto Settings::Load(const std::filesystem::path& path) -> EngineResult<void>
	{
		std::error_code fileError;
		const bool fileExists = std::filesystem::exists(path, fileError);
		if (fileError)
			return resmgr::UnexpectedSerializationError(
				String("Failed to inspect settings file: ") + fileError.message().c_str(), "Settings");
		if (!fileExists)
			return {};

		std::ifstream file(path, std::ios::binary);
		if (!file)
			return resmgr::UnexpectedSerializationError("Failed to open settings file for reading", "Settings");

		const String serialized{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		if (file.bad())
			return resmgr::UnexpectedSerializationError("Failed to read settings file", "Settings");

		resmgr::JsonValue document;
		if (auto parsed = document.Parse(StringView(serialized.data(), serialized.size())); !parsed)
			return resmgr::UnexpectedSerializationError(parsed.error());
		if (!document.IsObject())
			return resmgr::UnexpectedSerializationError("Settings document must be a JSON object", "Settings");

		const auto serializedSettings = document.Find("settings");
		if (!serializedSettings || !serializedSettings->IsObject())
			return resmgr::UnexpectedSerializationError("Settings document is missing its settings object", "Settings");

		std::lock_guard<std::mutex> lock(m_mutex);
		UnorderedMap<String, Value> loadedSettings;
		loadedSettings.reserve(m_entries.size());
		for (const auto& [key, entry] : m_entries) {
			loadedSettings.emplace(key, entry.value);
		}

		for (const auto& [key, currentValue] : loadedSettings) {
			const auto serializedValue = serializedSettings->Find(StringView(key.data(), key.size()));
			if (!serializedValue)
				continue;

			Value loadedValue;
			if (std::holds_alternative<bool>(currentValue)) {
				auto value = serializedValue->AsBoolean();
				if (!value)
					return resmgr::UnexpectedSerializationError("Boolean setting has an invalid value", "Settings");
				loadedValue = *value;
			}
			else if (std::holds_alternative<String>(currentValue)) {
				auto value = serializedValue->AsString();
				if (!value)
					return resmgr::UnexpectedSerializationError("String setting has an invalid value", "Settings");
				loadedValue = std::move(*value);
			}
			else {
				auto value = serializedValue->AsNumber();
				if (!value || !std::isfinite(*value))
					return resmgr::UnexpectedSerializationError("Numeric setting has an invalid value", "Settings");

				if (std::holds_alternative<uint32_t>(currentValue)) {
					if (*value < 0.0 || *value > std::numeric_limits<uint32_t>::max() ||
						std::floor(*value) != *value)
						return resmgr::UnexpectedSerializationError("Unsigned setting is out of range", "Settings");
					loadedValue = static_cast<uint32_t>(*value);
				}
				else {
					if (*value < -std::numeric_limits<float>::max() ||
						*value > std::numeric_limits<float>::max())
						return resmgr::UnexpectedSerializationError("Floating-point setting is out of range", "Settings");
					loadedValue = static_cast<float>(*value);
				}
			}

			loadedSettings[key] = std::move(loadedValue);
		}

		for (auto& [key, entry] : m_entries)
			entry.value = std::move(loadedSettings.at(key));
		return {};
	}

	auto Settings::Save(const std::filesystem::path& path) const -> EngineResult<void>
	{
		UnorderedMap<String, Entry> settings;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			settings = m_entries;
		}

		resmgr::JsonValue serializedSettings{ resmgr::JsonValue::Type::Object };
		for (const auto& [key, entry] : settings) {
			resmgr::JsonValue serializedValue;
			if (const auto* integerValue = std::get_if<uint32_t>(&entry.value))
				serializedValue = resmgr::JsonValue(*integerValue);
			else if (const auto* floatValue = std::get_if<float>(&entry.value))
				serializedValue = resmgr::JsonValue(static_cast<double>(*floatValue));
			else if (const auto* stringValue = std::get_if<String>(&entry.value))
				serializedValue = resmgr::JsonValue(StringView(stringValue->data(), stringValue->size()));
			else
				serializedValue = resmgr::JsonValue(std::get<bool>(entry.value));

			if (auto set = serializedSettings.Set(StringView(key.data(), key.size()), std::move(serializedValue)); !set)
				return resmgr::UnexpectedSerializationError(set.error());
		}

		resmgr::JsonValue document{ resmgr::JsonValue::Type::Object };
		if (auto set = document.Set("version", resmgr::JsonValue(1)); !set)
			return resmgr::UnexpectedSerializationError(set.error());
		if (auto set = document.Set("settings", std::move(serializedSettings)); !set)
			return resmgr::UnexpectedSerializationError(set.error());

		auto json = document.ToString();
		if (!json)
			return resmgr::UnexpectedSerializationError(json.error());

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
			return resmgr::UnexpectedSerializationError("Failed to open settings file for writing", "Settings");
		file.write(json->data(), static_cast<std::streamsize>(json->size()));
		file.flush();
		if (!file)
			return resmgr::UnexpectedSerializationError("Failed to write settings file", "Settings");

		return {};
	}

	String Settings::MakeKey(Category category, StringView path)
	{
		String result;
		result.reserve(path.size() + 16);

		switch (category)
		{
		case Category::Core:
			result = "Core/";
			break;

		case Category::Render:
			result = "Render/";
			break;

		case Category::Physics:
			result = "Physics/";
			break;
		}

		result += path;

		return result;
	}

} // namespace elm
