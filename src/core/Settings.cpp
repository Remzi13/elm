#include "core/Settings.hpp"

#include "core/Unexpected.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace elm {

    namespace {

        constexpr const char* LogSource = "Settings";

        /// A value of a file as the type of current; nothing when it has another type or does not fit.
        auto ConvertFileValue(const Settings::FileValue& fileValue, const Settings::Value& current) -> std::optional<Settings::Value>
        {
            if (std::holds_alternative<bool>(current)) {
                if (const auto* boolean = std::get_if<bool>(&fileValue))
                    return Settings::Value(*boolean);
                return std::nullopt;
            }
            if (std::holds_alternative<String>(current)) {
                if (const auto* text = std::get_if<String>(&fileValue))
                    return Settings::Value(*text);
                return std::nullopt;
            }
            const auto* number = std::get_if<double>(&fileValue);
            if (!number || !std::isfinite(*number))
                return std::nullopt;
            if (std::holds_alternative<uint32_t>(current)) {
                if (*number < 0.0 || *number > std::numeric_limits<uint32_t>::max() || std::floor(*number) != *number)
                    return std::nullopt;
                return Settings::Value(static_cast<uint32_t>(*number));
            }
            if (*number < -std::numeric_limits<float>::max() || *number > std::numeric_limits<float>::max())
                return std::nullopt;
            return Settings::Value(static_cast<float>(*number));
        }

        auto ToFileValue(const Settings::Value& value) -> Settings::FileValue
        {
            if (const auto* integer = std::get_if<uint32_t>(&value))
                return static_cast<double>(*integer);
            if (const auto* number = std::get_if<float>(&value))
                return static_cast<double>(*number);
            if (const auto* text = std::get_if<String>(&value))
                return *text;
            return std::get<bool>(value);
        }

        /// Only what could become a value when its key is registered.
        [[nodiscard]] bool CanBeKept(const Settings::FileValue& fileValue)
        {
            if (const auto* number = std::get_if<double>(&fileValue))
                return std::isfinite(*number);
            return !std::holds_alternative<std::monostate>(fileValue);
        }

    } // namespace

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
        const Path fileName { ".settings" };
        std::error_code error;
        const Path currentPath = FileSystem::CurrentPath(error);
        if (error)
            return fileName;

        const Path directory = FileSystem::FindInParents(currentPath, fileName);
        return (directory.Empty() ? currentPath : directory) / fileName;
    }

    auto Settings::ApplyFileValues(const FileValues& fileValues) -> EngineResult<void>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        UnorderedMap<String, Value> converted;
        FileValues kept;
        for (const auto& [key, fileValue] : fileValues) {
            const auto entry = m_entries.find(key);
            if (entry == m_entries.end()) {
                // The keys registered later (debug channels, windows made on demand) find their values here
                if (CanBeKept(fileValue))
                    kept.emplace(key, fileValue);
                continue;
            }
            auto value = ConvertFileValue(fileValue, entry->second.value);
            if (!value)
                return MakeUnexpected(ErrorCode::SerializationFailed, "Setting " + key + " has an invalid value", log::Category::Core, LogSource);
            converted.emplace(key, std::move(*value));
        }

        for (auto& [key, value] : converted)
            m_entries.at(key).value = std::move(value);
        m_fileValues = std::move(kept);
        return {};
    }

    auto Settings::GetFileValues() const -> FileValues
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Keys not registered in this run stay in the file
        FileValues fileValues = m_fileValues;
        for (const auto& [key, entry] : m_entries)
            fileValues.insert_or_assign(key, ToFileValue(entry.value));
        return fileValues;
    }

    auto Settings::TakeFileValue(const String& key, const Value& defaultValue) -> std::optional<Value>
    {
        const auto found = m_fileValues.find(key);
        if (found == m_fileValues.end())
            return std::nullopt;
        const FileValue fileValue = std::move(found->second);
        m_fileValues.erase(found);
        return ConvertFileValue(fileValue, defaultValue);
    }

    String Settings::MakeKey(Category category, StringView path)
    {
        String result;
        result.reserve(path.size() + 16);

        switch (category) {
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
