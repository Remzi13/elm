#include "resmgr/Serializer.hpp"

#include "core/Std.hpp"

#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>

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
                return UnexpectedSerializationError("Invalid numeric scene field: " + std::string(description), "Serializer");

            return *number;
        }

        template<typename T>
        auto ReadUnsigned(const JsonValue& value, StringView description) -> EngineResult<T>
        {
            auto number = ReadNumber(value, description);
            if (!number)
                return UnexpectedSerializationError(number.error());

            if (*number < 0.0 || *number > static_cast<double>(std::numeric_limits<T>::max()) || std::floor(*number) != *number)
                return UnexpectedSerializationError("Invalid integer scene field: " + std::string(description), "Serializer");

            return static_cast<T>(*number);
        }

        template<size_t N>
        auto ReadFloatArray(const JsonValue& value, StringView description) -> EngineResult<std::array<float, N>>
        {
            if (!value.IsArray())
                return UnexpectedSerializationError("Invalid array scene field: " + std::string(description), "Serializer");

            auto size = value.Size();
            if (!size || *size != N)
                return UnexpectedSerializationError("Invalid array scene field: " + std::string(description), "Serializer");

            std::array<float, N> result{};
            for (size_t index = 0; index < N; ++index) {
                auto element = GetElement(value, index);
                if (!element)
                    return UnexpectedSerializationError(element.error());
                auto number = ReadNumber(*element, description);
                if (!number)
                    return UnexpectedSerializationError(number.error());
                if (*number < -std::numeric_limits<float>::max() || *number > std::numeric_limits<float>::max())
                    return UnexpectedSerializationError("Scene number is outside the float range: " + std::string(description), "Serializer");

                result[index] = static_cast<float>(*number);
            }

            return result;
        }

        auto ReadMatrix(const JsonValue& value, StringView description) -> EngineResult<std::array<float, 16>>
        {
            if (!value.IsArray())
                return UnexpectedSerializationError("Invalid array scene field: " + std::string(description), "Serializer");

            auto size = value.Size();
            if (!size)
                return UnexpectedSerializationError(size.error());

            if (*size == 16)
                return ReadFloatArray<16>(value, description);

            if (*size != 4)
                return UnexpectedSerializationError("Invalid matrix scene field: " + std::string(description), "Serializer");

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
                return UnexpectedSerializationError("Scene field is not an array: " + std::string(key), "Serializer");

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
                return UnexpectedSerializationError("Invalid boolean scene field: " + std::string(key), "Serializer");
            return *result;
        }

        auto SerializeModel(const Model& model) -> EngineResult<JsonValue>
        {
            const auto& meshData = model.meshData;
            JsonValue document{ JsonValue::Type::Object };
            auto result = SetField(document, "formatVersion", JsonValue(1.0));
            if (!result)
                return UnexpectedSerializationError(result.error());

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
            auto color = MakeArray(std::array{ model.color.x, model.color.y, model.color.z, model.color.w });
            if (!color)
                return UnexpectedSerializationError(color.error());

            result = SetField(document, "vertices", std::move(serializedVertices));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(document, "indices", std::move(*indices));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(document, "minBounds", std::move(*minBounds));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(document, "maxBounds", std::move(*maxBounds));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(document, "color", std::move(*color));
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

        auto DeserializeModel(const JsonValue& document) -> EngineResult<Model>
        {
            auto meshData = DeserializeMeshData(document);
            if (!meshData)
                return UnexpectedSerializationError(meshData.error());

            Model model;
            model.meshData = std::move(*meshData);
            auto colorValue = document["color"];
            if (colorValue.Exists()) {
                auto color = ReadFloatArray<4>(colorValue, "model.color");
                if (!color)
                    return UnexpectedSerializationError(color.error());
                model.color = Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
            }
            return model;
        }

    }

    auto Serializer::SaveModel(const Model& model, const std::filesystem::path& path) -> EngineResult<void>
    {
        auto serializedModel = SerializeModel(model);
        if (!serializedModel)
            return UnexpectedSerializationError(serializedModel.error());
        auto json = serializedModel->ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        return WriteFile(path, *json);
    }

    auto Serializer::SaveScene(const Scene& scene, const std::filesystem::path& path) -> EngineResult<void>
    {
        JsonValue document{ JsonValue::Type::Object };
        auto result = SetField(document, "formatVersion", JsonValue(2.0));
        if (!result)
            return UnexpectedSerializationError(result.error());
        result = SetField(document, "preset", JsonValue(static_cast<double>(scene.preset)));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedModels{ JsonValue::Type::Array };
        UnorderedMap<const MeshData*, size_t> meshIndices;
        const auto sceneDirectory = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
        for (const auto& mesh : scene.meshes) {
            const auto& meshData = mesh.GetData();
            if (!meshData)
                return UnexpectedSerializationError("Scene contains a mesh without geometry", "Serializer");
            if (meshIndices.find(meshData.get()) != meshIndices.end())
                continue;

            const auto modelPath = sceneDirectory /
                (path.stem().string() + "_model_" + std::to_string(meshIndices.size()) + ".model");
            Model model{ *meshData };
            const auto instance = std::find_if(scene.instances.begin(), scene.instances.end(), [&](const Scene::Instance& candidate) {
                return candidate.meshData.get() == meshData.get();
            });
            if (instance != scene.instances.end())
                model.color = instance->color;

            auto writeModel = SaveModel(model, modelPath);
            if (!writeModel)
                return UnexpectedSerializationError(writeModel.error());

            const auto relativeModelPath = modelPath.lexically_relative(sceneDirectory).generic_string();
            JsonValue modelReference{ JsonValue::Type::Object };
            result = SetField(modelReference, "path", JsonValue(relativeModelPath));
            if (!result)
                return UnexpectedSerializationError(result.error());

            meshIndices.emplace(meshData.get(), meshIndices.size());
            result = AddToArray(serializedModels, std::move(modelReference));
            if (!result)
                return UnexpectedSerializationError(result.error());
        }

        result = SetField(document, "models", std::move(serializedModels));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedInstances{ JsonValue::Type::Array };
        for (const auto& instance : scene.instances) {
            const auto mesh = meshIndices.find(instance.meshData.get());
            if (mesh == meshIndices.end())
                return UnexpectedSerializationError("Scene instance refers to geometry not owned by the scene", "Serializer");

            JsonValue serializedInstance{ JsonValue::Type::Object };
            result = SetField(serializedInstance, "meshIndex", JsonValue(static_cast<double>(mesh->second)));
            if (!result)
                return UnexpectedSerializationError(result.error());

            auto worldTransform = MakeMatrix(instance.worldTransform.m);
            if (!worldTransform)
                return UnexpectedSerializationError(worldTransform.error());
            auto color = MakeArray(std::array{ instance.color.x, instance.color.y, instance.color.z, instance.color.w });
            if (!color)
                return UnexpectedSerializationError(color.error());

            result = SetField(serializedInstance, "worldTransform", std::move(*worldTransform));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(serializedInstance, "color", std::move(*color));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(serializedInstance, "visible", JsonValue(instance.visible));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = AddToArray(serializedInstances, std::move(serializedInstance));
            if (!result)
                return UnexpectedSerializationError(result.error());
        }

        result = SetField(document, "instances", std::move(serializedInstances));
        if (!result)
            return UnexpectedSerializationError(result.error());

        auto json = document.ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        return WriteFile(path, *json);
    }

    auto Serializer::LoadScene(Scene& scene, render::RenderResources& resources, const std::filesystem::path& path) -> EngineResult<void>
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
        if (*formatVersion != 1 && *formatVersion != 2)
            return UnexpectedSerializationError("Unsupported scene file version", "Serializer");

        auto presetField = GetField(document, "preset");
        if (!presetField)
            return UnexpectedSerializationError(presetField.error());
        auto preset = ReadUnsigned<uint32_t>(*presetField, "preset");
        if (!preset)
            return UnexpectedSerializationError(preset.error());
        if (*preset > static_cast<uint32_t>(ScenePreset::PhysicsSandbox))
            return UnexpectedSerializationError("Scene file contains an invalid preset", "Serializer");

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

        Scene loadedScene;
        loadedScene.preset = static_cast<ScenePreset>(*preset);
        loadedScene.meshes.reserve(*meshCount);
        loadedScene.instances.reserve(*instanceCount);
        Vector<Vector4> modelColors;
        modelColors.reserve(*meshCount);

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

                const std::filesystem::path relativeModelPath(*modelPathText);
                if (relativeModelPath.empty() || relativeModelPath.is_absolute())
                    return UnexpectedSerializationError("Scene model path must be a non-empty relative path", "Serializer");

                auto modelJson = ReadFile(path.parent_path() / relativeModelPath);
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
                static_cast<void>(loadedScene.MakeInstance(resources, *meshData));
                modelColors.push_back(Model{}.color);
            } else {
                auto model = DeserializeModel(modelDocument);
                if (!model)
                    return UnexpectedSerializationError(model.error());
                static_cast<void>(loadedScene.MakeInstance(resources, model->meshData));
                modelColors.push_back(model->color);
            }
        }

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
            if (*meshIndex >= loadedScene.meshes.size())
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

            auto instance = loadedScene.MakeInstance(loadedScene.meshes[*meshIndex]);
            instance.worldTransform.m = *worldTransform;
            instance.color = modelColors[*meshIndex];
            auto colorField = (*serializedInstance)["color"];
            if (colorField.Exists()) {
                auto color = ReadFloatArray<4>(colorField, "instance.color");
                if (!color)
                    return UnexpectedSerializationError(color.error());
                instance.color = Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
            }
            instance.visible = *visible;
            loadedScene.instances.push_back(std::move(instance));
        }

        scene = std::move(loadedScene);
        return {};
    }

    auto Serializer::WriteFile(const std::filesystem::path& path, StringView contents) -> EngineResult<void>
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return UnexpectedSerializationError("Failed to open file for writing: " + path.string(), "Serializer");

        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.close();
        if (!file)
            return UnexpectedSerializationError("Failed to write file: " + path.string(), "Serializer");

        return {};
    }

    auto Serializer::ReadFile(const std::filesystem::path& path) -> EngineResult<String>
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return UnexpectedSerializationError("Failed to open file for reading: " + path.string(), "Serializer");

        const String contents{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        if (file.bad())
            return UnexpectedSerializationError("Failed to read file: " + path.string(), "Serializer");

        return String(contents.begin(), contents.end());
    }
}
