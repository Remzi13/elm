#include "resmgr/Serializer.hpp"

#include "core/Std.hpp"

#include "resmgr/JsonValue.hpp"

#include "project/Project.hpp"
#include "resmgr/SerializationError.hpp"

#include "Scene/ComponentTypes.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace elm::resmgr {
    namespace {
        auto SetField(JsonValue& object, StringView key, JsonValue value) -> EngineResult<void>
        {
            return object.Set(key, std::move(value));
        }

        auto AddToArray(JsonValue& array, JsonValue value) -> EngineResult<void>
        {
            return array.Push(std::move(value));
        }

        template<typename Range>
        auto MakeArray(const Range& values) -> EngineResult<JsonValue>
        {
            JsonValue array{ JsonValue::Type::Array };
            for (const auto value : values) {
                auto result = AddToArray(array, JsonValue(static_cast<double>(value)));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return array;
        }

        auto MakeMatrix(const std::array<float, 16>& values) -> EngineResult<JsonValue>
        {
            JsonValue matrix{ JsonValue::Type::Array };
            for (size_t row = 0; row < 4; ++row) {
                auto rowArray = MakeArray(std::array{
                    values[row * 4],
                    values[row * 4 + 1],
                    values[row * 4 + 2],
                    values[row * 4 + 3]
                });
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
            return MakeArray(std::array{ vector.x, vector.y, vector.z });
        }

        auto MakeModelFileStem(StringView name, size_t fallbackIndex) -> String
        {
            String stem;
            stem.reserve(name.size());
            for (const unsigned char character : name) {
                const bool invalid = character < 0x20 || character == 0x7f ||
                    character == '/' || character == '\\' || character == ':' || character == '*' ||
                    character == '?' || character == '"' || character == '<' || character == '>' || character == '|';
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
            const bool numberedDevice = reservedCheck.size() == 4 &&
                (reservedCheck.compare(0, 3, "COM") == 0 || reservedCheck.compare(0, 3, "LPT") == 0) &&
                reservedCheck[3] >= '1' && reservedCheck[3] <= '9';
            if (reservedCheck == "CON" || reservedCheck == "PRN" || reservedCheck == "AUX" ||
                reservedCheck == "NUL" || numberedDevice)
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

        template<typename T>
        auto ReadUnsigned(const JsonValue& value, StringView description) -> EngineResult<T>
        {
            auto number = ReadNumber(value, description);
            if (!number)
                return UnexpectedSerializationError(number.error());

            if (*number < 0.0 || *number > static_cast<double>(std::numeric_limits<T>::max()) || std::floor(*number) != *number)
                return UnexpectedSerializationError("Invalid integer scene field: " + String(description), "Serializer");

            return static_cast<T>(*number);
        }

        template<size_t N>
        auto ReadFloatArray(const JsonValue& value, StringView description) -> EngineResult<std::array<float, N>>
        {
            if (!value.IsArray())
                return UnexpectedSerializationError("Invalid array scene field: " + String(description), "Serializer");

            auto size = value.Size();
            if (!size || *size != N)
                return UnexpectedSerializationError("Invalid array scene field: " + String(description), "Serializer");

            std::array<float, N> result{};
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

            std::array<float, 16> matrix{};
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

            return Vector3{ (*components)[0], (*components)[1], (*components)[2] };
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

        auto ReadBoolField(const JsonValue& object, StringView key) -> EngineResult<bool>
        {
            auto value = GetField(object, key);
            if (!value)
                return UnexpectedSerializationError(value.error());

            auto result = value->AsBoolean();
            if (!result)
                return UnexpectedSerializationError("Invalid boolean scene field: " + String(key), "Serializer");
            return *result;
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
            }, value);
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
            auto result = SetFields(serialized, {
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
            auto result = SetFields(serialized, {
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
        using ModelIndices = UnorderedMap<graphics::ModelHandler, size_t>;

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

        auto SerializeModel(const graphics::Model& model) -> EngineResult<JsonValue>
        {
            JsonValue document{ JsonValue::Type::Object };
            auto result = SetField(document, "formatVersion", JsonValue(2.0));
            if (!result)
                return UnexpectedSerializationError(result.error());

            result = SetField(document, "name", JsonValue(model.name.c_str()));
            if (!result)
                return UnexpectedSerializationError(result.error());

            JsonValue serializedSubmeshes{ JsonValue::Type::Array };
            for (const auto& submesh : model.submeshes) {
                if (!submesh.data)
                    return UnexpectedSerializationError("Model contains a submesh without geometry", "Serializer");
                const auto& meshData = *submesh.data;
                JsonValue serializedSubmesh{ JsonValue::Type::Object };
                JsonValue serializedVertices{ JsonValue::Type::Array };
                for (const auto& vertex : meshData.vertices) {
                    JsonValue serializedVertex{ JsonValue::Type::Object };
                    auto position = MakeVector(vertex.position);
                    if (!position)
                        return UnexpectedSerializationError(position.error());
                    result = SetField(serializedVertex, "position", std::move(*position));
                    if (!result)
                        return UnexpectedSerializationError(result.error());

                    auto normal = MakeVector(vertex.normal);
                    if (!normal)
                        return UnexpectedSerializationError(normal.error());
                    result = SetField(serializedVertex, "normal", std::move(*normal));
                    if (!result)
                        return UnexpectedSerializationError(result.error());
                    result = SetField(serializedVertex, "u", JsonValue(vertex.u));
                    if (!result)
                        return UnexpectedSerializationError(result.error());
                    result = SetField(serializedVertex, "v", JsonValue(vertex.v));
                    if (!result)
                        return UnexpectedSerializationError(result.error());
                    result = AddToArray(serializedVertices, std::move(serializedVertex));
                    if (!result)
                        return UnexpectedSerializationError(result.error());
                }

                auto indices = MakeArray(meshData.indices);
                if (!indices)
                    return UnexpectedSerializationError(indices.error());
                auto minBounds = MakeVector(meshData.localBounds.minBounds);
                if (!minBounds)
                    return UnexpectedSerializationError(minBounds.error());
                auto maxBounds = MakeVector(meshData.localBounds.maxBounds);
                if (!maxBounds)
                    return UnexpectedSerializationError(maxBounds.error());
                auto color = MakeArray(std::array{ submesh.color.x, submesh.color.y, submesh.color.z, submesh.color.w });
                if (!color)
                    return UnexpectedSerializationError(color.error());

                result = SetField(serializedSubmesh, "vertices", std::move(serializedVertices));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = SetField(serializedSubmesh, "indices", std::move(*indices));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = SetField(serializedSubmesh, "minBounds", std::move(*minBounds));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = SetField(serializedSubmesh, "maxBounds", std::move(*maxBounds));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = SetField(serializedSubmesh, "color", std::move(*color));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = SetField(serializedSubmesh, "name", JsonValue(submesh.name.c_str()));
                if (!result)
                    return UnexpectedSerializationError(result.error());
                result = AddToArray(serializedSubmeshes, std::move(serializedSubmesh));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            result = SetField(document, "submeshes", std::move(serializedSubmeshes));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return document;
        }

        auto DeserializeMeshData(const JsonValue& document) -> EngineResult<MeshData>
        {
            if (!document.IsObject())
                return UnexpectedSerializationError("Model document root must be an object", "Serializer");

            auto vertices = ReadArrayField(document, "vertices");
            if (!vertices)
                return UnexpectedSerializationError(vertices.error());
            auto indices = ReadArrayField(document, "indices");
            if (!indices)
                return UnexpectedSerializationError(indices.error());
            auto minBounds = ReadVector3Field(document, "minBounds");
            if (!minBounds)
                return UnexpectedSerializationError(minBounds.error());
            auto maxBounds = ReadVector3Field(document, "maxBounds");
            if (!maxBounds)
                return UnexpectedSerializationError(maxBounds.error());
            auto vertexCount = vertices->Size();
            if (!vertexCount)
                return UnexpectedSerializationError(vertexCount.error());
            auto indexCount = indices->Size();
            if (!indexCount)
                return UnexpectedSerializationError(indexCount.error());

            MeshData meshData;
            meshData.localBounds = AABB{ *minBounds, *maxBounds };
            meshData.vertices.reserve(*vertexCount);
            meshData.indices.reserve(*indexCount);

            for (size_t vertexIndex = 0; vertexIndex < *vertexCount; ++vertexIndex) {
                auto serializedVertex = GetElement(*vertices, vertexIndex);
                if (!serializedVertex)
                    return UnexpectedSerializationError(serializedVertex.error());
                if (!serializedVertex->IsObject())
                    return UnexpectedSerializationError("Model vertex must be an object", "Serializer");

                auto position = ReadVector3Field(*serializedVertex, "position");
                if (!position)
                    return UnexpectedSerializationError(position.error());
                auto normal = ReadVector3Field(*serializedVertex, "normal");
                if (!normal)
                    return UnexpectedSerializationError(normal.error());
                auto uField = GetField(*serializedVertex, "u");
                if (!uField)
                    return UnexpectedSerializationError(uField.error());
                auto u = ReadNumber(*uField, "vertex.u");
                if (!u)
                    return UnexpectedSerializationError(u.error());
                auto vField = GetField(*serializedVertex, "v");
                if (!vField)
                    return UnexpectedSerializationError(vField.error());
                auto v = ReadNumber(*vField, "vertex.v");
                if (!v)
                    return UnexpectedSerializationError(v.error());
                if (*u < -std::numeric_limits<float>::max() || *u > std::numeric_limits<float>::max() ||
                    *v < -std::numeric_limits<float>::max() || *v > std::numeric_limits<float>::max())
                    return UnexpectedSerializationError("Vertex UV is outside the float range", "Serializer");

                meshData.vertices.push_back({ *position, *normal, static_cast<float>(*u), static_cast<float>(*v) });
            }

            for (size_t index = 0; index < *indexCount; ++index) {
                auto serializedIndex = GetElement(*indices, index);
                if (!serializedIndex)
                    return UnexpectedSerializationError(serializedIndex.error());
                auto meshVertexIndex = ReadUnsigned<uint32_t>(*serializedIndex, "model index");
                if (!meshVertexIndex)
                    return UnexpectedSerializationError(meshVertexIndex.error());
                if (*meshVertexIndex >= meshData.vertices.size())
                    return UnexpectedSerializationError("Model contains an out-of-range vertex index", "Serializer");
                meshData.indices.push_back(*meshVertexIndex);
            }

            return meshData;
        }

        auto DeserializeModel(const JsonValue& document) -> EngineResult<graphics::Model>
        {
            graphics::Model model;
            if (const auto nameValue = document.Find("name")) {
                auto name = nameValue->AsString();
                if (!name)
                    return UnexpectedSerializationError(name.error());
                model.name = std::move(*name);
            }

            if (const auto serializedSubmeshes = document.Find("submeshes")) {
                if (!serializedSubmeshes->IsArray())
                    return UnexpectedSerializationError("Model submeshes must be an array", "Serializer");
                auto count = serializedSubmeshes->Size();
                if (!count)
                    return UnexpectedSerializationError(count.error());
                for (size_t index = 0; index < *count; ++index) {
                    auto serializedSubmesh = GetElement(*serializedSubmeshes, index);
                    if (!serializedSubmesh)
                        return UnexpectedSerializationError(serializedSubmesh.error());
                    auto meshData = DeserializeMeshData(*serializedSubmesh);
                    if (!meshData)
                        return UnexpectedSerializationError(meshData.error());
                    Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
                    if (const auto colorValue = serializedSubmesh->Find("color")) {
                        auto components = ReadFloatArray<4>(*colorValue, "submesh.color");
                        if (!components)
                            return UnexpectedSerializationError(components.error());
                        color = Vector4{ (*components)[0], (*components)[1], (*components)[2], (*components)[3] };
                    }
                    String submeshName;
                    if (const auto nameValue = serializedSubmesh->Find("name")) {
                        auto name = nameValue->AsString();
                        if (!name)
                            return UnexpectedSerializationError(name.error());
                        submeshName = std::move(*name);
                    }
                    model.AddSubmesh(std::move(*meshData), color, std::move(submeshName));
                }
                return model;
            }

            auto meshData = DeserializeMeshData(document);
            if (!meshData)
                return UnexpectedSerializationError(meshData.error());
            Vector4 color { 0.8f, 0.8f, 0.8f, 1.0f };
            if (const auto colorValue = document.Find("color")) {
                auto components = ReadFloatArray<4>(*colorValue, "model.color");
                if (!components)
                    return UnexpectedSerializationError(components.error());
                color = Vector4{ (*components)[0], (*components)[1], (*components)[2], (*components)[3] };
            }
            model.AddSubmesh(std::move(*meshData), color);
            return model;
        }

        /// The models of the scene's Model components, each once, in the order of the nodes.
        auto UsedModels(const Scene& scene, const graphics::ModelManager& models) -> Vector<graphics::ModelHandler>
        {
            Vector<graphics::ModelHandler> used;
            const auto* modelComponents = scene.FindComponents<ModelComponent>();
            for (const auto& node : scene.Nodes()) {
                const ModelComponent* component = modelComponents ? modelComponents->Find(node.id) : nullptr;
                if (component && models.Get(component->model)
                    && std::find(used.begin(), used.end(), component->model) == used.end())
                    used.push_back(component->model);
            }
            return used;
        }

        /// models/<stem>.model, with a number added while such a file exists.
        auto UnusedModelFile(const String& stem, const Path& assetsFolder) -> EngineResult<project::AssetPath>
        {
            const String folder = String(project::Project::ModelsFolder) + "/";
            String text = folder + stem + ".model";
            for (size_t suffix = 1; FileSystem::Exists(assetsFolder / text); ++suffix)
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

    }

    auto Serializer::SaveModel(const graphics::Model& model, const Path& path) -> EngineResult<void>
    {
        auto serializedModel = SerializeModel(model);
        if (!serializedModel)
            return UnexpectedSerializationError(serializedModel.error());
        auto json = serializedModel->ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        return WriteFile(path, *json);
    }

    auto Serializer::LoadModel(const Path& path) -> EngineResult<graphics::Model>
    {
        auto json = ReadFile(path);
        if (!json)
            return UnexpectedSerializationError(json.error());
        JsonValue document;
        auto parseResult = document.Parse(*json);
        if (!parseResult)
            return UnexpectedSerializationError(parseResult.error());
        if (!document.IsObject())
            return UnexpectedSerializationError("Model document root must be an object", "Serializer");
        auto versionField = GetField(document, "formatVersion");
        if (!versionField)
            return UnexpectedSerializationError(versionField.error());
        auto version = ReadUnsigned<uint32_t>(*versionField, "model.formatVersion");
        if (!version || (*version != 1 && *version != 2))
            return UnexpectedSerializationError("Unsupported model file version", "Serializer");
        return DeserializeModel(document);
    }

    auto Serializer::SaveNewModel(const graphics::Model& model, const Path& assetsFolder) -> EngineResult<Path>
    {
        const auto asset = UnusedModelFile(MakeModelFileStem(model.name, 0), assetsFolder);
        if (!asset)
            return UnexpectedSerializationError(asset.error());
        Path file = asset->In(assetsFolder);
        if (auto written = SaveModel(model, file); !written)
            return UnexpectedSerializationError(written.error());
        return file;
    }

    auto Serializer::SaveNewModels(const Scene& scene, graphics::ModelManager& models, const Path& assetsFolder)
        -> EngineResult<size_t>
    {
        size_t saved = 0;
        for (const graphics::ModelHandler handler : UsedModels(scene, models)) {
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

    auto Serializer::SaveScene(const Scene& scene, const graphics::ModelManager& models, const Path& assetsFolder,
        const Path& path) -> EngineResult<void>
    {
        JsonValue document{ JsonValue::Type::Object };
        // 7: models are referenced by their asset paths (6: model, color and visible are the Model component)
        auto result = SetField(document, "formatVersion", JsonValue(7.0));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedModels{ JsonValue::Type::Array };
        ModelIndices modelIndices;
        for (const graphics::ModelHandler handler : UsedModels(scene, models)) {
            const Path* file = models.FileOf(handler);
            const auto asset = file ? project::AssetPath::Of(*file, assetsFolder) : std::nullopt;
            if (!asset)
                return UnexpectedSerializationError("Model \"" + models.Get(handler)->name
                    + "\" has no file in the assets folder: SaveNewModels writes the new models first", "Serializer");
            JsonValue modelReference{ JsonValue::Type::Object };
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
        JsonValue serializedNodes{ JsonValue::Type::Array };
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

        return WriteFile(path, *json);
    }

    auto Serializer::LoadScene(const Path& path, const Path& assetsFolder) -> EngineResult<SceneAsset>
    {
        auto json = ReadFile(path);
        if (!json)
            return UnexpectedSerializationError(json.error());
        JsonValue document;
        auto parseResult = document.Parse(*json);
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
        if (*formatVersion < 1 || *formatVersion > 7)
            return UnexpectedSerializationError("Unsupported scene file version", "Serializer");

        SceneAsset loaded;

        // 5 is 4 with node components, 6 moves model, color and visible of a node into its Model component
        if (*formatVersion >= 4) {
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

        auto meshes = ReadArrayField(document, *formatVersion == 1 ? "meshes" : "models");
        if (!meshes)
            return UnexpectedSerializationError(meshes.error());
        auto instances = ReadArrayField(document, "instances");
        if (!instances)
            return UnexpectedSerializationError(instances.error());
        auto meshCount = meshes->Size();
        if (!meshCount)
            return UnexpectedSerializationError(meshCount.error());
        auto instanceCount = instances->Size();
        if (!instanceCount)
            return UnexpectedSerializationError(instanceCount.error());

        loaded.nodes.reserve(*instanceCount + (*formatVersion >= 3 ? *meshCount : 0));
        loaded.models.reserve(*meshCount);
        Vector<Vector4> modelColors;
        modelColors.reserve(*meshCount);

        if (*formatVersion >= 3) {
            auto serializedNodes = ReadArrayField(document, "nodes");
            if (!serializedNodes)
                return UnexpectedSerializationError(serializedNodes.error());
            auto nodeCount = serializedNodes->Size();
            if (!nodeCount)
                return UnexpectedSerializationError(nodeCount.error());
            loaded.nodes.reserve(*nodeCount);
            for (size_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
                auto serializedNode = GetElement(*serializedNodes, nodeIndex);
                if (!serializedNode)
                    return UnexpectedSerializationError(serializedNode.error());
                if (!serializedNode->IsObject())
                    return UnexpectedSerializationError("Scene node must be an object", "Serializer");

                auto transformField = GetField(*serializedNode, "localTransform");
                if (!transformField)
                    return UnexpectedSerializationError(transformField.error());
                auto localTransform = ReadMatrix(*transformField, "node.localTransform");
                if (!localTransform)
                    return UnexpectedSerializationError(localTransform.error());

                SceneAsset::Node node;
                node.localTransform.m = *localTransform;
                if (const auto parentField = serializedNode->Find("parentIndex")) {
                    auto parentIndex = ReadUnsigned<size_t>(*parentField, "node.parentIndex");
                    if (!parentIndex)
                        return UnexpectedSerializationError(parentIndex.error());
                    if (*parentIndex >= *nodeCount || *parentIndex == nodeIndex)
                        return UnexpectedSerializationError("Scene node has an invalid parent index", "Serializer");
                    node.parentIndex = *parentIndex;
                }
                if (const auto nameField = serializedNode->Find("name")) {
                    auto name = nameField->AsString();
                    if (!name)
                        return UnexpectedSerializationError(name.error());
                    node.name = std::move(*name);
                }
                loaded.nodes.push_back(std::move(node));
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
        }

        for (size_t meshIndex = 0; meshIndex < *meshCount; ++meshIndex) {
            auto serializedModel = GetElement(*meshes, meshIndex);
            if (!serializedModel)
                return UnexpectedSerializationError(serializedModel.error());

            JsonValue modelDocument;
            if (*formatVersion == 1) {
                modelDocument = std::move(*serializedModel);
            } else {
                auto modelPathField = GetField(*serializedModel, "path");
                if (!modelPathField)
                    return UnexpectedSerializationError(modelPathField.error());
                auto modelPathText = modelPathField->AsString();
                if (!modelPathText)
                    return UnexpectedSerializationError(modelPathText.error());

                const Path relativeModelPath(*modelPathText);
                if (relativeModelPath.Empty() || relativeModelPath.IsAbsolute())
                    return UnexpectedSerializationError("Scene model path must be a non-empty relative path", "Serializer");

                auto modelJson = ReadFile(path.Parent() / relativeModelPath);
                if (!modelJson)
                    return UnexpectedSerializationError(modelJson.error());
                auto parseModelResult = modelDocument.Parse(*modelJson);
                if (!parseModelResult)
                    return UnexpectedSerializationError(parseModelResult.error());

                auto modelVersionField = GetField(modelDocument, "formatVersion");
                if (!modelVersionField)
                    return UnexpectedSerializationError(modelVersionField.error());
                auto modelVersion = ReadUnsigned<uint32_t>(*modelVersionField, "model.formatVersion");
                if (!modelVersion)
                    return UnexpectedSerializationError(modelVersion.error());
                if (*modelVersion != 1)
                    return UnexpectedSerializationError("Unsupported model file version", "Serializer");
            }

            if (*formatVersion == 1) {
                auto meshData = DeserializeMeshData(modelDocument);
                if (!meshData)
                    return UnexpectedSerializationError(meshData.error());
                graphics::Model model;
                model.AddSubmesh(std::move(*meshData), Vector4 { 1.0f, 1.0f, 1.0f, 1.0f });
                loaded.models.push_back(std::move(model));
                modelColors.push_back(Vector4 { 0.8f, 0.8f, 0.8f, 1.0f });
            } else {
                auto model = DeserializeModel(modelDocument);
                if (!model)
                    return UnexpectedSerializationError(model.error());
                Vector4 modelColor { 0.8f, 0.8f, 0.8f, 1.0f };
                if (!model->submeshes.empty()) {
                    modelColor = model->submeshes.front().color;
                    for (auto& submesh : model->submeshes)
                        submesh.color = Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };
                }
                modelColors.push_back(modelColor);
                loaded.models.push_back(std::move(*model));
            }
        }

        // Model components are keyed by SceneAsset::NodeRef, a NodeId
        if (loaded.nodes.size() + *instanceCount >= NodeId::MaxIndex)
            return UnexpectedSerializationError("Scene file has too many nodes", "Serializer");
        for (size_t index = 0; index < *instanceCount; ++index) {
            auto serializedInstance = GetElement(*instances, index);
            if (!serializedInstance)
                return UnexpectedSerializationError(serializedInstance.error());
            if (!serializedInstance->IsObject())
                return UnexpectedSerializationError("Scene instance must be an object", "Serializer");

            auto meshIndexField = GetField(*serializedInstance, "meshIndex");
            if (!meshIndexField)
                return UnexpectedSerializationError(meshIndexField.error());
            auto meshIndex = ReadUnsigned<size_t>(*meshIndexField, "instance.meshIndex");
            if (!meshIndex)
                return UnexpectedSerializationError(meshIndex.error());
            if (*meshIndex >= loaded.models.size())
                return UnexpectedSerializationError("Scene instance refers to a missing mesh", "Serializer");

            auto transformField = GetField(*serializedInstance, "worldTransform");
            if (!transformField)
                return UnexpectedSerializationError(transformField.error());
            auto worldTransform = ReadMatrix(*transformField, "instance.worldTransform");
            if (!worldTransform)
                return UnexpectedSerializationError(worldTransform.error());
            auto visible = ReadBoolField(*serializedInstance, "visible");
            if (!visible)
                return UnexpectedSerializationError(visible.error());

            SceneAsset::Node node;
            node.localTransform.m = *worldTransform;
            ModelComponent model { .model = SceneAsset::ModelRef(*meshIndex), .color = modelColors[*meshIndex], .visible = *visible };
            auto colorField = (*serializedInstance)["color"];
            if (colorField.Exists()) {
                auto color = ReadFloatArray<4>(colorField, "instance.color");
                if (!color)
                    return UnexpectedSerializationError(color.error());
                model.color = Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
            }
            if (const auto nameField = serializedInstance->Find("name")) {
                auto name = nameField->AsString();
                if (!name)
                    return UnexpectedSerializationError(name.error());
                node.name = std::move(*name);
            }
            if (*formatVersion >= 3) {
                if (const auto nodeField = serializedInstance->Find("nodeIndex")) {
                    auto nodeIndex = ReadUnsigned<size_t>(*nodeField, "instance.nodeIndex");
                    if (!nodeIndex)
                        return UnexpectedSerializationError(nodeIndex.error());
                    if (*nodeIndex >= loaded.nodes.size())
                        return UnexpectedSerializationError("Scene instance refers to a missing node", "Serializer");
                    node.parentIndex = *nodeIndex;
                }
            }
            loaded.components.Components<ModelComponent>().Emplace(SceneAsset::NodeRef(loaded.nodes.size()), model);
            loaded.nodes.push_back(std::move(node));
        }

        return loaded;
    }

    auto Serializer::LoadProject(const Path& path) -> EngineResult<project::Project>
    {
        auto json = ReadFile(path);
        if (!json)
            return UnexpectedSerializationError(json.error());
        JsonValue document;
        if (auto parsed = document.Parse(*json); !parsed)
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
        return WriteFile(project.File(), *json);
    }

    auto Serializer::WriteFile(const Path& path, StringView contents) -> EngineResult<void>
    {
        if (const auto directory = path.Parent(); !directory.Empty()) {
            std::error_code error;
            FileSystem::CreateDirectories(directory, error);
            if (error)
                return UnexpectedSerializationError("Failed to create directory: " + directory.string(), "Serializer");
        }

        auto file = FileSystem::OpenForWriting(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return UnexpectedSerializationError("Failed to open file for writing: " + path.string(), "Serializer");

        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.close();
        if (!file)
            return UnexpectedSerializationError("Failed to write file: " + path.string(), "Serializer");

        return {};
    }

    auto Serializer::ReadFile(const Path& path) -> EngineResult<String>
    {
        auto file = FileSystem::OpenForReading(path, std::ios::binary);
        if (!file)
            return UnexpectedSerializationError("Failed to open file for reading: " + path.string(), "Serializer");

        const String contents{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        if (file.bad())
            return UnexpectedSerializationError("Failed to read file: " + path.string(), "Serializer");

        return String(contents.begin(), contents.end());
    }
}
