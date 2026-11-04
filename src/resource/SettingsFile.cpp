#include "resource/SettingsFile.hpp"

#include "core/File.hpp"
#include "core/Settings.hpp"

#include "resource/JsonValue.hpp"
#include "resource/SerializationError.hpp"

#include <system_error>
#include <utility>

namespace elm::resource {

    namespace {

        constexpr const char* LogSource = "Settings";
        constexpr int FormatVersion = 1;

        /// By the JSON kind: a failed As* conversion would log an error.
        auto ToFileValue(const JsonValue& value) -> Settings::FileValue
        {
            switch (value.GetKind()) {
            case JsonValue::Kind::Boolean:
                if (auto boolean = value.AsBoolean())
                    return *boolean;
                break;
            case JsonValue::Kind::String:
                if (auto text = value.AsString())
                    return std::move(*text);
                break;
            case JsonValue::Kind::Number:
                if (auto number = value.AsNumber())
                    return *number;
                break;
            default:
                break;
            }
            return std::monostate {};
        }

        auto ToJson(const Settings::FileValue& value) -> JsonValue
        {
            if (const auto* boolean = std::get_if<bool>(&value))
                return JsonValue(*boolean);
            if (const auto* text = std::get_if<String>(&value))
                return JsonValue(StringView(*text));
            if (const auto* number = std::get_if<double>(&value))
                return JsonValue(*number);
            return JsonValue();
        }

    } // namespace

    auto ReadSettingsFile(Settings& settings, const Path& path) -> EngineResult<void>
    {
        std::error_code fileError;
        const bool fileExists = FileSystem::Exists(path, fileError);
        if (fileError)
            return UnexpectedSerializationError(String("Failed to inspect settings file: ") + fileError.message().c_str(), LogSource);
        if (!fileExists)
            return {};

        File file;
        if (auto opened = file.Open(path, FileMode::Read); !opened)
            return UnexpectedSerializationError(opened.error().message, LogSource);
        const auto serialized = file.ReadAll();
        if (!serialized)
            return UnexpectedSerializationError(serialized.error().message, LogSource);

        JsonValue document;
        if (auto parsed = document.Parse(*serialized->ReadText(0, serialized->Size())); !parsed)
            return UnexpectedSerializationError(parsed.error());
        if (!document.IsObject())
            return UnexpectedSerializationError("Settings document must be a JSON object", LogSource);
        const auto serializedSettings = document.Find("settings");
        if (!serializedSettings || !serializedSettings->IsObject())
            return UnexpectedSerializationError("Settings document is missing its settings object", LogSource);

        Settings::FileValues fileValues;
        for (const String& key : serializedSettings->Keys())
            fileValues.emplace(key, ToFileValue(*serializedSettings->Find(key)));
        return settings.ApplyFileValues(fileValues);
    }

    auto WriteSettingsFile(const Settings& settings, const Path& path) -> EngineResult<void>
    {
        JsonValue serializedSettings { JsonValue::Type::Object };
        for (const auto& [key, value] : settings.GetFileValues()) {
            if (auto set = serializedSettings.Set(key, ToJson(value)); !set)
                return UnexpectedSerializationError(set.error());
        }

        JsonValue document { JsonValue::Type::Object };
        if (auto set = document.Set("version", JsonValue(FormatVersion)); !set)
            return UnexpectedSerializationError(set.error());
        if (auto set = document.Set("settings", std::move(serializedSettings)); !set)
            return UnexpectedSerializationError(set.error());
        auto json = document.ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        File file;
        if (auto opened = file.Open(path, FileMode::Write); !opened)
            return UnexpectedSerializationError(opened.error().message, LogSource);
        if (auto written = file.Write(*json); !written)
            return UnexpectedSerializationError(written.error().message, LogSource);
        if (auto closed = file.Close(); !closed)
            return UnexpectedSerializationError(closed.error().message, LogSource);
        return {};
    }

} // namespace elm::resource
