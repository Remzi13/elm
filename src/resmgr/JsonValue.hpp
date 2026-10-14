#pragma once
#include "core/Std.hpp"
#include "core/Error.hpp"

#include <optional>
#include <variant>

namespace elm::resmgr {
    class JsonValue {
        struct Storage;
        using PathElement = std::variant<String, size_t>;

    public:
        enum class Type {
            Null,
            Object,
            Array
        };

        /// What a value holds, see GetKind.
        enum class Kind {
            Null,
            Boolean,
            Number,
            String,
            Array,
            Object
        };

        JsonValue();
        explicit JsonValue(Type type);
        explicit JsonValue(bool value);
        explicit JsonValue(double value);
        explicit JsonValue(StringView value);
        explicit JsonValue(const char* value);

        template<std::integral T>
            requires (!std::same_as<T, bool>)
        explicit JsonValue(T value) : JsonValue(static_cast<double>(value)) {}

        ~JsonValue();
        JsonValue(const JsonValue&);
        auto operator=(const JsonValue&) -> JsonValue&;
        JsonValue(JsonValue&&) noexcept;
        auto operator=(JsonValue&&) noexcept -> JsonValue&;

        [[nodiscard]] auto Parse(StringView json) -> EngineResult<void>;
        [[nodiscard]] auto SetObject() -> EngineResult<void>;
        [[nodiscard]] auto SetArray() -> EngineResult<void>;

        [[nodiscard]] auto ToString() const -> EngineResult<String>;
        [[nodiscard]] auto Find(StringView key) const -> std::optional<JsonValue>;
        [[nodiscard]] auto operator[](StringView key) const -> JsonValue;
        [[nodiscard]] auto operator[](size_t index) const -> JsonValue;
        [[nodiscard]] auto At(size_t index) const -> JsonValue;
        [[nodiscard]] auto Set(StringView key, JsonValue value) -> EngineResult<void>;
        [[nodiscard]] auto Set(JsonValue value) -> EngineResult<void>;
        [[nodiscard]] auto Push(JsonValue value) -> EngineResult<void>;
        [[nodiscard]] auto Size() const -> EngineResult<size_t>;
        [[nodiscard]] auto Exists() const -> EngineResult<void>;
        [[nodiscard]] auto IsObject() const -> bool;
        [[nodiscard]] auto IsArray() const -> bool;
        /// The kind of the value without logging an error (As* log one on a wrong type), for
        /// fields that may hold several kinds. Null for a missing value too.
        [[nodiscard]] auto GetKind() const -> Kind;
        /// Field names of an object in sorted order, empty for anything else.
        [[nodiscard]] auto Keys() const -> Vector<String>;
        [[nodiscard]] auto AsNumber() const -> EngineResult<double>;
        [[nodiscard]] auto AsBoolean() const -> EngineResult<bool>;
        [[nodiscard]] auto AsString() const -> EngineResult<String>;

    private:
        JsonValue(SharedPtr<Storage> storage, Vector<PathElement> path);

        [[nodiscard]] auto Resolve(bool create) const -> EngineResult<void*>;

        SharedPtr<Storage> m_storage;
        Vector<PathElement> m_path;
    };
}
