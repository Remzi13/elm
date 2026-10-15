#include "script/bindings/Bindings.hpp"

namespace elm::script::bindings {

    namespace {

        /// What elm.time reaches: the timing its owner updates every frame.
        struct TimeContext {
            const ScriptTime* time { nullptr };
        };

        const ScriptTime& Time(lua_State* L) { return RequireSystem(L, ContextOf<TimeContext>(L).time, "elm.time"); }

        float Delta(lua_State* L) { return Time(L).deltaTime; }
        float FixedDelta(lua_State* L) { return Time(L).fixedDeltaTime; }
        double Now(lua_State* L) { return Time(L).time; }
        uint64_t Frame(lua_State* L) { return Time(L).frame; }

    } // namespace

    void RegisterTime(lua::State& state, const ScriptTime& time)
    {
        state.SetContext(TimeContext { &time });
        lua::Module(state, "elm.time")
            .Function<&Delta>("delta")
            .Function<&FixedDelta>("fixed_delta")
            .Function<&Now>("now")
            .Function<&Frame>("frame");
    }

} // namespace elm::script::bindings
