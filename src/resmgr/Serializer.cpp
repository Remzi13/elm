#include "resmgr/Serializer.hpp"

#include "core/Std.hpp"

#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"

#include <cctype>
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

        auto SerializeModel(const Model& model) -> EngineResult<JsonValue>
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

        auto DeserializeModel(const JsonValue& document) -> EngineResult<Model>
        {
            Model model;
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

    }

    auto Serializer::SaveModel(const Model& model, const Path& path) -> EngineResult<void>
    {
        auto serializedModel = SerializeModel(model);
        if (!serializedModel)
            return UnexpectedSerializationError(serializedModel.error());
        auto json = serializedModel->ToString();
        if (!json)
            return UnexpectedSerializationError(json.error());

        return WriteFile(path, *json);
    }

    auto Serializer::SaveScene(const Scene& scene, const ModelManager& models, const Path& path) -> EngineResult<void>
    {
        JsonValue document{ JsonValue::Type::Object };
        auto result = SetField(document, "formatVersion", JsonValue(4.0));
        if (!result)
            return UnexpectedSerializationError(result.error());
        result = SetField(document, "preset", JsonValue(static_cast<double>(scene.preset)));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedModels{ JsonValue::Type::Array };
        UnorderedMap<ModelHandler, size_t> modelIndices;
        Set<String> modelFileNames;
        const auto sceneDirectory = path.Parent().Empty() ? Path(".") : path.Parent();
        EngineResult<void> modelSaveResult;
        models.ForEach([&](ModelHandler handler, const Model& model) {
            if (!modelSaveResult)
                return;
            const auto baseStem = MakeModelFileStem(
                StringView(model.name.data(), model.name.size()), modelIndices.size());
            String fileStem = baseStem;
            size_t suffix = 1;
            while (!modelFileNames.insert(fileStem).second)
                fileStem = baseStem + "_" + std::to_string(suffix++).c_str();
            const auto modelPath = sceneDirectory / StringView(fileStem + ".model");

            auto writeModel = SaveModel(model, modelPath);
            if (!writeModel) {
                modelSaveResult = std::unexpected(writeModel.error());
                return;
            }

            const auto relativeModelPath = modelPath.LexicallyRelative(sceneDirectory).generic_string();
            JsonValue modelReference{ JsonValue::Type::Object };
            result = SetField(modelReference, "path", JsonValue(relativeModelPath));
            if (!result) {
                modelSaveResult = std::unexpected(result.error());
                return;
            }

            modelIndices.emplace(handler, modelIndices.size());
            result = AddToArray(serializedModels, std::move(modelReference));
            if (!result)
                modelSaveResult = std::unexpected(result.error());
        });
        if (!modelSaveResult)
            return UnexpectedSerializationError(modelSaveResult.error());

        result = SetField(document, "models", std::move(serializedModels));
        if (!result)
            return UnexpectedSerializationError(result.error());

        JsonValue serializedNodes{ JsonValue::Type::Array };
        for (const auto& node : scene.nodes) {
            JsonValue serializedNode{ JsonValue::Type::Object };
            result = SetField(serializedNode, "name", JsonValue(node.name.c_str()));
            if (!result)
                return UnexpectedSerializationError(result.error());
            auto localTransform = MakeMatrix(node.localTransform.m);
            if (!localTransform)
                return UnexpectedSerializationError(localTransform.error());
            result = SetField(serializedNode, "localTransform", std::move(*localTransform));
            if (!result)
                return UnexpectedSerializationError(result.error());
            auto worldTransform = MakeMatrix(node.worldTransform.m);
            if (!worldTransform)
                return UnexpectedSerializationError(worldTransform.error());
            result = SetField(serializedNode, "worldTransform", std::move(*worldTransform));
            if (!result)
                return UnexpectedSerializationError(result.error());
            if (node.parentIndex != Scene::NoNode) {
                result = SetField(serializedNode, "parentIndex", JsonValue(static_cast<double>(node.parentIndex)));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            if (node.model) {
                const auto model = modelIndices.find(node.model);
                if (model == modelIndices.end())
                    return UnexpectedSerializationError("Scene node refers to a model not owned by the model manager", "Serializer");
                result = SetField(serializedNode, "modelIndex", JsonValue(static_cast<double>(model->second)));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            auto color = MakeArray(std::array{ node.color.x, node.color.y, node.color.z, node.color.w });
            if (!color)
                return UnexpectedSerializationError(color.error());
            result = SetField(serializedNode, "color", std::move(*color));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = SetField(serializedNode, "visible", JsonValue(node.visible));
            if (!result)
                return UnexpectedSerializationError(result.error());
            result = AddToArray(serializedNodes, std::move(serializedNode));
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

    auto Serializer::LoadScene(Scene& scene, ModelManager& models,
        render::RenderResources& resources, const Path& path) -> EngineResult<void>
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
        if (*formatVersion != 1 && *formatVersion != 2 && *formatVersion != 3 && *formatVersion != 4)
            return UnexpectedSerializationError("Unsupported scene file version", "Serializer");

        auto presetField = GetField(document, "preset");
        if (!presetField)
            return UnexpectedSerializationError(presetField.error());
        auto preset = ReadUnsigned<uint32_t>(*presetField, "preset");
        if (!preset)
            return UnexpectedSerializationError(preset.error());
        if (*preset > static_cast<uint32_t>(ScenePreset::PhysicsSandbox))
            return UnexpectedSerializationError("Scene file contains an invalid preset", "Serializer");

        Scene loadedScene;
        loadedScene.preset = static_cast<ScenePreset>(*preset);
        ModelManager loadedModels;
        auto commitLoadedScene = [&]() {
            models.Clear();
            const auto remapping = loadedModels.MoveTo(models);
            for (auto& node : loadedScene.nodes) {
                if (const auto model = remapping.find(node.model); model != remapping.end())
                    node.model = model->second;
            }
            scene = std::move(loadedScene);
        };

        if (*formatVersion == 4) {
            auto serializedModels = ReadArrayField(document, "models");
            auto serializedNodes = ReadArrayField(document, "nodes");
            if (!serializedModels || !serializedNodes)
                return UnexpectedSerializationError("Scene file is missing models or nodes", "Serializer");
            auto modelCount = serializedModels->Size();
            auto nodeCount = serializedNodes->Size();
            if (!modelCount || !nodeCount)
                return UnexpectedSerializationError("Invalid models or nodes array", "Serializer");

            loadedScene.nodes.reserve(*nodeCount);
            Vector<ModelHandler> modelHandlers;
            modelHandlers.reserve(*modelCount);
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
                const Path relativeModelPath(*modelPathText);
                if (relativeModelPath.Empty() || relativeModelPath.IsAbsolute())
                    return UnexpectedSerializationError("Scene model path must be a non-empty relative path", "Serializer");

                auto modelJson = ReadFile(path.Parent() / relativeModelPath);
                if (!modelJson)
                    return UnexpectedSerializationError(modelJson.error());
                JsonValue modelDocument;
                auto parseModelResult = modelDocument.Parse(*modelJson);
                if (!parseModelResult)
                    return UnexpectedSerializationError(parseModelResult.error());
                auto modelVersionField = GetField(modelDocument, "formatVersion");
                if (!modelVersionField)
                    return UnexpectedSerializationError(modelVersionField.error());
                auto modelVersion = ReadUnsigned<uint32_t>(*modelVersionField, "model.formatVersion");
                if (!modelVersion || (*modelVersion != 1 && *modelVersion != 2))
                    return UnexpectedSerializationError("Unsupported model file version", "Serializer");
                auto model = DeserializeModel(modelDocument);
                if (!model)
                    return UnexpectedSerializationError(model.error());
                model->Upload(resources);
                modelHandlers.push_back(loadedModels.Insert(std::move(*model)));
            }

            for (size_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
                auto serializedNode = GetElement(*serializedNodes, nodeIndex);
                if (!serializedNode)
                    return UnexpectedSerializationError(serializedNode.error());
                if (!serializedNode->IsObject())
                    return UnexpectedSerializationError("Scene node must be an object", "Serializer");
                auto localField = GetField(*serializedNode, "localTransform");
                auto worldField = GetField(*serializedNode, "worldTransform");
                auto visibleField = GetField(*serializedNode, "visible");
                auto colorField = GetField(*serializedNode, "color");
                if (!localField || !worldField || !visibleField || !colorField)
                    return UnexpectedSerializationError("Scene node is missing required fields", "Serializer");
                auto localTransform = ReadMatrix(*localField, "node.localTransform");
                auto worldTransform = ReadMatrix(*worldField, "node.worldTransform");
                auto visible = visibleField->AsBoolean();
                auto color = ReadFloatArray<4>(*colorField, "node.color");
                if (!localTransform || !worldTransform || !visible || !color)
                    return UnexpectedSerializationError("Invalid scene node data", "Serializer");

                Scene::Node node;
                node.localTransform.m = *localTransform;
                node.worldTransform.m = *worldTransform;
                node.visible = *visible;
                node.color = Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
                if (const auto parentField = serializedNode->Find("parentIndex")) {
                    auto parentIndex = ReadUnsigned<size_t>(*parentField, "node.parentIndex");
                    if (!parentIndex || *parentIndex >= *nodeCount || *parentIndex == nodeIndex)
                        return UnexpectedSerializationError("Scene node has an invalid parent index", "Serializer");
                    node.parentIndex = *parentIndex;
                }
                if (const auto modelField = serializedNode->Find("modelIndex")) {
                    auto modelIndex = ReadUnsigned<size_t>(*modelField, "node.modelIndex");
                    if (!modelIndex || *modelIndex >= *modelCount)
                        return UnexpectedSerializationError("Scene node refers to a missing model", "Serializer");
                    node.model = modelHandlers[*modelIndex];
                }
                if (const auto nameField = serializedNode->Find("name")) {
                    auto name = nameField->AsString();
                    if (!name)
                        return UnexpectedSerializationError(name.error());
                    node.name = std::move(*name);
                }
                loadedScene.nodes.push_back(std::move(node));
            }

            for (size_t nodeIndex = 0; nodeIndex < loadedScene.nodes.size(); ++nodeIndex) {
                size_t current = nodeIndex;
                for (size_t depth = 0; depth <= loadedScene.nodes.size(); ++depth) {
                    const size_t parentIndex = loadedScene.nodes[current].parentIndex;
                    if (parentIndex == Scene::NoNode)
                        break;
                    if (depth == loadedScene.nodes.size())
                        return UnexpectedSerializationError("Scene node hierarchy contains a cycle", "Serializer");
                    current = parentIndex;
                }
            }

            commitLoadedScene();
            return {};
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

        loadedScene.nodes.reserve(*instanceCount + (*formatVersion >= 3 ? *meshCount : 0));
        Vector<ModelHandler> modelHandlers;
        modelHandlers.reserve(*meshCount);
        Vector<Vector4> modelColors;
        modelColors.reserve(*meshCount);

        if (*formatVersion >= 3) {
            auto serializedNodes = ReadArrayField(document, "nodes");
            if (!serializedNodes)
                return UnexpectedSerializationError(serializedNodes.error());
            auto nodeCount = serializedNodes->Size();
            if (!nodeCount)
                return UnexpectedSerializationError(nodeCount.error());
            loadedScene.nodes.reserve(*nodeCount);
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

                Scene::Node node;
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
                loadedScene.nodes.push_back(std::move(node));
            }

            for (size_t nodeIndex = 0; nodeIndex < loadedScene.nodes.size(); ++nodeIndex) {
                size_t current = nodeIndex;
                for (size_t depth = 0; depth <= loadedScene.nodes.size(); ++depth) {
                    const size_t parentIndex = loadedScene.nodes[current].parentIndex;
                    if (parentIndex == Scene::NoNode)
                        break;
                    if (depth == loadedScene.nodes.size())
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
                Model model;
                model.AddSubmesh(std::move(*meshData), Vector4 { 1.0f, 1.0f, 1.0f, 1.0f });
                model.Upload(resources);
                modelHandlers.push_back(loadedModels.Insert(std::move(model)));
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
                model->Upload(resources);
                modelColors.push_back(modelColor);
                modelHandlers.push_back(loadedModels.Insert(std::move(*model)));
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
            if (*meshIndex >= modelHandlers.size())
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

            Scene::Node node;
            node.model = modelHandlers[*meshIndex];
            node.worldTransform.m = *worldTransform;
            node.localTransform.m = *worldTransform;
            node.color = modelColors[*meshIndex];
            auto colorField = (*serializedInstance)["color"];
            if (colorField.Exists()) {
                auto color = ReadFloatArray<4>(colorField, "instance.color");
                if (!color)
                    return UnexpectedSerializationError(color.error());
                node.color = Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
            }
            node.visible = *visible;
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
                    if (*nodeIndex >= loadedScene.nodes.size())
                        return UnexpectedSerializationError("Scene instance refers to a missing node", "Serializer");
                    node.parentIndex = *nodeIndex;
                }
            }
            loadedScene.nodes.push_back(std::move(node));
        }

        commitLoadedScene();
        return {};
    }

    auto Serializer::WriteFile(const Path& path, StringView contents) -> EngineResult<void>
    {
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
