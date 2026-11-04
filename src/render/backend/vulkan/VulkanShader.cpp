#include "render/backend/vulkan/VulkanShader.hpp"

#include "core/Log.hpp"

#include "render/ShaderSource.hpp"

#include <cstring>

namespace elm::render::vulkan {

    namespace {

        constexpr const char* Source = "VulkanBackend";
        constexpr StringView HlslExtension = ".hlsl";

        constexpr uint32_t SpirvMagic = 0x07230203;
        constexpr size_t SpirvHeaderWords = 5;
        constexpr uint32_t OpName = 5;
        constexpr uint32_t OpDecorate = 71;
        constexpr uint32_t DecorationBinding = 33;

        struct Instruction {
            uint32_t opcode { 0 };
            std::span<const uint32_t> operands;
        };

        /// Calls visit(instruction, offset of its first word) for every instruction; false on malformed SPIR-V.
        template <typename Visitor>
        bool ForEachInstruction(std::span<const uint32_t> words, Visitor&& visit)
        {
            for (size_t offset = SpirvHeaderWords; offset < words.size();) {
                const uint32_t wordCount = words[offset] >> 16;
                if (wordCount == 0 || offset + wordCount > words.size())
                    return false;
                visit(Instruction { words[offset] & 0xFFFFu, words.subspan(offset + 1, wordCount - 1) }, offset);
                offset += wordCount;
            }
            return true;
        }

        /// Literal string operand: UTF-8 bytes packed into words, NUL-terminated.
        String ReadLiteral(std::span<const uint32_t> words)
        {
            String text;
            for (const uint32_t word : words) {
                for (uint32_t byte = 0; byte < 4; ++byte) {
                    const char character = static_cast<char>((word >> (byte * 8)) & 0xFFu);
                    if (character == '\0')
                        return text;
                    text.push_back(character);
                }
            }
            return text;
        }

    } // namespace

    String SpirvFileName(const ShaderDesc& desc)
    {
        StringView file = desc.sourceFile;
        if (file.ends_with(HlslExtension))
            file.remove_suffix(HlslExtension.size());
        String name = "spirv/" + String(file);
        for (const auto& macro : desc.macros)
            name += "." + macro.name + "_" + macro.value;
        return name + ".spv";
    }

    std::optional<SpirvShader> LoadSpirvShader(const ShaderDesc& desc)
    {
        const String fileName = SpirvFileName(desc);
        const String bytes = loadShaderSource(fileName);
        if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
            ERROR_MESSAGE(log::Category::Render, Source, "Shader '%s' is missing, build the project to compile it", fileName.c_str());
            return std::nullopt;
        }

        SpirvShader shader { desc.name, desc.stage, Vector<uint32_t>(bytes.size() / sizeof(uint32_t)) };
        std::memcpy(shader.words.data(), bytes.data(), bytes.size());
        if (shader.words.size() < SpirvHeaderWords || shader.words[0] != SpirvMagic) {
            ERROR_MESSAGE(log::Category::Render, Source, "Shader '%s' is not SPIR-V", fileName.c_str());
            return std::nullopt;
        }
        return shader;
    }

    Vector<uint32_t> RemapBindings(std::span<const uint32_t> words, const SpirvBindings& bindings)
    {
        UnorderedMap<uint32_t, uint32_t> bindingOfId;
        ForEachInstruction(words, [&](const Instruction& instruction, size_t) {
            if (instruction.opcode != OpName || instruction.operands.size() < 2)
                return;
            const auto it = bindings.find(ReadLiteral(instruction.operands.subspan(1)));
            if (it != bindings.end())
                bindingOfId[instruction.operands[0]] = it->second;
        });

        Vector<uint32_t> patched(words.begin(), words.end());
        ForEachInstruction(words, [&](const Instruction& instruction, size_t offset) {
            const auto& operands = instruction.operands;
            if (instruction.opcode != OpDecorate || operands.size() < 3 || operands[1] != DecorationBinding)
                return;
            // OpDecorate <target> Binding <number>: the number is the fourth word of the instruction
            if (const auto it = bindingOfId.find(operands[0]); it != bindingOfId.end())
                patched[offset + 3] = it->second;
        });
        return patched;
    }

} // namespace elm::render::vulkan
