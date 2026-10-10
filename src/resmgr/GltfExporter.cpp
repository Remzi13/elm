#include "resmgr/GltfExporter.hpp"

#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>

namespace elm::resmgr {
    namespace {
        struct BufferView {
            size_t buffer { 0 };
            size_t byteOffset { 0 };
            size_t byteLength { 0 };
            size_t byteStride { 0 };
        };

        struct Accessor {
            size_t bufferView { 0 };
            size_t byteOffset { 0 };
            size_t count { 0 };
            uint32_t componentType { 0 };
            String type;
            bool normalized { false };
        };

        struct GltfDocument {
            JsonValue root;
            Vector<Vector<uint8_t>> buffers;
            Vector<BufferView> bufferViews;
            Vector<Accessor> accessors;
        };

        auto Failure(String message) -> std::unexpected<EngineError>
        {
            return UnexpectedSerializationError(std::move(message), "GltfExporter");
        }

        auto Failure(const std::string& message) -> std::unexpected<EngineError>
        {
            return UnexpectedSerializationError(StringView(message.data(), message.size()), "GltfExporter");
        }

        auto Failure(const char* message) -> std::unexpected<EngineError>
        {
            return UnexpectedSerializationError(message, "GltfExporter");
        }

        auto SetField(JsonValue& object, StringView key, JsonValue value) -> EngineResult<void>
        {
            auto result = object.Set(key, std::move(value));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return {};
        }

        auto Push(JsonValue& array, JsonValue value) -> EngineResult<void>
        {
            auto result = array.Push(std::move(value));
            if (!result)
                return UnexpectedSerializationError(result.error());
            return {};
        }

        template<size_t N>
        auto MakeArray(const std::array<float, N>& values) -> EngineResult<JsonValue>
        {
            JsonValue array{ JsonValue::Type::Array };
            for (const float value : values) {
                auto result = Push(array, JsonValue(static_cast<double>(value)));
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            return array;
        }

        auto AppendUint32(Vector<uint8_t>& bytes, uint32_t value) -> void
        {
            bytes.push_back(static_cast<uint8_t>(value & 0xff));
            bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
            bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
            bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
        }

        auto AppendFloat(Vector<uint8_t>& bytes, float value) -> void
        {
            AppendUint32(bytes, std::bit_cast<uint32_t>(value));
        }

        auto AddNumberField(JsonValue& object, StringView key, size_t value) -> EngineResult<void>
        {
            return SetField(object, key, JsonValue(static_cast<double>(value)));
        }

        auto EncodeUri(StringView value) -> String
        {
            constexpr char hex[] = "0123456789ABCDEF";
            String encoded;
            for (const unsigned char character : value) {
                if ((character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') ||
                    character == '-' || character == '_' || character == '.' || character == '~') {
                    encoded.push_back(static_cast<char>(character));
                } else {
                    encoded.push_back('%');
                    encoded.push_back(hex[character >> 4]);
                    encoded.push_back(hex[character & 0xf]);
                }
            }
            return encoded;
        }

        auto WriteBytes(const std::filesystem::path& path, const Vector<uint8_t>& bytes) -> EngineResult<void>
        {
            std::ofstream file(path, std::ios::binary);
            if (!file)
                return Failure("Unable to open glTF output file: " + path.string());
            if (!bytes.empty())
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return Failure("Unable to write glTF output file: " + path.string());
            return {};
        }

        auto ExportModels(
            const std::filesystem::path& path,
            const Vector<Model>& models,
            std::span<const Matrix4x4> transforms) -> EngineResult<void>
        {
            if (path.empty())
                return Failure("glTF output path is empty");
            if (!transforms.empty() && transforms.size() != models.size())
                return Failure("glTF model and transform counts do not match");
            if (models.empty())
                return Failure("Cannot export an empty glTF scene");

            JsonValue root{ JsonValue::Type::Object };
            JsonValue asset{ JsonValue::Type::Object };
            auto result = SetField(asset, "version", JsonValue("2.0"));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(asset, "generator", JsonValue("elm"));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "asset", std::move(asset));
            if (!result) return UnexpectedSerializationError(result.error());

            JsonValue buffers{ JsonValue::Type::Array };
            JsonValue bufferViews{ JsonValue::Type::Array };
            JsonValue accessors{ JsonValue::Type::Array };
            JsonValue materials{ JsonValue::Type::Array };
            JsonValue meshes{ JsonValue::Type::Array };
            JsonValue nodes{ JsonValue::Type::Array };
            JsonValue sceneNodes{ JsonValue::Type::Array };
            Vector<uint8_t> binary;

            for (size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
                const auto& model = models[modelIndex];
                const auto& mesh = model.meshData;
                if (mesh.vertices.empty())
                    return Failure("Cannot export a glTF model without vertices");
                constexpr size_t maxBufferSize = std::numeric_limits<uint32_t>::max();

                Vector<uint32_t> indices = mesh.indices;
                if (indices.empty()) {
                    if (mesh.vertices.size() % 3 != 0)
                        return Failure("Non-indexed glTF model must contain a multiple of three vertices");
                    indices.reserve(mesh.vertices.size());
                    for (size_t index = 0; index < mesh.vertices.size(); ++index) {
                        if (index > std::numeric_limits<uint32_t>::max())
                            return Failure("glTF model has too many vertices");
                        indices.push_back(static_cast<uint32_t>(index));
                    }
                }
                if (indices.empty() || indices.size() % 3 != 0)
                    return Failure("glTF triangle model has an invalid index count");
                for (const uint32_t index : indices) {
                    if (index >= mesh.vertices.size())
                        return Failure("glTF model index is outside the vertex array");
                }
                const size_t alignedOffset = binary.size() + ((4 - binary.size() % 4) % 4);
                if (alignedOffset > maxBufferSize ||
                    mesh.vertices.size() > (maxBufferSize - alignedOffset) / 32)
                    return Failure("glTF model exceeds the 32-bit buffer limit");
                const size_t vertexEnd = alignedOffset + mesh.vertices.size() * 32;
                if (indices.size() > (maxBufferSize - vertexEnd) / sizeof(uint32_t))
                    return Failure("glTF model exceeds the 32-bit buffer limit");

                while (binary.size() % 4 != 0)
                    binary.push_back(0);
                const size_t vertexOffset = binary.size();
                Vector3 minimum{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
                Vector3 maximum{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
                for (const auto& vertex : mesh.vertices) {
                    const std::array<float, 8> components{
                        vertex.position.x, vertex.position.y, vertex.position.z,
                        vertex.normal.x, vertex.normal.y, vertex.normal.z,
                        vertex.u, vertex.v
                    };
                    for (const float component : components) {
                        if (!std::isfinite(component))
                            return Failure("Cannot export non-finite vertex data to glTF");
                        AppendFloat(binary, component);
                    }
                    minimum.x = (std::min)(minimum.x, vertex.position.x);
                    minimum.y = (std::min)(minimum.y, vertex.position.y);
                    minimum.z = (std::min)(minimum.z, vertex.position.z);
                    maximum.x = (std::max)(maximum.x, vertex.position.x);
                    maximum.y = (std::max)(maximum.y, vertex.position.y);
                    maximum.z = (std::max)(maximum.z, vertex.position.z);
                }
                if (binary.size() > std::numeric_limits<uint32_t>::max())
                    return Failure("glTF model exceeds the 32-bit buffer limit");
                const size_t vertexLength = binary.size() - vertexOffset;

                JsonValue vertexView{ JsonValue::Type::Object };
                result = AddNumberField(vertexView, "buffer", 0);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(vertexView, "byteOffset", vertexOffset);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(vertexView, "byteLength", vertexLength);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(vertexView, "byteStride", 32);
                if (!result) return UnexpectedSerializationError(result.error());
                const size_t vertexViewIndex = *bufferViews.Size();
                result = Push(bufferViews, std::move(vertexView));
                if (!result) return UnexpectedSerializationError(result.error());

                const size_t positionAccessorIndex = *accessors.Size();
                for (const auto& [offset, type, minimumValues, maximumValues] : {
                    std::tuple<size_t, const char*, std::array<float, 3>, std::array<float, 3>>{
                        0, "VEC3", { minimum.x, minimum.y, minimum.z }, { maximum.x, maximum.y, maximum.z } },
                    std::tuple<size_t, const char*, std::array<float, 3>, std::array<float, 3>>{
                        12, "VEC3", {}, {} },
                    std::tuple<size_t, const char*, std::array<float, 3>, std::array<float, 3>>{
                        24, "VEC2", {}, {} }
                }) {
                    JsonValue accessor{ JsonValue::Type::Object };
                    result = AddNumberField(accessor, "bufferView", vertexViewIndex);
                    if (!result) return UnexpectedSerializationError(result.error());
                    result = AddNumberField(accessor, "byteOffset", offset);
                    if (!result) return UnexpectedSerializationError(result.error());
                    result = SetField(accessor, "componentType", JsonValue(5126));
                    if (!result) return UnexpectedSerializationError(result.error());
                    result = AddNumberField(accessor, "count", mesh.vertices.size());
                    if (!result) return UnexpectedSerializationError(result.error());
                    result = SetField(accessor, "type", JsonValue(type));
                    if (!result) return UnexpectedSerializationError(result.error());
                    if (offset == 0) {
                        auto minArray = MakeArray(minimumValues);
                        auto maxArray = MakeArray(maximumValues);
                        if (!minArray || !maxArray)
                            return Failure("Unable to create glTF position bounds");
                        result = SetField(accessor, "min", std::move(*minArray));
                        if (!result) return UnexpectedSerializationError(result.error());
                        result = SetField(accessor, "max", std::move(*maxArray));
                        if (!result) return UnexpectedSerializationError(result.error());
                    }
                    result = Push(accessors, std::move(accessor));
                    if (!result) return UnexpectedSerializationError(result.error());
                }

                while (binary.size() % 4 != 0)
                    binary.push_back(0);
                const size_t indexOffset = binary.size();
                for (const uint32_t index : indices)
                    AppendUint32(binary, index);
                if (binary.size() > std::numeric_limits<uint32_t>::max())
                    return Failure("glTF model exceeds the 32-bit buffer limit");

                JsonValue indexView{ JsonValue::Type::Object };
                result = AddNumberField(indexView, "buffer", 0);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(indexView, "byteOffset", indexOffset);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(indexView, "byteLength", binary.size() - indexOffset);
                if (!result) return UnexpectedSerializationError(result.error());
                const size_t indexViewIndex = *bufferViews.Size();
                result = Push(bufferViews, std::move(indexView));
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue indexAccessor{ JsonValue::Type::Object };
                result = AddNumberField(indexAccessor, "bufferView", indexViewIndex);
                if (!result) return UnexpectedSerializationError(result.error());
                result = SetField(indexAccessor, "componentType", JsonValue(5125));
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(indexAccessor, "count", indices.size());
                if (!result) return UnexpectedSerializationError(result.error());
                result = SetField(indexAccessor, "type", JsonValue("SCALAR"));
                if (!result) return UnexpectedSerializationError(result.error());
                const size_t indexAccessorIndex = *accessors.Size();
                result = Push(accessors, std::move(indexAccessor));
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue attributes{ JsonValue::Type::Object };
                result = AddNumberField(attributes, "POSITION", positionAccessorIndex);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(attributes, "NORMAL", positionAccessorIndex + 1);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(attributes, "TEXCOORD_0", positionAccessorIndex + 2);
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue primitive{ JsonValue::Type::Object };
                result = SetField(primitive, "attributes", std::move(attributes));
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(primitive, "indices", indexAccessorIndex);
                if (!result) return UnexpectedSerializationError(result.error());
                result = AddNumberField(primitive, "material", modelIndex);
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue primitives{ JsonValue::Type::Array };
                result = Push(primitives, std::move(primitive));
                if (!result) return UnexpectedSerializationError(result.error());
                JsonValue meshObject{ JsonValue::Type::Object };
                if (!model.name.empty()) {
                    result = SetField(meshObject, "name", JsonValue(model.name.c_str()));
                    if (!result) return UnexpectedSerializationError(result.error());
                }
                result = SetField(meshObject, "primitives", std::move(primitives));
                if (!result) return UnexpectedSerializationError(result.error());
                result = Push(meshes, std::move(meshObject));
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue color{ JsonValue::Type::Array };
                for (const float component : { model.color.x, model.color.y, model.color.z, model.color.w }) {
                    if (!std::isfinite(component))
                        return Failure("Cannot export non-finite material color to glTF");
                    result = Push(color, JsonValue(static_cast<double>(component)));
                    if (!result) return UnexpectedSerializationError(result.error());
                }
                JsonValue pbr{ JsonValue::Type::Object };
                result = SetField(pbr, "baseColorFactor", std::move(color));
                if (!result) return UnexpectedSerializationError(result.error());
                JsonValue material{ JsonValue::Type::Object };
                result = SetField(material, "pbrMetallicRoughness", std::move(pbr));
                if (!result) return UnexpectedSerializationError(result.error());
                result = Push(materials, std::move(material));
                if (!result) return UnexpectedSerializationError(result.error());

                JsonValue node{ JsonValue::Type::Object };
                if (!model.name.empty()) {
                    result = SetField(node, "name", JsonValue(model.name.c_str()));
                    if (!result) return UnexpectedSerializationError(result.error());
                }
                result = AddNumberField(node, "mesh", modelIndex);
                if (!result) return UnexpectedSerializationError(result.error());
                if (!transforms.empty()) {
                    JsonValue matrix{ JsonValue::Type::Array };
                    const auto& transform = transforms[modelIndex];
                    for (size_t column = 0; column < 4; ++column) {
                        for (size_t row = 0; row < 4; ++row) {
                            const float value = transform(row, column);
                            if (!std::isfinite(value))
                                return Failure("Cannot export non-finite node transform to glTF");
                            result = Push(matrix, JsonValue(static_cast<double>(value)));
                            if (!result) return UnexpectedSerializationError(result.error());
                        }
                    }
                    result = SetField(node, "matrix", std::move(matrix));
                    if (!result) return UnexpectedSerializationError(result.error());
                }
                result = Push(nodes, std::move(node));
                if (!result) return UnexpectedSerializationError(result.error());
                result = Push(sceneNodes, JsonValue(static_cast<double>(modelIndex)));
                if (!result) return UnexpectedSerializationError(result.error());
            }

            const auto binaryPath = path.parent_path() / (path.stem().string() + ".bin");
            JsonValue buffer{ JsonValue::Type::Object };
            result = SetField(buffer, "uri", JsonValue(EncodeUri(binaryPath.filename().generic_string())));
            if (!result) return UnexpectedSerializationError(result.error());
            result = AddNumberField(buffer, "byteLength", binary.size());
            if (!result) return UnexpectedSerializationError(result.error());
            result = Push(buffers, std::move(buffer));
            if (!result) return UnexpectedSerializationError(result.error());

            result = SetField(root, "buffers", std::move(buffers));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "bufferViews", std::move(bufferViews));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "accessors", std::move(accessors));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "materials", std::move(materials));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "meshes", std::move(meshes));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "nodes", std::move(nodes));
            if (!result) return UnexpectedSerializationError(result.error());
            JsonValue scene{ JsonValue::Type::Object };
            result = SetField(scene, "nodes", std::move(sceneNodes));
            if (!result) return UnexpectedSerializationError(result.error());
            JsonValue scenes{ JsonValue::Type::Array };
            result = Push(scenes, std::move(scene));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "scenes", std::move(scenes));
            if (!result) return UnexpectedSerializationError(result.error());
            result = SetField(root, "scene", JsonValue(0));
            if (!result) return UnexpectedSerializationError(result.error());

            auto json = root.ToString();
            if (!json)
                return UnexpectedSerializationError(json.error());
            auto writeBinary = WriteBytes(binaryPath, binary);
            if (!writeBinary)
                return UnexpectedSerializationError(writeBinary.error());
            std::ofstream jsonFile(path, std::ios::binary);
            if (!jsonFile)
                return Failure("Unable to open glTF output file: " + path.string());
            jsonFile.write(json->data(), static_cast<std::streamsize>(json->size()));
            if (!jsonFile)
                return Failure("Unable to write glTF output file: " + path.string());
            return {};
        }

        auto Required(const JsonValue& object, StringView key) -> EngineResult<JsonValue>
        {
            auto value = object.Find(key);
            if (!value)
                return Failure("Missing glTF field: " + String(key));
            return std::move(*value);
        }

        auto Optional(const JsonValue& object, StringView key) -> std::optional<JsonValue>
        {
            return object.Find(key);
        }

        auto ReadNumber(const JsonValue& value, StringView description) -> EngineResult<double>
        {
            auto number = value.AsNumber();
            if (!number || !std::isfinite(*number))
                return Failure("Invalid numeric glTF field: " + String(description));
            return *number;
        }

        template<typename T>
        auto ReadUnsigned(const JsonValue& value, StringView description) -> EngineResult<T>
        {
            auto number = ReadNumber(value, description);
            if (!number)
                return UnexpectedSerializationError(number.error());

            constexpr bool maxExceedsDoublePrecision = std::numeric_limits<T>::digits > std::numeric_limits<double>::digits;
            const double maximum = static_cast<double>(std::numeric_limits<T>::max());
            if (*number < 0.0 ||
                (maxExceedsDoublePrecision ? *number >= maximum : *number > maximum) ||
                std::floor(*number) != *number)
                return Failure("Invalid unsigned integer glTF field: " + String(description));

            return static_cast<T>(*number);
        }

        template<size_t N>
        auto ReadFloatArray(const JsonValue& value, StringView description) -> EngineResult<std::array<float, N>>
        {
            if (!value.IsArray())
                return Failure("Expected an array for glTF field: " + String(description));

            auto size = value.Size();
            if (!size || *size != N)
                return Failure("Invalid array length for glTF field: " + String(description));

            std::array<float, N> result{};
            for (size_t index = 0; index < N; ++index) {
                auto component = value.At(index);
                auto number = ReadNumber(component, description);
                if (!number || *number < -std::numeric_limits<float>::max() || *number > std::numeric_limits<float>::max())
                    return Failure("Invalid float component in glTF field: " + String(description));
                result[index] = static_cast<float>(*number);
            }
            return result;
        }

        auto ReadFile(const std::filesystem::path& path) -> EngineResult<Vector<uint8_t>>
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return Failure("Unable to open glTF resource: " + path.string());

            file.seekg(0, std::ios::end);
            const std::streamoff length = file.tellg();
            if (length < 0)
                return Failure(String("Unable to determine glTF resource size: ") + String(path.string().c_str()));
            if (static_cast<std::uintmax_t>(length) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
                return Failure("glTF resource is too large to read: " + path.string());
            file.seekg(0, std::ios::beg);

            Vector<uint8_t> bytes(static_cast<size_t>(length));
            if (!bytes.empty()) {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!file)
                    return Failure(String("Unable to read glTF resource: ") + String(path.string().c_str()  ));
            }
            return bytes;
        }

        auto DecodeBase64(StringView encoded) -> EngineResult<Vector<uint8_t>>
        {
            constexpr StringView alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            Vector<uint8_t> decoded;
            decoded.reserve(encoded.size() * 3 / 4);

            uint32_t accumulator = 0;
            unsigned bits = 0;
            bool paddingStarted = false;
            size_t paddingCount = 0;
            for (const char character : encoded) {
                if (character == '=') {
                    paddingStarted = true;
                    ++paddingCount;
                    if (paddingCount > 2)
                        return Failure("Invalid base64 padding in glTF buffer");
                    continue;
                }
                if (character == ' ' || character == '\t' || character == '\r' || character == '\n')
                    continue;
                if (paddingStarted)
                    return Failure("Invalid base64 data after padding in glTF buffer");

                const auto position = alphabet.find(character);
                if (position == StringView::npos)
                    return Failure("Invalid character in base64 glTF buffer");
                accumulator = (accumulator << 6) | static_cast<uint32_t>(position);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    decoded.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
                }
            }

            if (bits == 6 || (paddingCount == 1 && bits != 2) || (paddingCount == 2 && bits != 4))
                return Failure("Invalid base64 length in glTF buffer");
            return decoded;
        }

        auto DecodeUri(StringView uri) -> EngineResult<String>
        {
            String decoded;
            decoded.reserve(uri.size());
            auto hexValue = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                return -1;
            };

            for (size_t index = 0; index < uri.size(); ++index) {
                if (uri[index] != '%') {
                    decoded.push_back(uri[index]);
                    continue;
                }
                if (index + 2 >= uri.size())
                    return Failure("Invalid percent escape in glTF buffer URI");
                const int high = hexValue(uri[index + 1]);
                const int low = hexValue(uri[index + 2]);
                if (high < 0 || low < 0)
                    return Failure("Invalid percent escape in glTF buffer URI");
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
            }
            return decoded;
        }

        auto LoadBufferUri(const std::filesystem::path& gltfPath, StringView uri) -> EngineResult<Vector<uint8_t>>
        {
            constexpr StringView dataPrefix = "data:";
            if (uri.starts_with(dataPrefix)) {
                const size_t comma = uri.find(',');
                if (comma == StringView::npos)
                    return Failure("Invalid data URI in glTF buffer");
                const StringView metadata = uri.substr(0, comma);
                if (!metadata.ends_with(";base64"))
                    return Failure("Only base64 data URIs are supported for glTF buffers");
                return DecodeBase64(uri.substr(comma + 1));
            }

            auto decodedUri = DecodeUri(uri);
            if (!decodedUri)
                return UnexpectedSerializationError(decodedUri.error());
            std::u8string utf8Uri;
            utf8Uri.reserve(decodedUri->size());
            for (const unsigned char character : *decodedUri)
                utf8Uri.push_back(static_cast<char8_t>(character));
            const std::filesystem::path bufferPath = gltfPath.parent_path() / std::filesystem::path(std::move(utf8Uri));
            return ReadFile(bufferPath);
        }

        auto LoadDocument(const std::filesystem::path& path) -> EngineResult<GltfDocument>
        {
            auto jsonBytes = ReadFile(path);
            if (!jsonBytes)
                return UnexpectedSerializationError(jsonBytes.error());
            if (jsonBytes->empty())
                return Failure("glTF JSON file is empty");

            GltfDocument document;
            const StringView json(reinterpret_cast<const char*>(jsonBytes->data()), jsonBytes->size());
            if (auto parsed = document.root.Parse(json); !parsed)
                return Failure("Unable to parse glTF JSON: " + parsed.error().message);
            if (!document.root.IsObject())
                return Failure("glTF document root must be an object");

            auto asset = Required(document.root, "asset");
            if (!asset)
                return UnexpectedSerializationError(asset.error());
            auto versionField = Required(*asset, "version");
            if (!versionField)
                return UnexpectedSerializationError(versionField.error());
            auto version = versionField->AsString();
            if (!version || !version->starts_with("2."))
                return Failure("Only glTF 2.x assets are supported");

            if (const auto buffersField = Optional(document.root, "buffers")) {
                if (!buffersField->IsArray())
                    return Failure("glTF buffers must be an array");
                auto bufferCount = buffersField->Size();
                if (!bufferCount)
                    return UnexpectedSerializationError(bufferCount.error());
                document.buffers.reserve(*bufferCount);
                for (size_t index = 0; index < *bufferCount; ++index) {
                    auto buffer = buffersField->At(index);
                    if (!buffer.IsObject())
                        return Failure("glTF buffer entry must be an object");
                    auto uriField = Required(buffer, "uri");
                    if (!uriField)
                        return Failure("glTF buffer is missing its URI (binary .glb buffers are not supported)");
                    auto uri = uriField->AsString();
                    if (!uri)
                        return Failure("Invalid URI in glTF buffer");
                    auto bytes = LoadBufferUri(path, StringView(uri->data(), uri->size()));
                    if (!bytes)
                        return UnexpectedSerializationError(bytes.error());
                    auto byteLengthField = Required(buffer, "byteLength");
                    if (!byteLengthField)
                        return UnexpectedSerializationError(byteLengthField.error());
                    auto byteLength = ReadUnsigned<size_t>(*byteLengthField, "buffers.byteLength");
                    if (!byteLength || *byteLength > bytes->size())
                        return Failure("glTF buffer is shorter than its declared byteLength");
                    document.buffers.push_back(std::move(*bytes));
                }
            }

            if (const auto viewsField = Optional(document.root, "bufferViews")) {
                if (!viewsField->IsArray())
                    return Failure("glTF bufferViews must be an array");
                auto viewCount = viewsField->Size();
                if (!viewCount)
                    return UnexpectedSerializationError(viewCount.error());
                document.bufferViews.reserve(*viewCount);
                for (size_t index = 0; index < *viewCount; ++index) {
                    const auto view = viewsField->At(index);
                    if (!view.IsObject())
                        return Failure("glTF bufferView entry must be an object");
                    auto bufferField = Required(view, "buffer");
                    auto lengthField = Required(view, "byteLength");
                    if (!bufferField || !lengthField)
                        return Failure("Invalid glTF bufferView");
                    auto buffer = ReadUnsigned<size_t>(*bufferField, "bufferViews.buffer");
                    auto length = ReadUnsigned<size_t>(*lengthField, "bufferViews.byteLength");
                    if (!buffer || !length || *buffer >= document.buffers.size())
                        return Failure("glTF bufferView references an invalid buffer");
                    size_t offset = 0;
                    size_t stride = 0;
                    if (const auto field = Optional(view, "byteOffset")) {
                        auto value = ReadUnsigned<size_t>(*field, "bufferViews.byteOffset");
                        if (!value) return UnexpectedSerializationError(value.error());
                        offset = *value;
                    }
                    if (const auto field = Optional(view, "byteStride")) {
                        auto value = ReadUnsigned<size_t>(*field, "bufferViews.byteStride");
                        if (!value) return UnexpectedSerializationError(value.error());
                        stride = *value;
                    }
                    const auto bufferSize = document.buffers[*buffer].size();
                    if (offset > bufferSize || *length > bufferSize - offset)
                        return Failure("glTF bufferView is outside its buffer");
                    document.bufferViews.push_back({ *buffer, offset, *length, stride });
                }
            }

            if (const auto accessorsField = Optional(document.root, "accessors")) {
                if (!accessorsField->IsArray())
                    return Failure("glTF accessors must be an array");
                auto accessorCount = accessorsField->Size();
                if (!accessorCount)
                    return UnexpectedSerializationError(accessorCount.error());
                document.accessors.reserve(*accessorCount);
                for (size_t index = 0; index < *accessorCount; ++index) {
                    const auto accessor = accessorsField->At(index);
                    if (!accessor.IsObject())
                        return Failure("glTF accessor entry must be an object");
                    auto viewField = Required(accessor, "bufferView");
                    auto countField = Required(accessor, "count");
                    auto componentTypeField = Required(accessor, "componentType");
                    auto typeField = Required(accessor, "type");
                    if (!viewField || !countField || !componentTypeField || !typeField)
                        return Failure("Invalid glTF accessor (sparse accessors are not supported)");
                    auto view = ReadUnsigned<size_t>(*viewField, "accessors.bufferView");
                    auto count = ReadUnsigned<size_t>(*countField, "accessors.count");
                    auto componentType = ReadUnsigned<uint32_t>(*componentTypeField, "accessors.componentType");
                    auto type = typeField->AsString();
                    if (!view || !count || !componentType || !type || *view >= document.bufferViews.size())
                        return Failure("glTF accessor references invalid data");

                    size_t offset = 0;
                    bool normalized = false;
                    if (const auto field = Optional(accessor, "byteOffset")) {
                        auto value = ReadUnsigned<size_t>(*field, "accessors.byteOffset");
                        if (!value) return UnexpectedSerializationError(value.error());
                        offset = *value;
                    }
                    if (const auto field = Optional(accessor, "normalized")) {
                        auto value = field->AsBoolean();
                        if (!value) return Failure("Invalid normalized flag in glTF accessor");
                        normalized = *value;
                    }
                    document.accessors.push_back({ *view, offset, *count, *componentType, std::move(*type), normalized });
                }
            }

            return document;
        }

        auto Components(StringView type) -> size_t
        {
            if (type == "SCALAR") return 1;
            if (type == "VEC2") return 2;
            if (type == "VEC3") return 3;
            if (type == "VEC4") return 4;
            return 0;
        }

        auto AccessorBytes(const GltfDocument& document, const Accessor& accessor, size_t elementSize, size_t index)
            -> EngineResult<const uint8_t*>
        {
            const auto& view = document.bufferViews[accessor.bufferView];
            const size_t stride = view.byteStride == 0 ? elementSize : view.byteStride;
            if (stride < elementSize || accessor.byteOffset > view.byteLength)
                return Failure("Invalid stride or offset in glTF accessor");
            if (index >= accessor.count)
                return Failure("glTF accessor index is out of range");
            const size_t remaining = view.byteLength - accessor.byteOffset;
            if (elementSize > remaining || index > (remaining - elementSize) / stride)
                return Failure("glTF accessor extends beyond its bufferView");

            const auto& buffer = document.buffers[view.buffer];
            const size_t offset = view.byteOffset + accessor.byteOffset + index * stride;
            if (offset > buffer.size() || elementSize > buffer.size() - offset)
                return Failure("glTF accessor extends beyond its buffer");
            return buffer.data() + offset;
        }

        auto ReadFloat(const uint8_t* bytes) -> float
        {
            const uint32_t bits = static_cast<uint32_t>(bytes[0])
                | (static_cast<uint32_t>(bytes[1]) << 8)
                | (static_cast<uint32_t>(bytes[2]) << 16)
                | (static_cast<uint32_t>(bytes[3]) << 24);
            return std::bit_cast<float>(bits);
        }

        auto ReadFloatAccessor(const GltfDocument& document, size_t accessorIndex, StringView expectedType)
            -> EngineResult<Vector<std::array<float, 4>>>
        {
            if (accessorIndex >= document.accessors.size())
                return Failure("glTF primitive references an invalid accessor");
            const auto& accessor = document.accessors[accessorIndex];
            const size_t components = Components(expectedType);
            if (accessor.type != expectedType || accessor.componentType != 5126 || accessor.normalized || components == 0)
                return Failure("Unsupported glTF vertex attribute format");

            const size_t elementSize = components * sizeof(float);
            Vector<std::array<float, 4>> values;
            values.resize(accessor.count);
            for (size_t index = 0; index < accessor.count; ++index) {
                auto bytes = AccessorBytes(document, accessor, elementSize, index);
                if (!bytes)
                    return UnexpectedSerializationError(bytes.error());
                for (size_t component = 0; component < components; ++component) {
                    const float value = ReadFloat(*bytes + component * sizeof(float));
                    if (!std::isfinite(value))
                        return Failure("Non-finite vertex attribute in glTF");
                    values[index][component] = value;
                }
            }
            return values;
        }

        auto ReadIndexAccessor(const GltfDocument& document, size_t accessorIndex) -> EngineResult<Vector<uint32_t>>
        {
            if (accessorIndex >= document.accessors.size())
                return Failure("glTF primitive references an invalid index accessor");
            const auto& accessor = document.accessors[accessorIndex];
            if (accessor.type != "SCALAR" || accessor.normalized)
                return Failure("glTF indices must use a non-normalized SCALAR accessor");

            size_t componentSize = 0;
            switch (accessor.componentType) {
            case 5121: componentSize = 1; break;
            case 5123: componentSize = 2; break;
            case 5125: componentSize = 4; break;
            default: return Failure("Unsupported glTF index component type");
            }

            Vector<uint32_t> indices;
            indices.reserve(accessor.count);
            for (size_t index = 0; index < accessor.count; ++index) {
                auto bytes = AccessorBytes(document, accessor, componentSize, index);
                if (!bytes)
                    return UnexpectedSerializationError(bytes.error());
                uint32_t value = (*bytes)[0];
                if (componentSize >= 2)
                    value |= static_cast<uint32_t>((*bytes)[1]) << 8;
                if (componentSize == 4) {
                    value |= static_cast<uint32_t>((*bytes)[2]) << 16;
                    value |= static_cast<uint32_t>((*bytes)[3]) << 24;
                }
                indices.push_back(value);
            }
            return indices;
        }

        auto ReadMaterialColor(const GltfDocument& document, const JsonValue& primitive) -> EngineResult<Vector4>
        {
            const auto materialField = Optional(primitive, "material");
            if (!materialField)
                return Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };

            auto materialIndex = ReadUnsigned<size_t>(*materialField, "primitives.material");
            if (!materialIndex)
                return UnexpectedSerializationError(materialIndex.error());
            auto materials = Required(document.root, "materials");
            if (!materials)
                return Failure("glTF primitive references a missing material array");
            if (!materials->IsArray())
                return Failure("glTF materials must be an array");
            auto materialCount = materials->Size();
            if (!materialCount || *materialIndex >= *materialCount)
                return Failure("glTF primitive references an invalid material");
            auto material = materials->At(*materialIndex);
            if (!material.IsObject())
                return Failure("glTF material entry must be an object");
            const auto pbr = Optional(material, "pbrMetallicRoughness");
            if (!pbr)
                return Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
            if (!pbr->IsObject())
                return Failure("glTF pbrMetallicRoughness must be an object");
            auto factor = Optional(*pbr, "baseColorFactor");
            if (!factor)
                return Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
            auto color = ReadFloatArray<4>(*factor, "materials.pbrMetallicRoughness.baseColorFactor");
            if (!color)
                return UnexpectedSerializationError(color.error());
            return Vector4{ (*color)[0], (*color)[1], (*color)[2], (*color)[3] };
        }

        auto DecodePrimitive(const GltfDocument& document, const JsonValue& primitive) -> EngineResult<Model>
        {
            if (!primitive.IsObject())
                return Failure("glTF primitive entry must be an object");
            if (const auto mode = Optional(primitive, "mode")) {
                auto value = ReadUnsigned<uint32_t>(*mode, "primitives.mode");
                if (!value || *value != 4)
                    return Failure("Only triangle-list glTF primitives are supported");
            }

            auto attributes = Required(primitive, "attributes");
            if (!attributes)
                return UnexpectedSerializationError(attributes.error());
            if (!attributes->IsObject())
                return Failure("glTF primitive attributes must be an object");
            auto positionField = Required(*attributes, "POSITION");
            if (!positionField)
                return Failure("glTF primitive is missing its POSITION attribute");
            auto positionIndex = ReadUnsigned<size_t>(*positionField, "attributes.POSITION");
            if (!positionIndex)
                return UnexpectedSerializationError(positionIndex.error());
            auto positions = ReadFloatAccessor(document, *positionIndex, "VEC3");
            if (!positions)
                return UnexpectedSerializationError(positions.error());
            if (positions->empty())
                return Failure("glTF primitive has no vertices");

            Vector<std::array<float, 4>> normals;
            if (const auto field = Optional(*attributes, "NORMAL")) {
                auto index = ReadUnsigned<size_t>(*field, "attributes.NORMAL");
                if (!index)
                    return UnexpectedSerializationError(index.error());
                auto values = ReadFloatAccessor(document, *index, "VEC3");
                if (!values || values->size() != positions->size())
                    return Failure("glTF NORMAL count does not match POSITION");
                normals = std::move(*values);
            }

            Vector<std::array<float, 4>> texcoords;
            if (const auto field = Optional(*attributes, "TEXCOORD_0")) {
                auto index = ReadUnsigned<size_t>(*field, "attributes.TEXCOORD_0");
                if (!index)
                    return UnexpectedSerializationError(index.error());
                auto values = ReadFloatAccessor(document, *index, "VEC2");
                if (!values || values->size() != positions->size())
                    return Failure("glTF TEXCOORD_0 count does not match POSITION");
                texcoords = std::move(*values);
            }

            Model model;
            model.meshData.vertices.reserve(positions->size());
            model.meshData.localBounds = AABB{};
            for (size_t index = 0; index < positions->size(); ++index) {
                const auto& position = (*positions)[index];
                const auto normal = normals.empty() ? std::array<float, 4>{} : normals[index];
                const auto uv = texcoords.empty() ? std::array<float, 4>{} : texcoords[index];
                const Vector3 point{ position[0], position[1], position[2] };
                model.meshData.vertices.push_back({ point, { normal[0], normal[1], normal[2] }, uv[0], uv[1] });
                model.meshData.localBounds.minBounds.x = std::min(model.meshData.localBounds.minBounds.x, point.x);
                model.meshData.localBounds.minBounds.y = std::min(model.meshData.localBounds.minBounds.y, point.y);
                model.meshData.localBounds.minBounds.z = std::min(model.meshData.localBounds.minBounds.z, point.z);
                model.meshData.localBounds.maxBounds.x = std::max(model.meshData.localBounds.maxBounds.x, point.x);
                model.meshData.localBounds.maxBounds.y = std::max(model.meshData.localBounds.maxBounds.y, point.y);
                model.meshData.localBounds.maxBounds.z = std::max(model.meshData.localBounds.maxBounds.z, point.z);
            }

            if (const auto indicesField = Optional(primitive, "indices")) {
                auto accessorIndex = ReadUnsigned<size_t>(*indicesField, "primitives.indices");
                if (!accessorIndex)
                    return UnexpectedSerializationError(accessorIndex.error());
                auto indices = ReadIndexAccessor(document, *accessorIndex);
                if (!indices)
                    return UnexpectedSerializationError(indices.error());
                model.meshData.indices = std::move(*indices);
            } else {
                model.meshData.indices.reserve(model.meshData.vertices.size());
                for (size_t index = 0; index < model.meshData.vertices.size(); ++index) {
                    if (index > std::numeric_limits<uint32_t>::max())
                        return Failure("glTF primitive has too many vertices for engine indices");
                    model.meshData.indices.push_back(static_cast<uint32_t>(index));
                }
            }

            if (model.meshData.indices.empty() || model.meshData.indices.size() % 3 != 0)
                return Failure("glTF triangle primitive has an invalid index count");
            for (const auto index : model.meshData.indices) {
                if (index >= model.meshData.vertices.size())
                    return Failure("glTF primitive index is outside the vertex array");
            }

            if (normals.empty()) {
                Vector<Vector3> generatedNormals(model.meshData.vertices.size());
                for (size_t index = 0; index < model.meshData.indices.size(); index += 3) {
                    const auto a = model.meshData.indices[index];
                    const auto b = model.meshData.indices[index + 1];
                    const auto c = model.meshData.indices[index + 2];
                    const Vector3 edgeA = model.meshData.vertices[b].position - model.meshData.vertices[a].position;
                    const Vector3 edgeB = model.meshData.vertices[c].position - model.meshData.vertices[a].position;
                    const Vector3 normal = edgeA.Cross(edgeB);
                    generatedNormals[a] += normal;
                    generatedNormals[b] += normal;
                    generatedNormals[c] += normal;
                }
                for (size_t index = 0; index < generatedNormals.size(); ++index)
                    model.meshData.vertices[index].normal = generatedNormals[index].Normalized();
            }

            auto color = ReadMaterialColor(document, primitive);
            if (!color)
                return UnexpectedSerializationError(color.error());
            model.color = *color;
            return model;
        }

        auto ReadVector3Field(const JsonValue& object, StringView key, const Vector3& fallback) -> EngineResult<Vector3>
        {
            const auto field = Optional(object, key);
            if (!field)
                return fallback;
            auto values = ReadFloatArray<3>(*field, key);
            if (!values)
                return UnexpectedSerializationError(values.error());
            return Vector3{ (*values)[0], (*values)[1], (*values)[2] };
        }

        auto ReadNodeTransform(const JsonValue& node) -> EngineResult<Matrix4x4>
        {
            if (!node.IsObject())
                return Failure("glTF node entry must be an object");
            if (const auto matrixField = Optional(node, "matrix")) {
                auto values = ReadFloatArray<16>(*matrixField, "nodes.matrix");
                if (!values)
                    return UnexpectedSerializationError(values.error());
                Matrix4x4 matrix;
                for (size_t row = 0; row < 4; ++row) {
                    for (size_t column = 0; column < 4; ++column)
                        matrix(row, column) = (*values)[column * 4 + row];
                }
                return matrix;
            }

            auto translation = ReadVector3Field(node, "translation", { 0.0f, 0.0f, 0.0f });
            auto scale = ReadVector3Field(node, "scale", { 1.0f, 1.0f, 1.0f });
            if (!translation || !scale)
                return Failure("Invalid TRS transform in glTF node");

            std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
            if (const auto rotationField = Optional(node, "rotation")) {
                auto values = ReadFloatArray<4>(*rotationField, "nodes.rotation");
                if (!values)
                    return UnexpectedSerializationError(values.error());
                rotation = *values;
            }
            const float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
            Matrix4x4 rotate = Matrix4x4::Identity();
            rotate(0, 0) = 1.0f - 2.0f * (y * y + z * z);
            rotate(0, 1) = 2.0f * (x * y - z * w);
            rotate(0, 2) = 2.0f * (x * z + y * w);
            rotate(1, 0) = 2.0f * (x * y + z * w);
            rotate(1, 1) = 1.0f - 2.0f * (x * x + z * z);
            rotate(1, 2) = 2.0f * (y * z - x * w);
            rotate(2, 0) = 2.0f * (x * z - y * w);
            rotate(2, 1) = 2.0f * (y * z + x * w);
            rotate(2, 2) = 1.0f - 2.0f * (x * x + y * y);

            return Matrix4x4::Translation(*translation) * rotate * Matrix4x4::Scaling(*scale);
        }

        auto ReadNodeChildIndices(const JsonValue& node) -> EngineResult<Vector<size_t>>
        {
            if (!node.IsObject())
                return Failure("glTF node entry must be an object");
            Vector<size_t> children;
            const auto field = Optional(node, "children");
            if (!field)
                return children;
            if (!field->IsArray())
                return Failure("glTF node children must be an array");
            auto count = field->Size();
            if (!count)
                return UnexpectedSerializationError(count.error());
            children.reserve(*count);
            for (size_t index = 0; index < *count; ++index) {
                auto child = ReadUnsigned<size_t>(field->At(index), "nodes.children");
                if (!child)
                    return UnexpectedSerializationError(child.error());
                children.push_back(*child);
            }
            return children;
        }

        auto ReadMeshPrimitives(const GltfDocument& document, size_t meshIndex) -> EngineResult<Vector<Model>>
        {
            auto meshes = Required(document.root, "meshes");
            if (!meshes)
                return UnexpectedSerializationError(meshes.error());
            if (!meshes->IsArray())
                return Failure("glTF meshes must be an array");
            auto meshCount = meshes->Size();
            if (!meshCount || meshIndex >= *meshCount)
                return Failure("glTF node references an invalid mesh");
            auto mesh = meshes->At(meshIndex);
            String meshName;
            if (const auto nameField = Optional(mesh, "name")) {
                auto name = nameField->AsString();
                if (!name)
                    return Failure("Invalid glTF mesh name");
                meshName = std::move(*name);
            }
            auto primitives = Required(mesh, "primitives");
            if (!primitives)
                return UnexpectedSerializationError(primitives.error());
            if (!primitives->IsArray())
                return Failure("glTF mesh primitives must be an array");
            auto primitiveCount = primitives->Size();
            if (!primitiveCount || *primitiveCount == 0)
                return Failure("glTF mesh has no primitives");

            Vector<Model> result;
            result.reserve(*primitiveCount);
            for (size_t index = 0; index < *primitiveCount; ++index) {
                auto model = DecodePrimitive(document, primitives->At(index));
                if (!model)
                    return UnexpectedSerializationError(model.error());
                model->name = meshName;
                if (!model->name.empty() && *primitiveCount > 1) {
                    model->name += " [";
                    model->name += std::to_string(index);
                    model->name += "]";
                }
                result.push_back(std::move(*model));
            }
            return result;
        }

        auto AddNodeInstances(
            const JsonValue& nodes,
            size_t nodeIndex,
            const Matrix4x4& parentTransform,
            Vector<bool>& recursionStack,
            const Vector<Vector<Scene::Instance>>& meshInstances,
            Scene& scene) -> EngineResult<void>
        {
            auto nodeCount = nodes.Size();
            if (!nodeCount || nodeIndex >= *nodeCount)
                return Failure("glTF scene references an invalid node");
            if (recursionStack[nodeIndex])
                return Failure("Cycle detected in glTF node hierarchy");
            recursionStack[nodeIndex] = true;

            const auto node = nodes.At(nodeIndex);
            auto localTransform = ReadNodeTransform(node);
            if (!localTransform)
                return UnexpectedSerializationError(localTransform.error());
            const Matrix4x4 worldTransform = parentTransform * *localTransform;

            if (const auto meshField = Optional(node, "mesh")) {
                auto meshIndex = ReadUnsigned<size_t>(*meshField, "nodes.mesh");
                if (!meshIndex || *meshIndex >= meshInstances.size())
                    return Failure("glTF node references an invalid mesh");
                String nodeName;
                if (const auto nameField = Optional(node, "name")) {
                    auto name = nameField->AsString();
                    if (!name)
                        return Failure("Invalid glTF node name");
                    nodeName = std::move(*name);
                }
                for (auto instance : meshInstances[*meshIndex]) {
                    instance.worldTransform = worldTransform;
                    if (instance.name.empty())
                        instance.name = nodeName;
                    scene.instances.push_back(std::move(instance));
                }
            }

            auto children = ReadNodeChildIndices(node);
            if (!children)
                return UnexpectedSerializationError(children.error());
            for (const auto childIndex : *children) {
                auto result = AddNodeInstances(nodes, childIndex, worldTransform, recursionStack, meshInstances, scene);
                if (!result)
                    return UnexpectedSerializationError(result.error());
            }
            recursionStack[nodeIndex] = false;
            return {};
        }
    }

    auto GltfExporter::ExportModel(const Model& model, const std::filesystem::path& path) -> EngineResult<void>
    {
        return ExportModels(path, Vector<Model>{ model }, {});
    }

    auto GltfExporter::Export(const std::filesystem::path& path, const Scene& scene) -> EngineResult<void>
    {
        Vector<Model> models;
        Vector<Matrix4x4> transforms;
        models.reserve(scene.instances.size());
        transforms.reserve(scene.instances.size());
        for (const auto& instance : scene.instances) {
            if (!instance.meshData)
                return Failure("Scene contains an instance without mesh data");
            Model model{ *instance.meshData, instance.color, instance.name };
            model.color = instance.color;
            model.name = instance.name;
            models.push_back(std::move(model));
            transforms.push_back(instance.worldTransform);
        }
        return ExportModels(path, models, transforms);
    }

    auto GltfExporter::LoadModel(const std::filesystem::path& path, size_t meshIndex, size_t primitiveIndex) -> EngineResult<Model>
    {
        auto document = LoadDocument(path);
        if (!document)
            return UnexpectedSerializationError(document.error());
        auto meshes = Required(document->root, "meshes");
        if (!meshes)
            return UnexpectedSerializationError(meshes.error());
        auto count = meshes->Size();
        if (!count || meshIndex >= *count)
            return Failure("Requested glTF mesh index does not exist");
        auto models = ReadMeshPrimitives(*document, meshIndex);
        if (!models)
            return UnexpectedSerializationError(models.error());
        if (primitiveIndex >= models->size())
            return Failure("Requested glTF primitive index does not exist");
        return std::move((*models)[primitiveIndex]);
    }

    auto GltfExporter::LoadScene(Scene& scene, render::RenderResources& resources, const std::filesystem::path& path)
        -> EngineResult<void>
    {
        auto document = LoadDocument(path);
        if (!document)
            return UnexpectedSerializationError(document.error());

        auto nodesResult = Required(document->root, "nodes");
        auto meshesResult = Required(document->root, "meshes");
        if (!nodesResult || !meshesResult)
            return Failure("glTF scene is missing its nodes or meshes");
        const JsonValue nodes = *nodesResult;
        auto nodeCount = nodes.Size();
        auto meshCount = meshesResult->Size();
        if (!nodes.IsArray() || !meshesResult->IsArray() || !nodeCount || !meshCount)
            return Failure("Invalid glTF nodes or meshes array");

        Scene imported;
        Vector<Vector<Scene::Instance>> meshInstances;
        meshInstances.resize(*meshCount);
        for (size_t meshIndex = 0; meshIndex < *meshCount; ++meshIndex) {
            auto models = ReadMeshPrimitives(*document, meshIndex);
            if (!models)
                return UnexpectedSerializationError(models.error());
            meshInstances[meshIndex].reserve(models->size());
            for (const auto& model : *models) {
                auto instance = imported.MakeInstance(resources, model.meshData);
                instance.color = model.color;
                instance.name = model.name;
                meshInstances[meshIndex].push_back(std::move(instance));
            }
        }

        Vector<bool> hasParent(*nodeCount, false);
        for (size_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
            auto children = ReadNodeChildIndices(nodes.At(nodeIndex));
            if (!children)
                return UnexpectedSerializationError(children.error());
            for (const auto childIndex : *children) {
                if (childIndex >= *nodeCount)
                    return Failure("glTF node references an invalid child node");
                hasParent[childIndex] = true;
            }
        }

        Vector<size_t> roots;
        const auto scenesField = Optional(document->root, "scenes");
        if (scenesField) {
            if (!scenesField->IsArray())
                return Failure("glTF scenes must be an array");
            size_t selectedSceneIndex = 0;
            if (const auto sceneIndexField = Optional(document->root, "scene")) {
                auto sceneIndex = ReadUnsigned<size_t>(*sceneIndexField, "scene");
                if (!sceneIndex)
                    return UnexpectedSerializationError(sceneIndex.error());
                selectedSceneIndex = *sceneIndex;
            }
            const auto& scenes = *scenesField;
            auto sceneCount = scenes.Size();
            if (!sceneCount || selectedSceneIndex >= *sceneCount)
                return Failure("glTF default scene index is out of range");
            const auto selectedScene = scenes.At(selectedSceneIndex);
            if (!selectedScene.IsObject())
                return Failure("glTF scene entry must be an object");
            auto sceneNodes = Required(selectedScene, "nodes");
            if (!sceneNodes || !sceneNodes->IsArray())
                return Failure("glTF scene nodes must be an array");
            auto rootCount = sceneNodes->Size();
            if (!rootCount)
                return UnexpectedSerializationError(rootCount.error());
            roots.reserve(*rootCount);
            for (size_t index = 0; index < *rootCount; ++index) {
                auto root = ReadUnsigned<size_t>(sceneNodes->At(index), "scenes.nodes");
                if (!root || *root >= *nodeCount)
                    return Failure("glTF scene references an invalid root node");
                roots.push_back(*root);
            }
        } else {
            if (Optional(document->root, "scene"))
                return Failure("glTF defines a default scene but has no scenes array");
            for (size_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
                if (!hasParent[nodeIndex])
                    roots.push_back(nodeIndex);
            }
        }

        Vector<bool> recursionStack(*nodeCount, false);
        const Matrix4x4 identity = Matrix4x4::Identity();
        for (const auto rootIndex : roots) {
            auto result = AddNodeInstances(nodes, rootIndex, identity, recursionStack, meshInstances, imported);
            if (!result)
                return UnexpectedSerializationError(result.error());
        }

        scene = std::move(imported);
        return {};
    }
}
