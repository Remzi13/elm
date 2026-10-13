#pragma once

#include "core/Std.hpp"

#include <initializer_list>

namespace elm::script::lua {

    /// Parameter names for the generated LuaLS annotations; C++ cannot reflect them.
    using ParamNames = std::initializer_list<const char*>;

    /// Lua type name of a C++ type. Evaluated when the meta file is written, so a class
    /// may refer to classes registered after it.
    using TypeNameFn = String (*)();

    struct ApiParam {
        String name;
        TypeNameFn type { nullptr };
    };

    struct ApiFunction {
        enum class Kind { Function, Method, Constructor };

        String name;
        Kind kind { Kind::Function };
        Vector<ApiParam> params;
        Vector<TypeNameFn> results;
        /// Takes any arguments (a raw lua_CFunction).
        bool variadic { false };
    };

    struct ApiField {
        String name;
        TypeNameFn type { nullptr };
        bool readOnly { false };
    };

    struct ApiClass {
        String name;
        /// Module table the class is stored in.
        String module;
        Vector<ApiField> fields;
        Vector<ApiFunction> functions;
        Vector<String> metamethods;
    };

    struct ApiModule {
        String path;
        Vector<ApiFunction> functions;
        Vector<ApiField> values;
    };

    /// What the bindings expose to scripts, filled in by Module and Class as they register.
    class ApiRegistry {
    public:
        /// Index of the module with this path, added when missing. Indices stay valid, references do not.
        [[nodiscard]] size_t AddModule(StringView path);
        [[nodiscard]] size_t AddClass(StringView name, StringView module);
        [[nodiscard]] ApiModule& ModuleAt(size_t index) { return m_modules[index]; }
        [[nodiscard]] ApiClass& ClassAt(size_t index) { return m_classes[index]; }

        [[nodiscard]] const Vector<ApiModule>& Modules() const noexcept { return m_modules; }
        [[nodiscard]] const Vector<ApiClass>& Classes() const noexcept { return m_classes; }

        /// Definition file for the Lua language server (---@meta), for completion and type checks in editors.
        [[nodiscard]] String GenerateLuaMeta() const;

    private:
        Vector<ApiModule> m_modules;
        Vector<ApiClass> m_classes;
    };

} // namespace elm::script::lua
