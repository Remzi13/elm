#pragma once

#include "core/Std.hpp"
#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include <mutex>
#include <optional>
#include <variant>

namespace elm {
    /// The settings of a program by the key Category/path, with their values in memory: reading and writing the
    /// .settings file is the job of resource/SettingsFile. Owner: the program. Any thread, under its own lock.
    class Settings {
    public:
        enum class Category {
            Core,
            Render,
            Physics,
            Script,
        };

        using Value = std::variant<uint32_t, float, String, bool>;

        /// A value of a settings file as its JSON type; monostate for any other kind.
        using FileValue = std::variant<std::monostate, bool, String, double>;
        using FileValues = UnorderedMap<String, FileValue>;

        struct Entry {
            Category category;
            String path;
            String label;
            Value value;
        };

    public:
        Settings();

        template <typename T>
        void Register(Category category, StringView path, StringView label, T defaultValue)
        {
            const String key = MakeKey(category, path);
            std::lock_guard<std::mutex> lock(m_mutex);

            const auto entry = m_entries.find(key);
            if (entry == m_entries.end()) {
                // Registered after Load: the file's value wins over the default
                Value value(std::move(defaultValue));
                if (auto loaded = TakeFileValue(key, value))
                    value = std::move(*loaded);
                m_entries.emplace(key, Entry { category, String(path.begin(), path.end()), String(label.begin(), label.end()), std::move(value) });
                return;
            }

            entry->second.category = category;
            entry->second.path = String(path.begin(), path.end());
            entry->second.label = String(label.begin(), label.end());
        }

        template <typename T>
        void Set(Category category, StringView path, T&& value)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const String key = MakeKey(category, path);
            const auto entry = m_entries.find(key);
            if (entry != m_entries.end()) {
                entry->second.value = Value(std::forward<T>(value));
                return;
            }

            m_fileValues.erase(key);
            m_entries.emplace(key, Entry { category, String(path.begin(), path.end()), String(path.begin(), path.end()), Value(std::forward<T>(value)) });
        }

        template <typename T>
        T Get(Category category, StringView path, T defaultValue = T {}) const
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
        [[nodiscard]] static auto DefaultFilePath() -> Path;
        /// What a file holds, by key. A registered key takes its value; one that does not fit is an error and
        /// changes nothing. The others replace the kept values: Register takes them, GetFileValues returns them.
        [[nodiscard]] auto ApplyFileValues(const FileValues& fileValues) -> EngineResult<void>;
        /// What a file should hold: the registered values and the kept ones.
        [[nodiscard]] auto GetFileValues() const -> FileValues;

    private:
        static String MakeKey(Category category, StringView path);
        /// The file's value of key as the type of defaultValue, taken out of m_fileValues; nothing when the
        /// file has none or one of another type. Called with m_mutex held.
        [[nodiscard]] std::optional<Value> TakeFileValue(const String& key, const Value& defaultValue);

    private:
        mutable std::mutex m_mutex;
        UnorderedMap<String, Entry> m_entries;
        /// Applied before their keys were registered: Register takes them, GetFileValues returns them.
        UnorderedMap<String, FileValue> m_fileValues;
    };
} // namespace elm
