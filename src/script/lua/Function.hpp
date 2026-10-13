#pragma once

#include "core/Error.hpp"

#include "script/lua/ApiRegistry.hpp"
#include "script/lua/Stack.hpp"

#include <exception>
#include <functional>
#include <tuple>

namespace elm::script::lua::detail {

    // ── Signatures ──────────────────────────────────────────────────────────────

    template <typename R, typename... A>
    struct Signature {
        using Result = R;
        using Args = std::tuple<A...>;
        static constexpr size_t Arity = sizeof...(A);
        static constexpr bool IsMember = false;
        using Class = void;
    };

    template <typename C, typename R, typename... A>
    struct MemberSignature : Signature<R, A...> {
        static constexpr bool IsMember = true;
        using Class = C;
    };

    /// Lambdas and other function objects: the call operator, without the object as an argument.
    template <typename F>
    struct FunctionTraits : FunctionTraits<decltype(&F::operator())> {
        static constexpr bool IsMember = false;
        using Class = void;
    };

    template <typename R, typename... A>
    struct FunctionTraits<R (*)(A...)> : Signature<R, A...> { };
    template <typename R, typename... A>
    struct FunctionTraits<R (*)(A...) noexcept> : Signature<R, A...> { };
    template <typename C, typename R, typename... A>
    struct FunctionTraits<R (C::*)(A...)> : MemberSignature<C, R, A...> { };
    template <typename C, typename R, typename... A>
    struct FunctionTraits<R (C::*)(A...) noexcept> : MemberSignature<C, R, A...> { };
    template <typename C, typename R, typename... A>
    struct FunctionTraits<R (C::*)(A...) const> : MemberSignature<const C, R, A...> { };
    template <typename C, typename R, typename... A>
    struct FunctionTraits<R (C::*)(A...) const noexcept> : MemberSignature<const C, R, A...> { };

    template <auto Fn>
    using TraitsOf = FunctionTraits<std::remove_cvref_t<decltype(Fn)>>;

    /// int(lua_State*) is a plain Lua C function: it reads its arguments and pushes its results itself.
    template <auto Fn>
    inline constexpr bool IsRawCFunction = std::is_convertible_v<decltype(Fn), lua_CFunction>
        && std::is_pointer_v<std::remove_cvref_t<decltype(Fn)>>;

    // ── Arguments ───────────────────────────────────────────────────────────────

    /// A lua_State* parameter receives the calling state and takes no stack slot.
    template <typename Arg>
    inline constexpr bool IsStateArg = std::is_same_v<std::remove_cvref_t<Arg>, lua_State*>;

    /// A free function whose first parameter is lua_State* (accessors that need the engine context).
    template <auto Fn>
    consteval bool TakesStateFirst()
    {
        using Traits = TraitsOf<Fn>;
        if constexpr (Traits::IsMember || Traits::Arity == 0)
            return false;
        else
            return IsStateArg<std::tuple_element_t<0, typename Traits::Args>>;
    }

    /// Stack offset of argument I: the arguments before it that take a slot.
    template <typename Args, size_t I>
    consteval int SlotOf()
    {
        return []<size_t... J>(std::index_sequence<J...>) {
            return (0 + ... + ((J < I && !IsStateArg<std::tuple_element_t<J, Args>>) ? 1 : 0));
        }(std::make_index_sequence<std::tuple_size_v<Args>>{});
    }

    template <typename Arg>
    decltype(auto) ReadArg(lua_State* L, int index)
    {
        if constexpr (IsStateArg<Arg>)
            return L;
        else
            return StackTraits<std::remove_cvref_t<Arg>>::Check(L, index);
    }

    // ── Results ─────────────────────────────────────────────────────────────────

    template <typename T>
    inline constexpr bool IsTuple = false;
    template <typename... T>
    inline constexpr bool IsTuple<std::tuple<T...>> = true;

    template <typename T>
    inline constexpr bool IsExpected = false;
    template <typename T, typename E>
    inline constexpr bool IsExpected<std::expected<T, E>> = true;

    /// Pushes a result and returns how many values it took: a tuple is several results,
    /// a failed EngineResult raises a Lua error with its message.
    template <typename R>
    int PushResults(lua_State* L, R&& result)
    {
        using V = std::remove_cvref_t<R>;
        if constexpr (IsTuple<V>) {
            return std::apply([L](auto&&... items) {
                (Push(L, std::forward<decltype(items)>(items)), ...);
                return static_cast<int>(sizeof...(items));
            }, std::forward<R>(result));
        } else if constexpr (IsExpected<V>) {
            if (!result)
                return luaL_error(L, "%s", result.error().message.c_str());
            if constexpr (std::is_void_v<typename V::value_type>)
                return 0;
            else
                return PushResults(L, *std::forward<R>(result));
        } else {
            Push(L, std::forward<R>(result));
            return 1;
        }
    }

    // ── Calls ───────────────────────────────────────────────────────────────────

    /// Runs a binding body; C++ exceptions become Lua errors. Lua's own errors (thrown by
    /// luaL_error and friends, Lua is compiled as C++) are not std::exception and pass through.
    template <typename Body>
    int Guarded(lua_State* L, Body&& body)
    {
        try {
            return body();
        } catch (const std::exception& error) {
            return luaL_error(L, "%s", error.what());
        }
    }

    template <auto Fn, int FirstSlot>
    int Invoke(lua_State* L)
    {
        using Traits = TraitsOf<Fn>;
        using Args = typename Traits::Args;
        using R = typename Traits::Result;

        return [L]<size_t... I>(std::index_sequence<I...>) -> int {
            if constexpr (Traits::IsMember) {
                auto& self = StackTraits<std::remove_const_t<typename Traits::Class>>::Check(L, FirstSlot);
                if constexpr (std::is_void_v<R>) {
                    std::invoke(Fn, self, ReadArg<std::tuple_element_t<I, Args>>(L, FirstSlot + 1 + SlotOf<Args, I>())...);
                    return 0;
                } else {
                    return PushResults(L,
                        std::invoke(Fn, self, ReadArg<std::tuple_element_t<I, Args>>(L, FirstSlot + 1 + SlotOf<Args, I>())...));
                }
            } else {
                if constexpr (std::is_void_v<R>) {
                    std::invoke(Fn, ReadArg<std::tuple_element_t<I, Args>>(L, FirstSlot + SlotOf<Args, I>())...);
                    return 0;
                } else {
                    return PushResults(L,
                        std::invoke(Fn, ReadArg<std::tuple_element_t<I, Args>>(L, FirstSlot + SlotOf<Args, I>())...));
                }
            }
        }(std::make_index_sequence<Traits::Arity>{});
    }

    /// Lua C function calling Fn with arguments read from the stack starting at FirstSlot.
    template <auto Fn, int FirstSlot = 1>
    int Thunk(lua_State* L)
    {
        return Guarded(L, [L] { return Invoke<Fn, FirstSlot>(L); });
    }

    template <auto Fn>
    [[nodiscard]] constexpr lua_CFunction CFunctionOf() noexcept
    {
        if constexpr (IsRawCFunction<Fn>)
            return Fn;
        else
            return &Thunk<Fn>;
    }

    /// Constructs T from the arguments: T(args...) when such a constructor exists, T{ args... } otherwise.
    template <typename T, int FirstSlot, typename... A>
    int Construct(lua_State* L)
    {
        return Guarded(L, [L] {
            [L]<size_t... I>(std::index_sequence<I...>) {
                if constexpr (std::is_constructible_v<T, A...>)
                    StackTraits<T>::Push(L, T(ReadArg<A>(L, FirstSlot + static_cast<int>(I))...));
                else
                    StackTraits<T>::Push(L, T { ReadArg<A>(L, FirstSlot + static_cast<int>(I))... });
            }(std::index_sequence_for<A...>{});
            return 1;
        });
    }

    // ── Description for the API registry ────────────────────────────────────────

    template <typename R>
    void AddResultTypes(Vector<TypeNameFn>& results)
    {
        using V = std::remove_cvref_t<R>;
        if constexpr (std::is_void_v<V>) {
        } else if constexpr (IsTuple<V>) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                (results.push_back(&TypeNameOf<std::tuple_element_t<I, V>>), ...);
            }(std::make_index_sequence<std::tuple_size_v<V>>{});
        } else if constexpr (IsExpected<V>) {
            AddResultTypes<typename V::value_type>(results);
        } else {
            results.push_back(&TypeNameOf<V>);
        }
    }

    /// skip: leading parameters that take a stack slot but are not listed (the object of a method).
    template <typename Args>
    void AddParams(Vector<ApiParam>& params, ParamNames names, size_t skip)
    {
        [&]<size_t... I>(std::index_sequence<I...>) {
            size_t position = 0;
            size_t skipped = 0;
            auto add = [&]<typename Arg>() {
                if (IsStateArg<Arg>)
                    return;
                if (skipped < skip) {
                    ++skipped;
                    return;
                }
                const size_t nameIndex = position++;
                ApiParam param;
                param.name = nameIndex < names.size() ? String(names.begin()[nameIndex]) : "arg" + String(std::to_string(nameIndex + 1));
                if constexpr (!IsStateArg<Arg>)
                    param.type = &TypeNameOf<Arg>;
                params.push_back(std::move(param));
            };
            (add.template operator()<std::tuple_element_t<I, Args>>(), ...);
        }(std::make_index_sequence<std::tuple_size_v<Args>>{});
    }

    /// selfIsFirstArg: a free function bound as a method takes the object as its first parameter.
    template <auto Fn>
    [[nodiscard]] ApiFunction Describe(const char* name, ApiFunction::Kind kind, ParamNames names, bool selfIsFirstArg)
    {
        ApiFunction function;
        function.name = name;
        function.kind = kind;
        if constexpr (IsRawCFunction<Fn>) {
            // The C function reads its own arguments: names given here are documentation only
            for (const char* paramName : names)
                function.params.push_back({ String(paramName), nullptr });
            function.variadic = names.size() == 0;
        } else {
            using Traits = TraitsOf<Fn>;
            AddParams<typename Traits::Args>(function.params, names, selfIsFirstArg && !Traits::IsMember ? 1 : 0);
            AddResultTypes<typename Traits::Result>(function.results);
        }
        return function;
    }

} // namespace elm::script::lua::detail
