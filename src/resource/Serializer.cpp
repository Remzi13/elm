#include "resource/Serializer.hpp"

#include "core/File.hpp"
#include "core/Std.hpp"

#include "resource/JsonValue.hpp"

#include "project/Project.hpp"
#include "resource/SerializationError.hpp"

#include "Scene/ComponentTypes.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace elm::resource {
    namespace {
        auto SetField(JsonValue& object, StringView key, JsonValue value) -> EngineResult<void>
        {
            return object.Set(key, std::move(value));
        }

        auto AddToArray(JsonValue& array, JsonValue value) -> EngineResult<void>
        {
            return array.Push(std::move(value));
        }

        template <typename Range>
        auto MakeArray(const Range& values) -> EngineResult<JsonValue>
        {
            JsonValue array { JsonValue::Type::Array };
            for (const auto value : values) {
                auto result = AddToArray(array, JsonValue(static_cast<double>(value)));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return array;
        }

        auto MakeMatrix(const std::array<float, 16>& values) -> EngineResult<JsonValue>
        {
            JsonValue matrix { JsonValue::Type::Array };
            for (size_t row = 0; row < 4; ++row) {
                auto rowArray = MakeArray(std::array {
                    values[row * 4],
                    values[row * 4 + 1],
                    values[row * 4 + 2],
                    values[row * 4 + 3] });
                if (!rowArray)
                    return UnexpectedSerializationError(rowArray.error());

                auto result = AddToArray(matrix, std::move(*rowArray));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return matrix;
        }

        auto MakeVector(const Vector3& vector) -> EngineResult<JsonValue>
        {
            return MakeArray(std::array { vector.x, vector.y, vector.z });
        }

        auto MakeModelFileStem(StringView name, size_t fallbackIndex) -> String
        {
            String stem;
            stem.reserve(name.size());
            for (const unsigned char character : name) {
                const bool invalid = character < 0x20 || character == 0x7f || character == '/' || character == '\\' || character == ':' || character == '*' || character == '?' || character == '"' || character == '<' || character == '>' || character == '|';
                stem.push_back(invalid ? '_' : static_cast<char>(character));
            }

            while (!stem.empty() && (stem.front() == '.' || stem.front() == ' '))
                stem.erase(stem.begin());
            while (!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
                stem.pop_back();
            if (stem.empty())
                stem = "model_" + std::to_string(fallbackIndex);

            String reservedCheck = stem;
            if (const auto extension = reservedCheck.find('.'); extension != String::npos)
                reservedCheck.resize(extension);
            for (char& character : reservedCheck)
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            const bool numberedDevice = reservedCheck.size() == 4 && (reservedCheck.compare(0, 3, "COM") == 0 || reservedCheck.compare(0, 3, "LPT") == 0) && reservedCheck[3] >= '1' && reservedCheck[3] <= '9';
            if (reservedCheck == "CON" || reservedCheck == "PRN" || reservedCheck == "AUX" || reservedCheck == "NUL" || numberedDevice)
                stem.insert(stem.begin(), '_');

            return stem;
        }

        auto GetField(const JsonValue& object, StringView key) -> EngineResult<JsonValue>
        {
            auto field = object[key];
            auto result = field.Exists();
            if (!result)
                return UnexpectedSerializationError(result.error());
            return field;
        }

        auto GetElement(const JsonValue& array, size_t index) -> EngineResult<JsonValue>
        {
            auto element = array.At(index);
            auto result = element.Exists();
            if (!result)
                return UnexpectedSerializationError(result.error());
            return element;
        }

        auto ReadNumber(const JsonValue& value, StringView description) -> EngineResult<double>
        {
            auto number = value.AsNumber();
            if (!number || !std::isfinite(*number))
                return UnexpectedSerializationError("Invalid numeric scene field: " + String(description), "Serializer");

            return *number;
        }

        template <typename T>
        auto ReadUnsigned(const JsonValue& value, StringView description) -> EngineResult<T>
        {
            auto number = ReadNumber(value, description);
            if (!number)
                return UnexpectedSerializationError(number.error());

            if (*number < 0.0 || *number > static_cast<double>(std::numeric_limits<T>::max()) || std::floor(*number) != *number)
                return UnexpectedSerializationError("Invalid integer scene field: " + String(description), "Serializer");

            return static_cast<T>(*number);
        }

        template <size_t N>
        auto ReadFloatArray(const JsonValue& value, StringView description) -> EngineResult<std::array<float, N>>
        {
            if (!value.IsArray())
                return UnexpectedSerializationError("Invalid array scene field: " + String(description), "Serializer");

            auto size = value.Size();
            if (!size || *size != N)
                return UnexpectedSerializationError("Invalid array scene field: " + String(description), "Serializer");

            std::array<float, N> result {};
            for (size_t index = 0; index < N; ++index) {
                auto element = GetElement(value, index);
                if (!element)
                    return UnexpectedSerializationError(element.error());
                auto number = ReadNumber(*element, description);
                if (!number)
                    return UnexpectedSerializationError(number.error());
                if (*number < -std::numeric_limits<float>::max() || *number > std::numeric_limits<float>::max())
                    return UnexpectedSerializationError("Scene number is outside the float range: " + String(description), "Serializer");

                result[index] = static_cast<float>(*number);
            }

            return result;
        }

        auto ReadMatrix(const JsonValue& value, StringView description) -> EngineResult<std::array<float, 16>>
        {
            if (!value.IsArray())
                return UnexpectedSerializationError("Invalid array scene field: " + String(description), "Serializer");

            auto size = value.Size();
            if (!size)
                return UnexpectedSerializationError(size.error());

            if (*size == 16)
                return ReadFloatArray<16>(value, description);

            if (*size != 4)
                return UnexpectedSerializationError("Invalid matrix scene field: " + String(description), "Serializer");

            std::array<float, 16> matrix {};
            for (size_t row = 0; row < 4; ++row) {
                auto rowValue = GetElement(value, row);
                if (!rowValue)
                    return UnexpectedSerializationError(rowValue.error());

                auto rowValues = ReadFloatArray<4>(*rowValue, description);
                if (!rowValues)
                    return UnexpectedSerializationError(rowValues.error());

                std::copy(rowValues->begin(), rowValues->end(), matrix.begin() + row * 4);
            }
            return matrix;
        }

        auto ReadVector3(const JsonValue& value, StringView description) -> EngineResult<Vector3>
        {
            auto components = ReadFloatArray<3>(value, description);
            if (!components)
                return UnexpectedSerializationError(components.error());

            return Vector3 { (*components)[0], (*components)[1], (*components)[2] };
        }

        auto ReadArrayField(const JsonValue& object, StringView key) -> EngineResult<JsonValue>
        {
            auto value = GetField(object, key);
            if (!value)
                return UnexpectedSerializationError(value.error());
            if (!value->IsArray())
                return UnexpectedSerializationError("Scene field is not an array: " + String(key), "Serializer");

            return std::move(*value);
        }

        auto ReadVector3Field(const JsonValue& object, StringView key) -> EngineResult<Vector3>
        {
            auto value = GetField(object, key);
            if (!value)
                return UnexpectedSerializationError(value.error());
            return ReadVector3(*value, key);
        }

        // ── Node components ─────────────────────────────────────────────────────
        // "components": { "Script": {...}, "RigidBody": {...}, "Collider": {...}, "Model": {...} } of a node.
        // Every field is written; a missing field reads as the component's default.

        /// The double printed as the float's shortest form: 0.1f is written as 0.1, not 0.10000000149011612,
        /// and still reads back as the same float.
        auto ShortNumber(float value) -> JsonValue
        {
            char buffer[32];
            double result = value;
            if (const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value); written.ec == std::errc {})
                std::from_chars(buffer, written.ptr, result);
            return JsonValue(result);
        }

        auto MakeShortArray(std::initializer_list<float> values) -> EngineResult<JsonValue>
        {
            JsonValue array { JsonValue::Type::Array };
            for (const float value : values) {
                auto result = AddToArray(array, ShortNumber(value));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return array;
        }

        auto MakeShortVector(const Vector3& vector) -> EngineResult<JsonValue> { return MakeShortArray({ vector.x, vector.y, vector.z }); }

        auto SetResult(JsonValue& object, StringView key, EngineResult<JsonValue> value) -> EngineResult<void>
        {
            if (!value)
                return UnexpectedSerializationError(value.error());
            return SetField(object, key, std::move(*value));
        }

        auto SetFields(JsonValue& object, std::initializer_list<std::pair<StringView, JsonValue>> fields) -> EngineResult<void>
        {
            for (const auto& [key, value] : fields) {
                auto result = SetField(object, key, value);
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return {};
        }

        /// A script prop: boolean, number, string, [x, y, z] for a Vector3, [x, y, z, w] for a Vector4,
        /// { "entity": node index } for an Entity (null when it refers to no node of the scene).
        auto MakeScriptValue(const ScriptValue& value, const Scene& scene) -> EngineResult<JsonValue>
        {
            return std::visit([&]<typename V>(const V& item) -> EngineResult<JsonValue> {
                if constexpr (std::is_same_v<V, bool>) {
                    return JsonValue(item);
                } else if constexpr (std::is_same_v<V, int64_t> || std::is_same_v<V, double>) {
                    return JsonValue(static_cast<double>(item));
                } else if constexpr (std::is_same_v<V, String>) {
                    return JsonValue(StringView(item));
                } else if constexpr (std::is_same_v<V, Vector3>) {
                    return MakeShortVector(item);
                } else if constexpr (std::is_same_v<V, Vector4>) {
                    return MakeShortArray({ item.x, item.y, item.z, item.w });
                } else {
                    JsonValue reference { JsonValue::Type::Object };
                    const size_t index = scene.IndexOf(item);
                    auto result = SetField(reference, "entity", index == Scene::NoNode ? JsonValue() : JsonValue(static_cast<double>(index)));
                    if (!result)
                        return UnexpectedSerializationError(result.error());
                    return reference;
                }
            },
                value);
        }

        auto MakeScript(const ScriptComponent& script, const Scene& scene) -> EngineResult<JsonValue>
        {
            JsonValue props { JsonValue::Type::Object };
            for (const auto& [name, value] : script.props) {
                auto result = SetResult(props, name, MakeScriptValue(value, scene));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            JsonValue serialized { JsonValue::Type::Object };
            auto result = SetFields(serialized, { { "module", JsonValue(StringView(script.module)) }, { "props", std::move(props) } });
            if (!result)
                return UnexpectedSerializationError(result.error());
            return serialized;
        }

        auto MakeRigidBody(const RigidBody& body) -> EngineResult<JsonValue>
        {
            JsonValue serialized { JsonValue::Type::Object };
            auto result = SetFields(serialized,
                {
                    { "type", JsonValue(BodyTypeName(body.type)) },
                    { "mass", ShortNumber(body.mass) },
                    { "friction", ShortNumber(body.friction) },
                    { "restitution", ShortNumber(body.restitution) },
                    { "linearDamping", ShortNumber(body.linearDamping) },
                    { "angularDamping", ShortNumber(body.angularDamping) },
                    { "gravityScale", ShortNumber(body.gravityScale) },
                });
            if (!result)
                return UnexpectedSerializationError(result.error());
            return serialized;
        }

        auto MakeCollider(const Collider& collider) -> EngineResult<JsonValue>
        {
            JsonValue serialized { JsonValue::Type::Object };
            auto result = SetFields(serialized,
                {
                    { "shape", JsonValue(ColliderShapeName(collider.shape)) },
                    { "radius", ShortNumber(collider.radius) },
                    { "halfHeight", ShortNumber(collider.halfHeight) },
                    { "trigger", JsonValue(collider.trigger) },
                });
            if (result)
                result = SetResult(serialized, "halfExtents", MakeShortVector(collider.halfExtents));
            if (result)
                result = SetResult(serialized, "offset", MakeShortVector(collider.offset));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return serialized;
        }

        /// Index of every saved model in the file's "models".
        using ModelIndices = UnorderedMap<ModelHandler, size_t>;

        /// The model as its index in "models" (none for a component without a model), the tint and the user's flag.
        auto MakeModel(const ModelComponent& component, const ModelIndices& modelIndices) -> EngineResult<JsonValue>
        {
            JsonValue serialized { JsonValue::Type::Object };
            const Vector4& color = component.color;
            auto result = SetFields(serialized, { { "visible", JsonValue(component.visible) } });
            if (result)
                result = SetResult(serialized, "color", MakeShortArray({ color.x, color.y, color.z, color.w }));
            if (const auto model = modelIndices.find(component.model); result && model != modelIndices.end())
                result = SetField(serialized, "modelIndex", JsonValue(static_cast<double>(model->second)));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return serialized;
        }

        /// The node's component of type T as make writes it, nullopt when the node has none.
        template <SceneComponent T, typename Make>
        auto MakeComponentOf(const Scene& scene, NodeId node, Make&& make) -> EngineResult<std::optional<JsonValue>>
        {
            const auto* store = scene.FindComponents<T>();
            const T* component = store ? store->Find(node) : nullptr;
            if (!component)
                return std::optional<JsonValue> {};
            auto serialized = make(*component);
            if (!serialized)
                return UnexpectedSerializationError(serialized.error());
            return std::optional<JsonValue>(std::move(*serialized));
        }

        auto MakeComponent(const Scene& scene, NodeId node, ComponentType type, const ModelIndices& modelIndices)
            -> EngineResult<std::optional<JsonValue>>
        {
            switch (type) {
            case ComponentType::Script:
                return MakeComponentOf<ScriptComponent>(scene, node, [&](const ScriptComponent& script) { return MakeScript(script, scene); });
            case ComponentType::RigidBody:
                return MakeComponentOf<RigidBody>(scene, node, MakeRigidBody);
            case ComponentType::Collider:
                return MakeComponentOf<Collider>(scene, node, MakeCollider);
            case ComponentType::Model:
                return MakeComponentOf<ModelComponent>(scene, node, [&](const ModelComponent& model) { return MakeModel(model, modelIndices); });
            }
            return UnexpectedSerializationError("Scene node has an unknown component type", "Serializer");
        }

        /// The components of a node under their names, nullopt when it has none.
        auto MakeComponents(const Scene& scene, NodeId node, const ModelIndices& modelIndices) -> EngineResult<std::optional<JsonValue>>
        {
            JsonValue components { JsonValue::Type::Object };
            bool empty = true;
            for (const ComponentType type : AllComponentTypes) {
                auto component = MakeComponent(scene, node, type, modelIndices);
                if (!component)
                    return UnexpectedSerializationError(component.error());
                if (!*component)
                    continue;
                auto result = SetField(components, ComponentTypeName(type), std::move(**component));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                empty = false;
            }
            if (empty)
                return std::optional<JsonValue> {};
            return std::optional<JsonValue>(std::move(components));
        }

        /// A node with its parent as an index in the file and its components. The world transform is not
        /// written: it follows from the local ones.
        auto MakeNode(const Scene& scene, const Scene::Node& node, const ModelIndices& modelIndices) -> EngineResult<JsonValue>
        {
            JsonValue serialized { JsonValue::Type::Object };
            auto result = SetField(serialized, "name", JsonValue(node.name.c_str()));
            if (result)
                result = SetResult(serialized, "localTransform", MakeMatrix(node.localTransform.m));
            if (const size_t parentIndex = scene.IndexOf(node.parent); result && parentIndex != Scene::NoNode)
                result = SetField(serialized, "parentIndex", JsonValue(static_cast<double>(parentIndex)));
            auto components = MakeComponents(scene, node.id, modelIndices);
            if (!components)
                return UnexpectedSerializationError(components.error());
            if (result && *components)
                result = SetField(serialized, "components", std::move(**components));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return serialized;
        }

        auto ReadFloat(const JsonValue& value, StringView description) -> EngineResult<float>
        {
            auto number = ReadNumber(value, description);
            if (!number)
                return UnexpectedSerializationError(number.error());
            if (std::abs(*number) > std::numeric_limits<float>::max())
                return UnexpectedSerializationError("Scene number is outside the float range: " + String(description), "Serializer");
            return static_cast<float>(*number);
        }

        // Read a field into target when the object has it, target keeps its default otherwise

        auto ReadFloatInto(const JsonValue& object, StringView key, float& target) -> EngineResult<void>
        {
            const auto field = object.Find(key);
            if (!field)
                return {};
            auto value = ReadFloat(*field, key);
            if (!value)
                return UnexpectedSerializationError(value.error());
            target = *value;
            return {};
        }

        auto ReadVector3Into(const JsonValue& object, StringView key, Vector3& target) -> EngineResult<void>
        {
            const auto field = object.Find(key);
            if (!field)
                return {};
            auto value = ReadVector3(*field, key);
            if (!value)
                return UnexpectedSerializationError(value.error());
            target = *value;
            return {};
        }

        auto ReadVector4Into(const JsonValue& object, StringView key, Vector4& target) -> EngineResult<void>
        {
            const auto field = object.Find(key);
            if (!field)
                return {};
            auto values = ReadFloatArray<4>(*field, key);
            if (!values)
                return UnexpectedSerializationError(values.error());
            target = Vector4 { (*values)[0], (*values)[1], (*values)[2], (*values)[3] };
            return {};
        }

        auto ReadBoolInto(const JsonValue& object, StringView key, bool& target) -> EngineResult<void>
        {
            const auto field = object.Find(key);
            if (!field)
                return {};
            if (field->GetKind() != JsonValue::Kind::Boolean)
                return UnexpectedSerializationError("Invalid boolean scene field: " + String(key), "Serializer");
            target = *field->AsBoolean();
            return {};
        }

        /// A name field of an enum, through fromName (BodyTypeFromName, ColliderShapeFromName).
        template <typename E>
        auto ReadEnumInto(const JsonValue& object, StringView key, std::optional<E> (*fromName)(StringView), E& target) -> EngineResult<void>
        {
            const auto field = object.Find(key);
            if (!field)
                return {};
            std::optional<E> value;
            if (field->GetKind() == JsonValue::Kind::String)
                value = fromName(*field->AsString());
            if (!value)
                return UnexpectedSerializationError("Invalid scene field: " + String(key), "Serializer");
            target = *value;
            return {};
        }

        /// See MakeScriptValue. JSON has one kind of number: a whole number comes back as an integer.
        auto ReadScriptValue(const JsonValue& value, size_t nodeCount, StringView description) -> EngineResult<ScriptValue>
        {
            switch (value.GetKind()) {
            case JsonValue::Kind::Boolean:
                return ScriptValue(*value.AsBoolean());
            case JsonValue::Kind::Number: {
                auto number = ReadNumber(value, description);
                if (!number)
                    return UnexpectedSerializationError(number.error());
                constexpr double LargestExactInteger = 9007199254740992.0;
                if (std::floor(*number) == *number && std::abs(*number) <= LargestExactInteger)
                    return ScriptValue(static_cast<int64_t>(*number));
                return ScriptValue(*number);
            }
            case JsonValue::Kind::String:
                return ScriptValue(*value.AsString());
            case JsonValue::Kind::Array: {
                const auto size = value.Size();
                if (size && *size == 3) {
                    auto vector = ReadVector3(value, description);
                    if (!vector)
                        return UnexpectedSerializationError(vector.error());
                    return ScriptValue(*vector);
                }
                if (size && *size == 4) {
                    auto components = ReadFloatArray<4>(value, description);
                    if (!components)
                        return UnexpectedSerializationError(components.error());
                    return ScriptValue(Vector4 { (*components)[0], (*components)[1], (*components)[2], (*components)[3] });
                }
                break;
            }
            case JsonValue::Kind::Object:
                if (const auto reference = value.Find("entity")) {
                    if (reference->GetKind() == JsonValue::Kind::Null)
                        return ScriptValue(NodeId {});
                    auto index = ReadUnsigned<size_t>(*reference, description);
                    if (!index || *index >= nodeCount)
                        return UnexpectedSerializationError("Script prop refers to a missing node: " + String(description), "Serializer");
                    return ScriptValue(SceneAsset::NodeRef(*index));
                }
                break;
            case JsonValue::Kind::Null:
                break;
            }
            return UnexpectedSerializationError("Invalid script prop: " + String(description), "Serializer");
        }

        auto ReadScript(const JsonValue& serialized, size_t nodeCount) -> EngineResult<ScriptComponent>
        {
            ScriptComponent script;
            if (const auto module = serialized.Find("module"); module && module->GetKind() == JsonValue::Kind::String)
                script.module = *module->AsString();
            if (script.module.empty())
                return UnexpectedSerializationError("Script component needs a module name", "Serializer");

            if (const auto props = serialized.Find("props")) {
                if (!props->IsObject())
                    return UnexpectedSerializationError("Script props must be an object", "Serializer");
                // Keys come sorted, as CheckScriptProps keeps them
                for (const auto& name : props->Keys()) {
                    auto value = ReadScriptValue((*props)[name], nodeCount, "Script.props." + name);
                    if (!value)
                        return UnexpectedSerializationError(value.error());
                    script.props.emplace_back(name, std::move(*value));
                }
            }
            return script;
        }

        auto ReadRigidBody(const JsonValue& serialized) -> EngineResult<RigidBody>
        {
            RigidBody body;
            for (auto result : {
                     ReadEnumInto(serialized, "type", &BodyTypeFromName, body.type),
                     ReadFloatInto(serialized, "mass", body.mass),
                     ReadFloatInto(serialized, "friction", body.friction),
                     ReadFloatInto(serialized, "restitution", body.restitution),
                     ReadFloatInto(serialized, "linearDamping", body.linearDamping),
                     ReadFloatInto(serialized, "angularDamping", body.angularDamping),
                     ReadFloatInto(serialized, "gravityScale", body.gravityScale),
                 }) {
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return body;
        }

        auto ReadCollider(const JsonValue& serialized) -> EngineResult<Collider>
        {
            Collider collider;
            for (auto result : {
                     ReadEnumInto(serialized, "shape", &ColliderShapeFromName, collider.shape),
                     ReadVector3Into(serialized, "halfExtents", collider.halfExtents),
                     ReadFloatInto(serialized, "radius", collider.radius),
                     ReadFloatInto(serialized, "halfHeight", collider.halfHeight),
                     ReadVector3Into(serialized, "offset", collider.offset),
                     ReadBoolInto(serialized, "trigger", collider.trigger),
                 }) {
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return collider;
        }

        /// The model is SceneAsset::ModelRef of its index in "models"; modelCount bounds it.
        auto ReadModel(const JsonValue& serialized, size_t modelCount) -> EngineResult<ModelComponent>
        {
            ModelComponent model;
            for (auto result : { ReadVector4Into(serialized, "color", model.color), ReadBoolInto(serialized, "visible", model.visible) }) {
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            if (const auto modelField = serialized.Find("modelIndex")) {
                auto modelIndex = ReadUnsigned<size_t>(*modelField, "Model.modelIndex");
                if (!modelIndex || *modelIndex >= modelCount)
                    return UnexpectedSerializationError("Scene node refers to a missing model", "Serializer");
                model.model = SceneAsset::ModelRef(*modelIndex);
            }
            return model;
        }

        template <SceneComponent T>
        auto StoreComponent(EngineResult<T> component, NodeId node, ComponentStores& components) -> EngineResult<void>
        {
            if (!component)
                return UnexpectedSerializationError(component.error());
            components.Components<T>().Emplace(node, std::move(*component));
            return {};
        }

        /// How many nodes and models a scene file has: Entity props and Model components refer to them by index.
        struct SceneFileCounts {
            size_t nodeCount { 0 };
            size_t modelCount { 0 };
        };

        /// Reads the components of an asset node into the asset's stores under the node's key.
        auto ReadComponents(const JsonValue& serialized, SceneFileCounts counts, NodeId node, ComponentStores& components)
            -> EngineResult<void>
        {
            if (!serialized.IsObject())
                return UnexpectedSerializationError("Scene node components must be an object", "Serializer");
            for (const auto& name : serialized.Keys()) {
                const JsonValue component = serialized[name];
                if (!component.IsObject())
                    return UnexpectedSerializationError("Scene node component must be an object: " + name, "Serializer");
                const auto type = ComponentTypeFromName(name);
                if (!type)
                    return UnexpectedSerializationError("Scene node has an unknown component: " + name, "Serializer");
                EngineResult<void> stored;
                switch (*type) {
                case ComponentType::Script:
                    stored = StoreComponent(ReadScript(component, counts.nodeCount), node, components);
                    break;
                case ComponentType::RigidBody:
                    stored = StoreComponent(ReadRigidBody(component), node, components);
                    break;
                case ComponentType::Collider:
                    stored = StoreComponent(ReadCollider(component), node, components);
                    break;
                case ComponentType::Model:
                    stored = StoreComponent(ReadModel(component, counts.modelCount), node, components);
                    break;
                }
                if (!stored)
                    return UnexpectedSerializationError(stored.error());
            }
            return {};
        }

        /// A node of a version 4 file or later without its components: name, parent and local transform.
        auto ReadNode(const JsonValue& serialized, size_t nodeIndex, size_t nodeCount) -> EngineResult<SceneAsset::Node>
        {
            if (!serialized.IsObject())
                return UnexpectedSerializationError("Scene node must be an object", "Serializer");
            auto localField = GetField(serialized, "localTransform");
            if (!localField)
                return UnexpectedSerializationError("Scene node is missing required fields", "Serializer");
            auto localTransform = ReadMatrix(*localField, "node.localTransform");
            if (!localTransform)
                return UnexpectedSerializationError(localTransform.error());

            SceneAsset::Node node;
            node.localTransform.m = *localTransform;
            if (const auto parentField = serialized.Find("parentIndex")) {
                auto parentIndex = ReadUnsigned<size_t>(*parentField, "node.parentIndex");
                if (!parentIndex || *parentIndex >= nodeCount || *parentIndex == nodeIndex)
                    return UnexpectedSerializationError("Scene node has an invalid parent index", "Serializer");
                node.parentIndex = *parentIndex;
            }
            if (const auto nameField = serialized.Find("name")) {
                auto name = nameField->AsString();
                if (!name)
                    return UnexpectedSerializationError(name.error());
                node.name = std::move(*name);
            }
            return node;
        }

        /// Versions 4 and 5 keep the model, color and visible on the node: a node with a model gets them as
        /// its Model component, without a model they meant nothing. The world transform is checked and
        /// dropped, it follows from the local ones.
        auto ReadNodeModel(const JsonValue& serialized, size_t modelCount) -> EngineResult<std::optional<ModelComponent>>
        {
            auto worldField = GetField(serialized, "worldTransform");
            auto visibleField = GetField(serialized, "visible");
            auto colorField = GetField(serialized, "color");
            if (!worldField || !visibleField || !colorField)
                return UnexpectedSerializationError("Scene node is missing required fields", "Serializer");
            auto worldTransform = ReadMatrix(*worldField, "node.worldTransform");
            auto visible = visibleField->AsBoolean();
            auto color = ReadFloatArray<4>(*colorField, "node.color");
            if (!worldTransform || !visible || !color)
                return UnexpectedSerializationError("Invalid scene node data", "Serializer");

            const auto modelField = serialized.Find("modelIndex");
            if (!modelField)
                return std::optional<ModelComponent> {};
            auto modelIndex = ReadUnsigned<size_t>(*modelField, "node.modelIndex");
            if (!modelIndex || *modelIndex >= modelCount)
                return UnexpectedSerializationError("Scene node refers to a missing model", "Serializer");
            return std::optional<ModelComponent>(ModelComponent {
                .model = SceneAsset::ModelRef(*modelIndex),
                .color = Vector4 { (*color)[0], (*color)[1], (*color)[2], (*color)[3] },
                .visible = *visible,
            });
        }

        // A .model file (docs/scene/Файл сцены.md): these values, a JSON table of the submeshes, then the bytes the
        // renderer copies to the GPU. Geometry is converted once, when a glTF file is imported.
        constexpr StringView ModelFileSignature = "ELMM";
        constexpr uint32_t ModelFileVersion = 3;
        constexpr size_t ModelFileVersionOffset = ModelFileSignature.size();
        constexpr size_t ModelFileHeaderSizeOffset = ModelFileVersionOffset + sizeof(uint32_t);
        constexpr size_t ModelFileHeaderOffset = ModelFileHeaderSizeOffset + sizeof(uint32_t);
        constexpr size_t ModelFileGeometryAlignment = 16;

        static_assert(std::endian::native == std::endian::little, "A .model file is little-endian and read without conversion");
        static_assert(std::is_trivially_copyable_v<Vertex> && sizeof(Vertex) == 32, "A change of Vertex is a new version of the .model file");
        static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, u) == 24 && offsetof(Vertex, v) == 28,
            "A change of Vertex is a new version of the .model file");

        auto ModelFileError(const Path& file, const char* problem) -> std::unexpected<EngineError>
        {
            return UnexpectedSerializationError("Model file " + file.string() + ": " + problem, "Serializer");
        }

        bool IsFinite(const Vector3& vector)
        {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        }

        bool IsFinite(const Vertex& vertex)
        {
            return IsFinite(vertex.position) && IsFinite(vertex.normal) && std::isfinite(vertex.u) && std::isfinite(vertex.v);
        }

        /// JSON cannot hold NaN or infinity, a binary file can: what reaches the renderer and culling stays finite.
        bool HasFiniteGeometry(const MeshData& meshData)
        {
            return IsFinite(meshData.localBounds.minBounds) && IsFinite(meshData.localBounds.maxBounds)
                && std::ranges::all_of(meshData.vertices, [](const Vertex& vertex) { return IsFinite(vertex); });
        }

        auto MakeSubmeshEntry(const Model::Submesh& submesh, uint64_t firstVertex, uint64_t firstIndex) -> EngineResult<JsonValue>
        {
            const MeshData& meshData = *submesh.data;
            auto minBounds = MakeVector(meshData.localBounds.minBounds);
            auto maxBounds = MakeVector(meshData.localBounds.maxBounds);
            auto color = MakeArray(std::array { submesh.color.x, submesh.color.y, submesh.color.z, submesh.color.w });
            if (!minBounds || !maxBounds || !color)
                return UnexpectedSerializationError("Failed to write a submesh of a model", "Serializer");

            JsonValue entry { JsonValue::Type::Object };
            for (const auto& result : {
                     SetField(entry, "name", JsonValue(submesh.name.c_str())),
                     SetField(entry, "color", std::move(*color)),
                     SetField(entry, "minBounds", std::move(*minBounds)),
                     SetField(entry, "maxBounds", std::move(*maxBounds)),
                     SetField(entry, "firstVertex", JsonValue(firstVertex)),
                     SetField(entry, "vertexCount", JsonValue(meshData.vertices.size())),
                     SetField(entry, "firstIndex", JsonValue(firstIndex)),
                     SetField(entry, "indexCount", JsonValue(meshData.indices.size())),
                 }) {
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return entry;
        }

        auto MakeModelHeader(const Model& model) -> EngineResult<String>
        {
            uint64_t vertexCount = 0;
            uint64_t indexCount = 0;
            JsonValue submeshes { JsonValue::Type::Array };
            for (const auto& submesh : model.submeshes) {
                if (!submesh.data)
                    return UnexpectedSerializationError("Model contains a submesh without geometry", "Serializer");
                const Vector4& color = submesh.color;
                if (!HasFiniteGeometry(*submesh.data) || !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) || !std::isfinite(color.w))
                    return UnexpectedSerializationError("Model contains a value that is not finite: " + model.name, "Serializer");
                auto entry = MakeSubmeshEntry(submesh, vertexCount, indexCount);
                if (!entry)
                    return UnexpectedSerializationError(entry.error());
                if (auto added = AddToArray(submeshes, std::move(*entry)); !added)
                    return UnexpectedSerializationError(added.error());
                vertexCount += submesh.data->vertices.size();
                indexCount += submesh.data->indices.size();
            }
            if (vertexCount > std::numeric_limits<uint32_t>::max() || indexCount > std::numeric_limits<uint32_t>::max())
                return UnexpectedSerializationError("Model has too much geometry for a file: " + model.name, "Serializer");

            JsonValue document { JsonValue::Type::Object };
            for (const auto& result : {
                     SetField(document, "name", JsonValue(model.name.c_str())),
                     SetField(document, "vertexCount", JsonValue(vertexCount)),
                     SetField(document, "indexCount", JsonValue(indexCount)),
                     SetField(document, "submeshes", std::move(submeshes)),
                 }) {
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return document.ToString();
        }

        auto EncodeModel(const Model& model) -> EngineResult<BinaryBlock>
        {
            auto header = MakeModelHeader(model);
            if (!header)
                return UnexpectedSerializationError(header.error());
            if (header->size() > std::numeric_limits<uint32_t>::max())
                return UnexpectedSerializationError("Model has too many submeshes for a file: " + model.name, "Serializer");

            size_t geometrySize = 0;
            for (const auto& submesh : model.submeshes)
                geometrySize += submesh.data->vertices.size() * sizeof(Vertex) + submesh.data->indices.size() * sizeof(uint32_t);

            BinaryBlock block;
            block.Reserve(ModelFileHeaderOffset + header->size() + ModelFileGeometryAlignment + geometrySize);
            block.AppendText(ModelFileSignature);
            block.Append(ModelFileVersion);
            block.Append(static_cast<uint32_t>(header->size()));
            block.AppendText(*header);
            block.AlignTo(ModelFileGeometryAlignment);
            for (const auto& submesh : model.submeshes)
                block.AppendArray<Vertex>(submesh.data->vertices);
            for (const auto& submesh : model.submeshes)
                block.AppendArray<uint32_t>(submesh.data->indices);
            return block;
        }

        struct ModelFileHeader {
            JsonValue document;
            uint64_t geometryOffset { 0 };
        };

        /// A model file while it is read: where its two geometry blocks lie and how much they hold.
        struct ModelFileView {
            const BinaryBlock& block;
            const Path& file;
            uint64_t vertexCount { 0 };
            uint64_t indexCount { 0 };
            uint64_t verticesOffset { 0 };
            uint64_t indicesOffset { 0 };
        };

        auto ReadModelHeader(const BinaryBlock& block, const Path& file) -> EngineResult<ModelFileHeader>
        {
            if (block.ReadText(0, ModelFileSignature.size()) != ModelFileSignature)
                return ModelFileError(file, "old or unknown format, import its glTF file again");
            if (block.Read<uint32_t>(ModelFileVersionOffset) != ModelFileVersion)
                return ModelFileError(file, "unsupported version");

            const auto headerSize = block.Read<uint32_t>(ModelFileHeaderSizeOffset);
            const auto text = headerSize ? block.ReadText(ModelFileHeaderOffset, *headerSize) : std::nullopt;
            if (!text)
                return ModelFileError(file, "the header is cut off");
            ModelFileHeader header;
            if (auto parsed = header.document.Parse(*text); !parsed || !header.document.IsObject())
                return ModelFileError(file, "the header is not valid");

            const uint64_t headerEnd = uint64_t { ModelFileHeaderOffset } + *headerSize;
            const uint64_t remainder = headerEnd % ModelFileGeometryAlignment;
            header.geometryOffset = remainder == 0 ? headerEnd : headerEnd + ModelFileGeometryAlignment - remainder;
            return header;
        }

        auto ReadCountField(const JsonValue& object, StringView key) -> EngineResult<uint64_t>
        {
            auto field = GetField(object, key);
            if (!field)
                return UnexpectedSerializationError(field.error());
            auto count = ReadUnsigned<uint32_t>(*field, key);
            if (!count)
                return UnexpectedSerializationError(count.error());
            return uint64_t { *count };
        }

        auto ReadTextField(const JsonValue& object, StringView key) -> EngineResult<String>
        {
            auto field = GetField(object, key);
            if (!field)
                return UnexpectedSerializationError(field.error());
            return field->AsString();
        }

        /// Geometry of the header's submesh. Counts are checked against the file first: a damaged file cannot ask for
        /// more memory than its own size.
        auto ReadSubmeshGeometry(const ModelFileView& view, const JsonValue& entry) -> EngineResult<MeshData>
        {
            const auto firstVertex = ReadCountField(entry, "firstVertex");
            const auto vertexCount = ReadCountField(entry, "vertexCount");
            const auto firstIndex = ReadCountField(entry, "firstIndex");
            const auto indexCount = ReadCountField(entry, "indexCount");
            const auto minBounds = ReadVector3Field(entry, "minBounds");
            const auto maxBounds = ReadVector3Field(entry, "maxBounds");
            if (!firstVertex || !vertexCount || !firstIndex || !indexCount || !minBounds || !maxBounds)
                return ModelFileError(view.file, "a submesh of the header is not valid");
            if (*firstVertex + *vertexCount > view.vertexCount || *firstIndex + *indexCount > view.indexCount)
                return ModelFileError(view.file, "a submesh lies outside of the geometry");

            MeshData meshData;
            meshData.localBounds = AABB { *minBounds, *maxBounds };
            meshData.vertices.resize(*vertexCount);
            meshData.indices.resize(*indexCount);
            if (!view.block.ReadArray<Vertex>(view.verticesOffset + *firstVertex * sizeof(Vertex), meshData.vertices)
                || !view.block.ReadArray<uint32_t>(view.indicesOffset + *firstIndex * sizeof(uint32_t), meshData.indices))
                return ModelFileError(view.file, "a submesh lies outside of the file");

            if (!std::ranges::all_of(meshData.indices, [&](uint32_t index) { return index < meshData.vertices.size(); }))
                return ModelFileError(view.file, "a submesh contains an out-of-range vertex index");
            if (!HasFiniteGeometry(meshData))
                return ModelFileError(view.file, "a submesh contains a value that is not finite");
            return meshData;
        }

        auto AddSubmeshOfFile(const ModelFileView& view, const JsonValue& entry, Model& model) -> EngineResult<void>
        {
            auto meshData = ReadSubmeshGeometry(view, entry);
            if (!meshData)
                return UnexpectedSerializationError(meshData.error());
            auto name = ReadTextField(entry, "name");
            auto colorField = GetField(entry, "color");
            const auto color = colorField ? ReadFloatArray<4>(*colorField, "submesh.color") : std::unexpected(colorField.error());
            if (!name || !color)
                return ModelFileError(view.file, "a submesh of the header is not valid");
            model.AddSubmesh(std::move(*meshData), Vector4 { (*color)[0], (*color)[1], (*color)[2], (*color)[3] }, std::move(*name));
            return {};
        }

        auto DecodeModel(const BinaryBlock& block, const Path& file) -> EngineResult<Model>
        {
            auto header = ReadModelHeader(block, file);
            if (!header)
                return UnexpectedSerializationError(header.error());
            auto name = ReadTextField(header->document, "name");
            const auto vertexCount = ReadCountField(header->document, "vertexCount");
            const auto indexCount = ReadCountField(header->document, "indexCount");
            const auto submeshes = ReadArrayField(header->document, "submeshes");
            const auto submeshCount = submeshes ? submeshes->Size() : std::unexpected(submeshes.error());
            if (!name || !vertexCount || !indexCount || !submeshCount)
                return ModelFileError(file, "the header is not valid");

            const uint64_t indicesOffset = header->geometryOffset + *vertexCount * sizeof(Vertex);
            if (block.Size() != indicesOffset + *indexCount * sizeof(uint32_t))
                return ModelFileError(file, "the size does not match the header");

            const ModelFileView view { block, file, *vertexCount, *indexCount, header->geometryOffset, indicesOffset };
            Model model(std::move(*name));
            for (size_t index = 0; index < *submeshCount; ++index) {
                auto entry = GetElement(*submeshes, index);
                if (!entry)
                    return UnexpectedSerializationError(entry.error());
                if (auto added = AddSubmeshOfFile(view, *entry, model); !added)
                    return UnexpectedSerializationError(added.error());
            }
            return model;
        }

        /// The models of the scene's Model components, each once, in the order of the nodes.
        auto UsedModels(const Scene& scene, const ModelCatalog& models) -> Vector<ModelHandler>
        {
            Vector<ModelHandler> used;
            const auto* modelComponents = scene.FindComponents<ModelComponent>();
            for (const auto& node : scene.Nodes()) {
                const ModelComponent* component = modelComponents ? modelComponents->Find(node.id) : nullptr;
                if (component && models.Get(component->model)
                    && std::find(used.begin(), used.end(), component->model) == used.end())
                    used.push_back(component->model);
            }
            return used;
        }

        /// models/<stem>.model, with a number added until `usable` takes the file.
        template <std::predicate<const Path&> Usable>
        auto FindModelFile(const String& stem, const Path& assetsFolder, Usable usable) -> EngineResult<project::AssetPath>
        {
            const String folder = String(project::Project::ModelsFolder) + "/";
            String text = folder + stem + ".model";
            for (size_t suffix = 1; !usable(assetsFolder / text); ++suffix)
                text = folder + stem + "_" + std::to_string(suffix).c_str() + ".model";
            auto file = project::AssetPath::FromString(text);
            if (!file)
                return UnexpectedSerializationError("Cannot make a model file name of " + stem, "Serializer");
            return *file;
        }

        /// The file of a model a scene references: from version 7 by its asset path, before relative to the scene file.
        auto ModelFileOf(StringView reference, uint32_t formatVersion, const Path& sceneFile, const Path& assetsFolder)
            -> EngineResult<Path>
        {
            if (formatVersion >= 7) {
                const auto asset = project::AssetPath::FromString(reference);
                if (!asset)
                    return UnexpectedSerializationError("Scene model path is not in the assets folder: " + String(reference), "Serializer");
                return asset->In(assetsFolder);
            }
            const Path relative(reference);
            if (relative.Empty() || relative.IsAbsolute())
                return UnexpectedSerializationError("Scene model path must be a non-empty relative path", "Serializer");
            return sceneFile.Parent() / relative;
        }

        auto ReadProjectString(const JsonValue& value, StringView description) -> EngineResult<String>
        {
            if (value.GetKind() != JsonValue::Kind::String)
                return UnexpectedSerializationError("Project field must be a string: " + String(description), "Serializer");
            auto text = value.AsString();
            if (!text || text->empty())
                return UnexpectedSerializationError("Project field must not be empty: " + String(description), "Serializer");
            return *text;
        }

        auto DeserializeProjectEntry(const JsonValue& entry) -> EngineResult<project::ProjectEntry>
        {
            project::ProjectEntry result;
            if (!entry.IsObject())
                return UnexpectedSerializationError("Project field must be an object: entry", "Serializer");
            if (const auto startupScene = entry.Find("startupScene")) {
                auto text = ReadProjectString(*startupScene, "entry.startupScene");
                if (!text)
                    return UnexpectedSerializationError(text.error());
                result.startupScene = project::AssetPath::FromString(*text);
                if (!result.startupScene)
                    return UnexpectedSerializationError("entry.startupScene is not a path in the assets folder: " + *text, "Serializer");
            }
            if (const auto mainModule = entry.Find("mainModule")) {
                auto name = ReadProjectString(*mainModule, "entry.mainModule");
                if (!name)
                    return UnexpectedSerializationError(name.error());
                result.mainModule = std::move(*name);
            }
            if (!result.startupScene && result.mainModule.empty())
                return UnexpectedSerializationError("Project entry needs startupScene or mainModule", "Serializer");
            return result;
        }

    } // namespace

    auto Serializer::SaveModel(const Model& model, const Path& path) -> EngineResult<void>
    {
        const auto block = EncodeModel(model);
        if (!block)
            return UnexpectedSerializationError(block.error());
        std::error_code folderError;
        FileSystem::CreateDirectories(path.Parent(), folderError);
        File file;
        if (auto opened = file.Open(path, FileMode::Write); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        if (auto written = file.Write(*block); !written)
            return UnexpectedSerializationError(written.error().message, "Serializer");
        if (auto closed = file.Close(); !closed)
            return UnexpectedSerializationError(closed.error().message, "Serializer");
        return {};
    }

    auto Serializer::LoadModel(const Path& path) -> EngineResult<Model>
    {
        File file;
        if (auto opened = file.Open(path, FileMode::Read); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        const auto block = file.ReadAll();
        if (!block)
            return UnexpectedSerializationError(block.error().message, "Serializer");
        return DecodeModel(*block, path);
    }

    auto Serializer::SaveNewModel(const Model& model, const Path& assetsFolder) -> EngineResult<Path>
    {
        const auto asset = FindModelFile(MakeModelFileStem(model.name, 0), assetsFolder,
            [](const Path& file) { return !FileSystem::Exists(file); });
        if (!asset)
            return UnexpectedSerializationError(asset.error());
        Path file = asset->In(assetsFolder);
        if (auto written = SaveModel(model, file); !written)
            return UnexpectedSerializationError(written.error());
        return file;
    }

    auto Serializer::SaveModelOnce(const Model& model, const Path& assetsFolder) -> EngineResult<Path>
    {
        const auto block = EncodeModel(model);
        if (!block)
            return UnexpectedSerializationError(block.error());

        const auto asset = FindModelFile(MakeModelFileStem(model.name, 0), assetsFolder, [&](const Path& file) {
            if (!FileSystem::Exists(file))
                return true;
            File existing;
            if (!existing.Open(file, FileMode::Read))
                return false;
            const auto contents = existing.ReadAll();
            return contents && *contents == *block;
        });
        if (!asset)
            return UnexpectedSerializationError(asset.error());
        Path file = asset->In(assetsFolder);
        if (FileSystem::Exists(file))
            return file;
        std::error_code folderError;
        FileSystem::CreateDirectories(file.Parent(), folderError);
        File modelFile;
        if (auto opened = modelFile.Open(file, FileMode::Write); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        if (auto written = modelFile.Write(*block); !written)
            return UnexpectedSerializationError(written.error().message, "Serializer");
        if (auto closed = modelFile.Close(); !closed)
            return UnexpectedSerializationError(closed.error().message, "Serializer");
        return file;
    }

    auto Serializer::SaveNewModels(const Scene& scene, ModelCatalog& models, const Path& assetsFolder)
        -> EngineResult<size_t>
    {
        size_t saved = 0;
        for (const ModelHandler handler : UsedModels(scene, models)) {
            // A file outside of the assets folder (a model from elsewhere, an old scene) is copied in
            if (const Path* file = models.FileOf(handler); file && project::AssetPath::Of(*file, assetsFolder))
                continue;
            auto file = SaveNewModel(*models.Get(handler), assetsFolder);
            if (!file)
                return UnexpectedSerializationError(file.error());
            models.SetFile(handler, std::move(*file));
            ++saved;
        }
        return saved;
    }

    auto Serializer::SaveScene(const Scene& scene, const ModelCatalog& models, const Path& assetsFolder,
        const Path& path) -> EngineResult<void>
    {
        JsonValue document { JsonValue::Type::Object };
        // 7: models are referenced by their asset paths (6: model, color and visible are the Model component)
        auto result = SetField(document, "formatVersion", JsonValue(7.0));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedModels { JsonValue::Type::Array };
        ModelIndices modelIndices;
        for (const ModelHandler handler : UsedModels(scene, models)) {
            const Path* file = models.FileOf(handler);
            const auto asset = file ? project::AssetPath::Of(*file, assetsFolder) : std::nullopt;
            if (!asset)
                return UnexpectedSerializationError("Model \"" + models.Get(handler)->name
                        + "\" has no file in the assets folder: SaveNewModels writes the new models first",
                    "Serializer");
            JsonValue modelReference { JsonValue::Type::Object };
            result = SetField(modelReference, "path", JsonValue(StringView(asset->Text())));
            if (!result)
                return UnexpectedSerializationError(result.error());

            modelIndices.emplace(handler, modelIndices.size());
            result = AddToArray(serializedModels, std::move(modelReference));
            if (!result)
                return UnexpectedSerializationError(result.error());
        }

        result = SetField(document, "models", std::move(serializedModels));
        if (!result)
            return UnexpectedSerializationError(result.error());

        // Parents come before their children, as in the scene
        JsonValue serializedNodes { JsonValue::Type::Array };
        for (const auto& node : scene.Nodes()) {
            auto serializedNode = MakeNode(scene, node, modelIndices);
            if (!serializedNode)
                return UnexpectedSerializationError(serializedNode.error());
            result = AddToArray(serializedNodes, std::move(*serializedNode));
            if (!result)
                return UnexpectedSerializationError(result.error());
        }
        result = SetField(document, "nodes", std::move(serializedNodes));
        if (!result)
            return UnexpectedSerializationError(result.error());

        auto json = document.ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        std::error_code folderError;
        FileSystem::CreateDirectories(path.Parent(), folderError);
        File file;
        if (auto opened = file.Open(path, FileMode::Write); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        if (auto written = file.Write(*json); !written)
            return UnexpectedSerializationError(written.error().message, "Serializer");
        if (auto closed = file.Close(); !closed)
            return UnexpectedSerializationError(closed.error().message, "Serializer");
        return {};
    }

    auto Serializer::LoadScene(const Path& path, const Path& assetsFolder) -> EngineResult<SceneAsset>
    {
        File file;
        if (auto opened = file.Open(path, FileMode::Read); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        const auto json = file.ReadAll();
        if (!json)
            return UnexpectedSerializationError(json.error().message, "Serializer");
        JsonValue document;
        auto parseResult = document.Parse(*json->ReadText(0, json->Size()));
        if (!parseResult)
            return UnexpectedSerializationError(parseResult.error());
        if (!document.IsObject())
            return UnexpectedSerializationError("Scene document root must be an object", "Serializer");

        auto versionField = GetField(document, "formatVersion");
        if (!versionField)
            return UnexpectedSerializationError(versionField.error());
        auto formatVersion = ReadUnsigned<uint32_t>(*versionField, "formatVersion");
        if (!formatVersion)
            return UnexpectedSerializationError(formatVersion.error());
        if (*formatVersion < 4 || *formatVersion > 7)
            return UnexpectedSerializationError("Unsupported scene file version", "Serializer");

        SceneAsset loaded;

        // 5 is 4 with node components, 6 moves model, color and visible of a node into its Model component
        auto serializedModels = ReadArrayField(document, "models");
        auto serializedNodes = ReadArrayField(document, "nodes");
        if (!serializedModels || !serializedNodes)
            return UnexpectedSerializationError("Scene file is missing models or nodes", "Serializer");
        auto modelCount = serializedModels->Size();
        auto nodeCount = serializedNodes->Size();
        if (!modelCount || !nodeCount)
            return UnexpectedSerializationError("Invalid models or nodes array", "Serializer");
        // Components and Entity props are keyed by SceneAsset::NodeRef, a NodeId
        if (*nodeCount >= NodeId::MaxIndex)
            return UnexpectedSerializationError("Scene file has too many nodes", "Serializer");

        loaded.nodes.reserve(*nodeCount);
        loaded.models.reserve(*modelCount);
        for (size_t modelIndex = 0; modelIndex < *modelCount; ++modelIndex) {
            auto modelReference = GetElement(*serializedModels, modelIndex);
            if (!modelReference)
                return UnexpectedSerializationError(modelReference.error());
            auto modelPathField = GetField(*modelReference, "path");
            if (!modelPathField)
                return UnexpectedSerializationError(modelPathField.error());
            auto modelPathText = modelPathField->AsString();
            if (!modelPathText)
                return UnexpectedSerializationError(modelPathText.error());
            auto modelFile = ModelFileOf(*modelPathText, *formatVersion, path, assetsFolder);
            if (!modelFile)
                return UnexpectedSerializationError(modelFile.error());

            auto model = LoadModel(*modelFile);
            if (!model)
                return UnexpectedSerializationError(model.error());
            loaded.models.push_back(std::move(*model));
            loaded.modelFiles.push_back(std::move(*modelFile));
        }

        const SceneFileCounts counts { .nodeCount = *nodeCount, .modelCount = *modelCount };
        for (size_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
            auto serializedNode = GetElement(*serializedNodes, nodeIndex);
            if (!serializedNode)
                return UnexpectedSerializationError(serializedNode.error());
            auto node = ReadNode(*serializedNode, nodeIndex, *nodeCount);
            if (!node)
                return UnexpectedSerializationError(node.error());
            if (*formatVersion <= 5) {
                auto model = ReadNodeModel(*serializedNode, *modelCount);
                if (!model)
                    return UnexpectedSerializationError(model.error());
                if (*model)
                    loaded.components.Components<ModelComponent>().Emplace(SceneAsset::NodeRef(nodeIndex), std::move(**model));
            }
            if (const auto componentsField = serializedNode->Find("components")) {
                auto components = ReadComponents(*componentsField, counts, SceneAsset::NodeRef(nodeIndex), loaded.components);
                if (!components)
                    return UnexpectedSerializationError(components.error());
            }
            loaded.nodes.push_back(std::move(*node));
        }

        for (size_t nodeIndex = 0; nodeIndex < loaded.nodes.size(); ++nodeIndex) {
            size_t current = nodeIndex;
            for (size_t depth = 0; depth <= loaded.nodes.size(); ++depth) {
                const size_t parentIndex = loaded.nodes[current].parentIndex;
                if (parentIndex == SceneAsset::NoNode)
                    break;
                if (depth == loaded.nodes.size())
                    return UnexpectedSerializationError("Scene node hierarchy contains a cycle", "Serializer");
                current = parentIndex;
            }
        }

        return loaded;
    }

    auto Serializer::LoadProject(const Path& path) -> EngineResult<project::Project>
    {
        File file;
        if (auto opened = file.Open(path, FileMode::Read); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        const auto json = file.ReadAll();
        if (!json)
            return UnexpectedSerializationError(json.error().message, "Serializer");
        JsonValue document;
        if (auto parsed = document.Parse(*json->ReadText(0, json->Size())); !parsed)
            return UnexpectedSerializationError(parsed.error());
        if (!document.IsObject())
            return UnexpectedSerializationError("Project document root must be an object", "Serializer");

        auto versionField = GetField(document, "formatVersion");
        if (!versionField)
            return UnexpectedSerializationError(versionField.error());
        auto version = ReadUnsigned<uint32_t>(*versionField, "project.formatVersion");
        if (!version || *version != 1)
            return UnexpectedSerializationError("Unsupported project file version", "Serializer");

        project::ProjectDesc desc;
        auto name = ReadProjectString(document["name"], "name");
        if (!name)
            return UnexpectedSerializationError(name.error());
        desc.name = std::move(*name);
        auto entry = GetField(document, "entry");
        if (!entry)
            return UnexpectedSerializationError("Project has no entry: set entry.startupScene or entry.mainModule", "Serializer");
        auto readEntry = DeserializeProjectEntry(*entry);
        if (!readEntry)
            return UnexpectedSerializationError(readEntry.error());
        desc.entry = std::move(*readEntry);
        return project::Project(path, std::move(desc));
    }

    auto Serializer::SaveProject(const project::Project& project) -> EngineResult<void>
    {
        const project::ProjectDesc& desc = project.Desc();
        JsonValue entry { JsonValue::Type::Object };
        if (desc.entry.startupScene) {
            if (auto result = SetField(entry, "startupScene", JsonValue(StringView(desc.entry.startupScene->Text()))); !result)
                return UnexpectedSerializationError(result.error());
        }
        if (!desc.entry.mainModule.empty()) {
            if (auto result = SetField(entry, "mainModule", JsonValue(StringView(desc.entry.mainModule))); !result)
                return UnexpectedSerializationError(result.error());
        }
        JsonValue document { JsonValue::Type::Object };
        auto result = SetField(document, "formatVersion", JsonValue(1.0));
        if (result)
            result = SetField(document, "name", JsonValue(StringView(desc.name)));
        if (result)
            result = SetField(document, "entry", std::move(entry));
        if (!result)
            return UnexpectedSerializationError(result.error());
        auto json = document.ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());
        std::error_code folderError;
        FileSystem::CreateDirectories(project.File().Parent(), folderError);
        File file;
        if (auto opened = file.Open(project.File(), FileMode::Write); !opened)
            return UnexpectedSerializationError(opened.error().message, "Serializer");
        if (auto written = file.Write(*json); !written)
            return UnexpectedSerializationError(written.error().message, "Serializer");
        if (auto closed = file.Close(); !closed)
            return UnexpectedSerializationError(closed.error().message, "Serializer");
        return {};
    }
} // namespace elm::resource
