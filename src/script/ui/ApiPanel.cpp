#include "script/ui/ApiPanel.hpp"

#include "script/lua/State.hpp"

#include "imgui.h"

#include <algorithm>

namespace elm::script {

    namespace {

        String TypeOf(lua::TypeNameFn type) { return type ? type() : String("any"); }

        /// "(a: number, b: Vector3) -> Entity"
        String Signature(const lua::ApiFunction& function)
        {
            String text = "(";
            for (size_t index = 0; index < function.params.size(); ++index) {
                if (index)
                    text += ", ";
                text += function.params[index].name + ": " + TypeOf(function.params[index].type);
            }
            if (function.variadic)
                text += function.params.empty() ? "..." : ", ...";
            text += ")";
            for (size_t index = 0; index < function.results.size(); ++index)
                text += (index ? ", " : " -> ") + TypeOf(function.results[index]);
            return text;
        }

        /// What a click inserts into the console: the call with its parameter names.
        String CallText(StringView owner, const char* separator, const lua::ApiFunction& function)
        {
            String text = String(owner) + separator + (function.kind == lua::ApiFunction::Kind::Constructor ? String() : function.name) + "(";
            for (size_t index = 0; index < function.params.size(); ++index)
                text += (index ? ", " : "") + function.params[index].name;
            return text + ")";
        }

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

    } // namespace

    ApiPanel::ApiPanel(const lua::State& state, InsertText insert)
        : m_state(state)
        , m_insert(std::move(insert))
    {
    }

    void ApiPanel::Draw()
    {
        if (!m_state.IsOpen())
            return;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter, e.g. spawn or Vector3", m_filter.data(), m_filter.size());
        const StringView filter(m_filter.data());

        if (!ImGui::BeginChild("ApiList", ImVec2(0.0f, 0.0f), false))
            return ImGui::EndChild();

        const auto& api = m_state.Api();
        auto entry = [&](const String& label, const String& detail, const String& insert) {
            if (!ContainsNoCase(label, filter) && !ContainsNoCase(detail, filter))
                return;
            if (ImGui::Selectable(label.c_str()))
                m_insert(insert);
            if (!detail.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", detail.c_str());
            }
        };

        for (const auto& module : api.Modules()) {
            if (!ImGui::CollapsingHeader(module.path.c_str(), filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            ImGui::PushID(module.path.c_str());
            for (const auto& function : module.functions)
                entry(module.path + "." + function.name, Signature(function), CallText(module.path, ".", function));
            for (const auto& value : module.values)
                entry(module.path + "." + value.name, ": " + TypeOf(value.type), module.path + "." + value.name);
            ImGui::PopID();
        }

        for (const auto& added : api.Classes()) {
            const String header = added.name + "  (" + added.module + "." + added.name + ")";
            if (!ImGui::CollapsingHeader(header.c_str(), filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            ImGui::PushID(header.c_str());
            const String qualified = added.module + "." + added.name;
            for (const auto& function : added.functions) {
                const bool method = function.kind == lua::ApiFunction::Kind::Method;
                const bool constructor = function.kind == lua::ApiFunction::Kind::Constructor;
                const String label = constructor ? added.name + "(...)" : (method ? ":" : ".") + function.name;
                entry(label, Signature(function), constructor ? CallText(qualified, "", function)
                    : method ? CallText("", ":", function) : CallText(qualified, ".", function));
            }
            for (const auto& field : added.fields)
                entry("." + field.name, ": " + TypeOf(field.type) + (field.readOnly ? " (read only)" : ""), "." + field.name);
            if (!added.metamethods.empty()) {
                String operators;
                for (const auto& name : added.metamethods)
                    operators += (operators.empty() ? "" : " ") + name;
                ImGui::TextDisabled("metamethods: %s", operators.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

} // namespace elm::script
