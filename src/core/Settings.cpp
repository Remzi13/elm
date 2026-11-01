#include "core/Settings.hpp"

#include "resource/JsonValue.hpp"
#include "resource/SerializationError.hpp"

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

	auto Settings::DefaultFilePath() -> Path
	{
		const Path fileName{ ".settings" };
		std::error_code error;
		const Path currentPath = FileSystem::CurrentPath(error);
		if (error)
			return fileName;

		const Path directory = FileSystem::FindInParents(currentPath, fileName);
		return (directory.Empty() ? currentPath : directory) / fileName;
	}

	auto Settings::Load(const Path& path) -> EngineResult<void>
	{
		std::error_code fileError;
		const bool fileExists = FileSystem::Exists(path, fileError);
		if (fileError)
			return resource::UnexpectedSerializationError(
				String("Failed to inspect settings file: ") + fileError.message().c_str(), "Settings");
		if (!fileExists)
			return {};

		auto file = FileSystem::OpenForReading(path, std::ios::binary);
		if (!file)
			return resource::UnexpectedSerializationError("Failed to open settings file for reading", "Settings");

		const String serialized{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		if (file.bad())
			return resource::UnexpectedSerializationError("Failed to read settings file", "Settings");

		resource::JsonValue document;
		if (auto parsed = document.Parse(StringView(serialized.data(), serialized.size())); !parsed)
			return resource::UnexpectedSerializationError(parsed.error());
		if (!document.IsObject())
			return resource::UnexpectedSerializationError("Settings document must be a JSON object", "Settings");

		const auto serializedSettings = document.Find("settings");
		if (!serializedSettings || !serializedSettings->IsObject())
			return resource::UnexpectedSerializationError("Settings document is missing its settings object", "Settings");

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
					return resource::UnexpectedSerializationError("Boolean setting has an invalid value", "Settings");
				loadedValue = *value;
			}
			else if (std::holds_alternative<String>(currentValue)) {
				auto value = serializedValue->AsString();
				if (!value)
					return resource::UnexpectedSerializationError("String setting has an invalid value", "Settings");
				loadedValue = std::move(*value);
			}
			else {
				auto value = serializedValue->AsNumber();
				if (!value || !std::isfinite(*value))
					return resource::UnexpectedSerializationError("Numeric setting has an invalid value", "Settings");

				if (std::holds_alternative<uint32_t>(currentValue)) {
					if (*value < 0.0 || *value > std::numeric_limits<uint32_t>::max() ||
						std::floor(*value) != *value)
						return resource::UnexpectedSerializationError("Unsigned setting is out of range", "Settings");
					loadedValue = static_cast<uint32_t>(*value);
				}
				else {
					if (*value < -std::numeric_limits<float>::max() ||
						*value > std::numeric_limits<float>::max())
						return resource::UnexpectedSerializationError("Floating-point setting is out of range", "Settings");
					loadedValue = static_cast<float>(*value);
				}
			}

			loadedSettings[key] = std::move(loadedValue);
		}

		for (auto& [key, entry] : m_entries)
			entry.value = std::move(loadedSettings.at(key));

		// The keys registered later (debug channels, windows made on demand) find their values here
		m_fileValues.clear();
		for (const String& key : serializedSettings->Keys()) {
			if (m_entries.contains(key))
				continue;
			// By the JSON kind: a failed As* conversion would log an error
			const auto value = serializedSettings->Find(StringView(key.data(), key.size()));
			switch (value->GetKind()) {
			case resource::JsonValue::Kind::Boolean:
				if (auto boolean = value->AsBoolean())
					m_fileValues.emplace(key, *boolean);
				break;
			case resource::JsonValue::Kind::String:
				if (auto text = value->AsString())
					m_fileValues.emplace(key, std::move(*text));
				break;
			case resource::JsonValue::Kind::Number:
				if (auto number = value->AsNumber(); number && std::isfinite(*number))
					m_fileValues.emplace(key, *number);
				break;
			default:
				break;
			}
		}
		return {};
	}

	auto Settings::TakeFileValue(const String& key, const Value& defaultValue) -> std::optional<Value>
	{
		const auto found = m_fileValues.find(key);
		if (found == m_fileValues.end())
			return std::nullopt;
		const FileValue fileValue = std::move(found->second);
		m_fileValues.erase(found);

		if (std::holds_alternative<bool>(defaultValue)) {
			if (const auto* boolean = std::get_if<bool>(&fileValue))
				return Value(*boolean);
			return std::nullopt;
		}
		if (std::holds_alternative<String>(defaultValue)) {
			if (const auto* text = std::get_if<String>(&fileValue))
				return Value(*text);
			return std::nullopt;
		}
		const auto* number = std::get_if<double>(&fileValue);
		if (!number)
			return std::nullopt;
		if (std::holds_alternative<float>(defaultValue))
			return Value(static_cast<float>(*number));
		const bool unsignedInteger = *number >= 0.0 && *number <= std::numeric_limits<uint32_t>::max() && std::floor(*number) == *number;
		if (!unsignedInteger)
			return std::nullopt;
		return Value(static_cast<uint32_t>(*number));
	}

	auto Settings::Save(const Path& path) const -> EngineResult<void>
	{
		UnorderedMap<String, Entry> settings;
		UnorderedMap<String, FileValue> fileValues;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			settings = m_entries;
			fileValues = m_fileValues;
		}

		resource::JsonValue serializedSettings{ resource::JsonValue::Type::Object };
		for (const auto& [key, entry] : settings) {
			resource::JsonValue serializedValue;
			if (const auto* integerValue = std::get_if<uint32_t>(&entry.value))
				serializedValue = resource::JsonValue(*integerValue);
			else if (const auto* floatValue = std::get_if<float>(&entry.value))
				serializedValue = resource::JsonValue(static_cast<double>(*floatValue));
			else if (const auto* stringValue = std::get_if<String>(&entry.value))
				serializedValue = resource::JsonValue(StringView(stringValue->data(), stringValue->size()));
			else
				serializedValue = resource::JsonValue(std::get<bool>(entry.value));

			if (auto set = serializedSettings.Set(StringView(key.data(), key.size()), std::move(serializedValue)); !set)
				return resource::UnexpectedSerializationError(set.error());
		}
		// Keys not registered in this run stay in the file
		for (const auto& [key, fileValue] : fileValues) {
			resource::JsonValue serializedValue;
			if (const auto* boolean = std::get_if<bool>(&fileValue))
				serializedValue = resource::JsonValue(*boolean);
			else if (const auto* text = std::get_if<String>(&fileValue))
				serializedValue = resource::JsonValue(StringView(text->data(), text->size()));
			else
				serializedValue = resource::JsonValue(std::get<double>(fileValue));
			if (auto set = serializedSettings.Set(StringView(key.data(), key.size()), std::move(serializedValue)); !set)
				return resource::UnexpectedSerializationError(set.error());
		}

		resource::JsonValue document{ resource::JsonValue::Type::Object };
		if (auto set = document.Set("version", resource::JsonValue(1)); !set)
			return resource::UnexpectedSerializationError(set.error());
		if (auto set = document.Set("settings", std::move(serializedSettings)); !set)
			return resource::UnexpectedSerializationError(set.error());

		auto json = document.ToString();
		if (!json)
			return resource::UnexpectedSerializationError(json.error());

		auto file = FileSystem::OpenForWriting(path, std::ios::binary | std::ios::trunc);
		if (!file)
			return resource::UnexpectedSerializationError("Failed to open settings file for writing", "Settings");
		file.write(json->data(), static_cast<std::streamsize>(json->size()));
		file.flush();
		if (!file)
			return resource::UnexpectedSerializationError("Failed to write settings file", "Settings");

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

		case Category::Script:
			result = "Script/";
			break;
		}

		result += path;

		return result;
	}

} // namespace elm
