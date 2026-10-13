#pragma once

#include "script/lua/Module.hpp"

#include <concepts>

namespace elm::script::lua {

    namespace detail {

        template <typename M>
        struct MemberTraits;
        template <typename C, typename M>
        struct MemberTraits<M C::*> {
            using Class = C;
            using Type = M;
        };

        /// __index: upvalue 1 holds the field getters, upvalue 2 the class table with the methods.
        inline int IndexDispatch(lua_State* L)
        {
            lua_pushvalue(L, 2);
            if (lua_rawget(L, lua_upvalueindex(1)) == LUA_TFUNCTION) {
                const lua_CFunction getter = lua_tocfunction(L, -1);
                lua_settop(L, 2);
                return getter(L);
            }
            lua_pop(L, 1);
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(2));
            return 1;
        }

        /// __newindex: upvalue 1 holds the field setters, upvalue 2 the class name.
        inline int NewIndexDispatch(lua_State* L)
        {
            lua_pushvalue(L, 2);
            if (lua_rawget(L, lua_upvalueindex(1)) == LUA_TFUNCTION) {
                const lua_CFunction setter = lua_tocfunction(L, -1);
                lua_settop(L, 3);
                return setter(L);
            }
            return luaL_error(L, "%s has no writable field '%s'", lua_tostring(L, lua_upvalueindex(2)),
                luaL_tolstring(L, 2, nullptr));
        }

        // Getters see (self, key), setters (self, key, value)

        template <typename T, auto Member>
        int FieldGet(lua_State* L)
        {
            return Guarded(L, [L] {
                Push(L, StackTraits<T>::Check(L, 1).*Member);
                return 1;
            });
        }

        template <typename T, auto Member>
        int FieldSet(lua_State* L)
        {
            using Field = typename MemberTraits<decltype(Member)>::Type;
            return Guarded(L, [L] {
                StackTraits<T>::Check(L, 1).*Member = ReadArg<Field>(L, 3);
                return 0;
            });
        }

        template <typename T, auto Getter>
        int PropertyGet(lua_State* L)
        {
            return Guarded(L, [L] {
                auto& self = StackTraits<T>::Check(L, 1);
                if constexpr (TakesStateFirst<Getter>())
                    return PushResults(L, std::invoke(Getter, L, self));
                else
                    return PushResults(L, std::invoke(Getter, self));
            });
        }

        template <typename T, auto Setter>
        int PropertySet(lua_State* L)
        {
            using Args = typename TraitsOf<Setter>::Args;
            using Value = std::tuple_element_t<std::tuple_size_v<Args> - 1, Args>;
            return Guarded(L, [L] {
                auto& self = StackTraits<T>::Check(L, 1);
                if constexpr (TakesStateFirst<Setter>())
                    std::invoke(Setter, L, self, ReadArg<Value>(L, 3));
                else
                    std::invoke(Setter, self, ReadArg<Value>(L, 3));
                return 0;
            });
        }

        template <typename T>
        int Destroy(lua_State* L)
        {
            std::destroy_at(static_cast<T*>(lua_touserdata(L, 1)));
            return 0;
        }

        template <typename T>
        int Equal(lua_State* L)
        {
            const T* left = StackTraits<T>::Test(L, 1);
            const T* right = StackTraits<T>::Test(L, 2);
            lua_pushboolean(L, left && right && *left == *right);
            return 1;
        }

    } // namespace detail

    /// Registers C++ class T as a Lua value type: objects are copies of T held in userdata.
    /// The class table (module.Name) holds the methods and static functions, obj.field goes
    /// through getters and setters, an unknown field cannot be written.
    ///
    ///     lua::Class<Vector3>(module, "Vector3")
    ///         .Constructor<float, float, float>({ "x", "y", "z" })
    ///         .Field<&Vector3::x>("x")
    ///         .Method<&Vector3::Dot>("dot", { "other" })
    ///         .Meta<&Add>("__add");
    ///
    /// Note: a field of class type is returned as a copy, `a.b.x = 1` changes the copy only.
    template <typename T>
    class Class {
    public:
        Class(Module& module, const char* name)
            : m_state(module.GetState())
            , m_api(m_state.Api().AddClass(name, module.Path()))
        {
            lua_State* L = m_state;
            m_base = lua_gettop(L);

            const bool created = luaL_newmetatable(L, name) != 0;
            ELM_ASSERT(created && "a class name is registered twice");
            UserType<T>::name = name;
            lua_newtable(L); // class table
            lua_newtable(L); // getters
            lua_newtable(L); // setters

            lua_pushvalue(L, Getters());
            lua_pushvalue(L, Methods());
            lua_pushcclosure(L, &detail::IndexDispatch, 2);
            lua_setfield(L, Metatable(), "__index");

            lua_pushvalue(L, Setters());
            lua_pushstring(L, name);
            lua_pushcclosure(L, &detail::NewIndexDispatch, 2);
            lua_setfield(L, Metatable(), "__newindex");

            if constexpr (!std::is_trivially_destructible_v<T>) {
                lua_pushcfunction(L, &detail::Destroy<T>);
                lua_setfield(L, Metatable(), "__gc");
            }
            if constexpr (std::equality_comparable<T>) {
                lua_pushcfunction(L, &detail::Equal<T>);
                lua_setfield(L, Metatable(), "__eq");
            }

            lua_pushvalue(L, Methods());
            lua_setfield(L, module.Index(), name);
        }

        ~Class()
        {
            ELM_ASSERT(lua_gettop(m_state) == m_base + 4);
            lua_settop(m_state, m_base);
        }

        Class(const Class&) = delete;
        Class& operator=(const Class&) = delete;

        /// Name(args...) and Name.new(args...).
        template <typename... A>
        Class& Constructor(ParamNames names = { })
        {
            lua_State* L = m_state;
            lua_pushcfunction(L, (&detail::Construct<T, 1, A...>));
            lua_setfield(L, Methods(), "new");

            // Calling the class table: the table itself is the first argument
            lua_newtable(L);
            lua_pushcfunction(L, (&detail::Construct<T, 2, A...>));
            lua_setfield(L, -2, "__call");
            lua_setmetatable(L, Methods());

            ApiFunction function;
            function.name = "new";
            function.kind = ApiFunction::Kind::Constructor;
            detail::AddParams<std::tuple<A...>>(function.params, names, 0);
            function.results.push_back(&TypeNameOf<T>);
            Api().functions.push_back(std::move(function));
            return *this;
        }

        /// Name(args...) and Name.new(args...) through a function returning T, for optional or
        /// computed arguments: Fn(std::optional<float> x, ...) -> T.
        template <auto Fn>
        Class& Factory(ParamNames names = { })
        {
            static_assert(std::is_same_v<std::remove_cvref_t<typename detail::TraitsOf<Fn>::Result>, T>,
                "a factory returns the class type");
            lua_State* L = m_state;
            lua_pushcfunction(L, (&detail::Thunk<Fn, 1>));
            lua_setfield(L, Methods(), "new");

            lua_newtable(L);
            lua_pushcfunction(L, (&detail::Thunk<Fn, 2>));
            lua_setfield(L, -2, "__call");
            lua_setmetatable(L, Methods());

            auto function = detail::Describe<Fn>("new", ApiFunction::Kind::Constructor, names, false);
            Api().functions.push_back(std::move(function));
            return *this;
        }

        /// A data member: read-write, read-only when const.
        template <auto Member>
            requires std::is_member_object_pointer_v<decltype(Member)>
        Class& Field(const char* name)
        {
            using Traits = detail::MemberTraits<decltype(Member)>;
            static_assert(std::is_base_of_v<typename Traits::Class, T>, "the field belongs to another class");

            lua_State* L = m_state;
            lua_pushcfunction(L, (&detail::FieldGet<T, Member>));
            lua_setfield(L, Getters(), name);
            constexpr bool readOnly = std::is_const_v<typename Traits::Type>;
            if constexpr (!readOnly) {
                lua_pushcfunction(L, (&detail::FieldSet<T, Member>));
                lua_setfield(L, Setters(), name);
            }
            AddField<typename Traits::Type>(name, readOnly);
            return *this;
        }

        /// A field computed by functions: Getter(const T&) -> V, Setter(T&, V); member functions work too.
        /// Free accessors may take lua_State* first. Without a Setter the field is read-only.
        template <auto Getter, auto Setter = nullptr>
        Class& Property(const char* name)
        {
            lua_State* L = m_state;
            lua_pushcfunction(L, (&detail::PropertyGet<T, Getter>));
            lua_setfield(L, Getters(), name);
            constexpr bool readOnly = std::is_same_v<decltype(Setter), std::nullptr_t>;
            if constexpr (!readOnly) {
                lua_pushcfunction(L, (&detail::PropertySet<T, Setter>));
                lua_setfield(L, Setters(), name);
            }
            AddField<typename detail::TraitsOf<Getter>::Result>(name, readOnly);
            return *this;
        }

        /// obj:name(...): a member function, or a free function taking T& (or const T&) first
        /// (after an optional lua_State*).
        template <auto Fn>
        Class& Method(const char* name, ParamNames names = { })
        {
            AddFunction<Fn>(Methods(), name);
            Api().functions.push_back(detail::Describe<Fn>(name, ApiFunction::Kind::Method, names, true));
            return *this;
        }

        /// Name.name(...): a function without an object.
        template <auto Fn>
        Class& StaticFunction(const char* name, ParamNames names = { })
        {
            AddFunction<Fn>(Methods(), name);
            Api().functions.push_back(detail::Describe<Fn>(name, ApiFunction::Kind::Function, names, false));
            return *this;
        }

        /// A metamethod: "__add", "__tostring", "__lt", ... (__eq is set for comparable types).
        template <auto Fn>
        Class& Meta(const char* metamethod)
        {
            AddFunction<Fn>(Metatable(), metamethod);
            Api().metamethods.push_back(metamethod);
            return *this;
        }

    private:
        [[nodiscard]] int Metatable() const noexcept { return m_base + 1; }
        [[nodiscard]] int Methods() const noexcept { return m_base + 2; }
        [[nodiscard]] int Getters() const noexcept { return m_base + 3; }
        [[nodiscard]] int Setters() const noexcept { return m_base + 4; }
        [[nodiscard]] ApiClass& Api() { return m_state.Api().ClassAt(m_api); }

        template <auto Fn>
        void AddFunction(int table, const char* name)
        {
            lua_pushcfunction(m_state, detail::CFunctionOf<Fn>());
            lua_setfield(m_state, table, name);
        }

        template <typename V>
        void AddField(const char* name, bool readOnly)
        {
            ApiField field;
            field.name = name;
            field.type = &TypeNameOf<V>;
            field.readOnly = readOnly;
            Api().fields.push_back(std::move(field));
        }

        State& m_state;
        size_t m_api;
        int m_base { 0 };
    };

} // namespace elm::script::lua
