#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"

#include <utility>

#include <glaze/glaze.hpp>

namespace elm::resmgr {
    struct JsonValue::Storage {
        glz::json_t value;
    };

    JsonValue::JsonValue() : m_storage(MakeShared<Storage>()) {}

    JsonValue::JsonValue(Type type) : JsonValue()
    {
        if (type == Type::Object)
            m_storage->value.data = glz::json_t::object_t{};
        else if (type == Type::Array)
            m_storage->value.data = glz::json_t::array_t{};
    }

    JsonValue::JsonValue(bool value) : JsonValue()
    {
        m_storage->value = value;
    }

    JsonValue::JsonValue(double value) : JsonValue()
    {
        m_storage->value = value;
    }

    JsonValue::JsonValue(StringView value) : JsonValue()
    {
        m_storage->value = String(value.data() ? value.data() : "", value.size());
    }

    JsonValue::JsonValue(const char* value) : JsonValue(StringView(value ? value : ""))
    {
    }

    JsonValue::~JsonValue() = default;
    JsonValue::JsonValue(const JsonValue&) = default;
    auto JsonValue::operator=(const JsonValue&) -> JsonValue& = default;
    JsonValue::JsonValue(JsonValue&&) noexcept = default;
    auto JsonValue::operator=(JsonValue&&) noexcept -> JsonValue& = default;

    JsonValue::JsonValue(SharedPtr<Storage> storage, Vector<PathElement> path)
        : m_storage(std::move(storage)), m_path(std::move(path))
    {
    }

    auto JsonValue::Resolve(bool create) const -> EngineResult<void*>
    {
        if (!m_storage)
            return UnexpectedSerializationError("JSON node has no document", "JsonValue");

        auto* current = &m_storage->value;
        for (size_t index = 0; index < m_path.size(); ++index) {
            const auto& pathElement = m_path[index];
            if (const auto* key = std::get_if<String>(&pathElement)) {
                if (current->holds<glz::json_t::null_t>() && create)
                    current->data = glz::json_t::object_t{};

                auto* object = current->get_if<glz::json_t::object_t>();
                if (!object)
                    return UnexpectedSerializationError("JSON path traverses a non-object value", "JsonValue");

                const StringView fieldName(key->data(), key->size());
                auto field = object->find(fieldName);
                if (field == object->end()) {
                    if (!create)
                        return UnexpectedSerializationError("JSON object is missing key: " + String(fieldName), "JsonValue");
                    field = object->emplace(String(fieldName), glz::json_t{}).first;
                }
                current = &field->second;
            } else {
                auto* array = current->get_if<glz::json_t::array_t>();
                if (!array)
                    return UnexpectedSerializationError("JSON path traverses a non-array value", "JsonValue");

                const auto arrayIndex = std::get<size_t>(pathElement);
                if (arrayIndex >= array->size())
                    return UnexpectedSerializationError("JSON array index is out of range", "JsonValue");
                current = &(*array)[arrayIndex];
            }
        }

        return static_cast<void*>(current);
    }

    auto JsonValue::Parse(StringView source) -> EngineResult<void>
    {
        if (!m_path.empty())
            return UnexpectedSerializationError("JSON can only be parsed into a document root", "JsonValue");

        const StringView json(source.data() ? source.data() : "", source.size());
        auto result = glz::read_json<glz::json_t>(json);
        if (!result)
            return UnexpectedSerializationError(glz::format_error(result.error(), json), "JsonValue");

        m_storage->value = std::move(*result);
        return {};
    }

    auto JsonValue::SetObject() -> EngineResult<void>
    {
        auto node = Resolve(true);
        if (!node)
            return UnexpectedSerializationError(node.error());

        static_cast<glz::json_t*>(*node)->data = glz::json_t::object_t{};
        return {};
    }

    auto JsonValue::SetArray() -> EngineResult<void>
    {
        auto node = Resolve(true);
        if (!node)
            return UnexpectedSerializationError(node.error());

        static_cast<glz::json_t*>(*node)->data = glz::json_t::array_t{};
        return {};
    }

    auto JsonValue::ToString() const -> EngineResult<String>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());

        String json;
        const auto error = glz::write_json(*static_cast<const glz::json_t*>(*node), json);
        if (error)
            return UnexpectedSerializationError(glz::format_error(error, json), "JsonValue");

        const auto formattedJson = glz::prettify_json(json);
        return String(formattedJson.begin(), formattedJson.end());
    }

    auto JsonValue::operator[](StringView key) const -> JsonValue
    {
        auto path = m_path;
        path.emplace_back(String(key.begin(), key.end()));
        return JsonValue(m_storage, std::move(path));
    }

    auto JsonValue::operator[](size_t index) const -> JsonValue
    {
        auto path = m_path;
        path.emplace_back(index);
        return JsonValue(m_storage, std::move(path));
    }

    auto JsonValue::At(size_t index) const -> JsonValue
    {
        return (*this)[index];
    }

    auto JsonValue::Set(StringView key, JsonValue value) -> EngineResult<void>
    {
        return (*this)[key].Set(std::move(value));
    }

    auto JsonValue::Set(JsonValue value) -> EngineResult<void>
    {
        auto node = Resolve(true);
        if (!node)
            return UnexpectedSerializationError(node.error());
        auto source = value.Resolve(false);
        if (!source)
            return UnexpectedSerializationError(source.error());

        const auto replacement = *static_cast<const glz::json_t*>(*source);
        *static_cast<glz::json_t*>(*node) = replacement;
        return {};
    }

    auto JsonValue::Push(JsonValue value) -> EngineResult<void>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());

        auto* array = static_cast<glz::json_t*>(*node)->get_if<glz::json_t::array_t>();
        if (!array)
            return UnexpectedSerializationError("Cannot append to a non-array JSON value", "JsonValue");

        auto source = value.Resolve(false);
        if (!source)
            return UnexpectedSerializationError(source.error());

        auto element = *static_cast<const glz::json_t*>(*source);
        array->push_back(std::move(element));
        return {};
    }

    auto JsonValue::Size() const -> EngineResult<size_t>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());

        const auto& value = *static_cast<const glz::json_t*>(*node);
        if (const auto* array = value.get_if<glz::json_t::array_t>())
            return array->size();
        if (const auto* object = value.get_if<glz::json_t::object_t>())
            return object->size();

        return UnexpectedSerializationError("JSON value is not an object or array", "JsonValue");
    }

    auto JsonValue::Exists() const -> EngineResult<void>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());

        return {};
    }

    auto JsonValue::IsObject() const -> bool
    {
        auto node = Resolve(false);
        return node && static_cast<const glz::json_t*>(*node)->get_if<glz::json_t::object_t>();
    }

    auto JsonValue::IsArray() const -> bool
    {
        auto node = Resolve(false);
        return node && static_cast<const glz::json_t*>(*node)->get_if<glz::json_t::array_t>();
    }

    auto JsonValue::AsNumber() const -> EngineResult<double>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());
        const auto* number = static_cast<const glz::json_t*>(*node)->get_if<double>();
        if (!number)
            return UnexpectedSerializationError("JSON value is not a number", "JsonValue");

        return *number;
    }

    auto JsonValue::AsBoolean() const -> EngineResult<bool>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());
        const auto* boolean = static_cast<const glz::json_t*>(*node)->get_if<bool>();
        if (!boolean)
            return UnexpectedSerializationError("JSON value is not a boolean", "JsonValue");

        return *boolean;
    }

    auto JsonValue::AsString() const -> EngineResult<String>
    {
        auto node = Resolve(false);
        if (!node)
            return UnexpectedSerializationError(node.error());
        const auto* string = static_cast<const glz::json_t*>(*node)->get_if<std::string>();
        if (!string)
            return UnexpectedSerializationError("JSON value is not a string", "JsonValue");

        return String(string->begin(), string->end());
    }
}
