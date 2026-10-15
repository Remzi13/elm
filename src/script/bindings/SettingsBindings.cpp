#include "script/bindings/Bindings.hpp"

#include "core/Settings.hpp"

#include <variant>

namespace elm::script::bindings {

    namespace {

        /// What elm.settings reaches.
        struct SettingsContext {
            Settings* settings { nullptr };
        };

        Settings& SettingsOf(lua_State* L) { return RequireSystem(L, ContextOf<SettingsContext>(L).settings, "elm.settings"); }

        const char* CategoryName(Settings::Category category)
        {
            switch (category) {
            case Settings::Category::Core: return "Core";
            case Settings::Category::Render: return "Render";
            case Settings::Category::Physics: return "Physics";
            case Settings::Category::Script: return "Script";
            }
            return "?";
        }

        /// Entry by its full key, "Category/path" as in the .settings file.
        auto FindSetting(const Settings& settings, StringView key) -> std::optional<Settings::Entry>
        {
            for (auto& entry : settings.GetEntries()) {
                const String entryKey = String(CategoryName(entry.category)) + "/" + entry.path;
                if (entryKey == key)
                    return std::move(entry);
            }
            return std::nullopt;
        }

        /// elm.settings.get(key) -> value or nil
        int GetSetting(lua_State* L)
        {
            const auto& settings = SettingsOf(L);
            const auto entry = FindSetting(settings, luaL_checkstring(L, 1));
            if (!entry) {
                lua_pushnil(L);
                return 1;
            }
            std::visit([L](const auto& value) { lua::Push(L, value); }, entry->value);
            return 1;
        }

        /// elm.settings.set(key, value): the value keeps the type the setting was registered with.
        int SetSetting(lua_State* L)
        {
            auto& settings = SettingsOf(L);
            const char* key = luaL_checkstring(L, 1);
            const auto entry = FindSetting(settings, key);
            if (!entry)
                return luaL_error(L, "unknown setting '%s'", key);

            const StringView path(entry->path);
            std::visit([&](const auto& current) {
                using V = std::decay_t<decltype(current)>;
                settings.Set(entry->category, path, lua::StackTraits<V>::Check(L, 2));
            }, entry->value);
            return 0;
        }

    } // namespace

    void RegisterSettingsModule(lua::State& state, Settings* settings)
    {
        state.SetContext(SettingsContext { settings });
        lua::Module(state, "elm.settings")
            .Function<&GetSetting>("get", { "key" })
            .Function<&SetSetting>("set", { "key", "value" });
    }

} // namespace elm::script::bindings
