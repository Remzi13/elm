#include "script/bindings/Bindings.hpp"

#include "graphics/debug/DebugDraw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace elm::script::bindings {

    namespace {

        using graphics::DebugChannel;
        using graphics::DebugDraw;
        using graphics::DebugPen;
        using math::Vector3;
        using math::Vector4;

        /// Script channels are Script/<name>: one group in the Debug menu.
        constexpr StringView ScriptChannelPrefix = "Script/";
        constexpr StringView DefaultChannelName = "Default";
        constexpr float DefaultPointSize = 0.2f;
        constexpr const char* StyleFields[] = { "color", "duration", "on_top", "channel" };

        /// A channel as Lua sees it (elm.debug.channel).
        struct ScriptDebugChannel {
            DebugChannel channel;
            String name;
        };

        /// What elm.debug reaches.
        struct DebugContext {
            DebugDraw* debugDraw { nullptr };
            /// Every channel the scripts drew in, the default one first; Stop clears them.
            Vector<DebugChannel> channels;
        };

        /// The pen and channel a style table describes.
        struct ScriptPen {
            DebugPen pen;
            DebugChannel channel;
        };

        DebugContext& Context(lua_State* L) { return ContextOf<DebugContext>(L); }
        DebugDraw& DebugDrawOf(lua_State* L) { return RequireSystem(L, Context(L).debugDraw, "elm.debug"); }

        DebugChannel RegisterScriptChannel(DebugContext& context, StringView name, bool enabledByDefault)
        {
            const String fullName = String(ScriptChannelPrefix) + String(name);
            const DebugChannel channel = context.debugDraw->RegisterChannel(fullName, name, enabledByDefault);
            if (std::ranges::find(context.channels, channel) == context.channels.end())
                context.channels.push_back(channel);
            return channel;
        }

        void CheckStyleFields(lua_State* L, int index)
        {
            // An unknown field is a typo, not something to skip
            lua_pushnil(L);
            while (lua_next(L, index)) {
                const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
                const bool known = key && std::ranges::any_of(StyleFields, [key](const char* field) { return std::strcmp(key, field) == 0; });
                if (!known)
                    luaL_error(L, "elm.debug style has no field '%s' (color, duration, on_top, channel)", luaL_tolstring(L, -2, nullptr));
                lua_pop(L, 1);
            }
        }

        /// Reads one field of the style table at index into pen; nil leaves the default.
        void ReadStyleField(lua_State* L, int index, const char* field, ScriptPen& pen)
        {
            if (lua_getfield(L, index, field) == LUA_TNIL) {
                lua_pop(L, 1);
                return;
            }
            if (std::strcmp(field, "color") == 0) {
                const auto color = lua::StackTraits<Vector4>::Get(L, -1);
                if (!color)
                    luaL_error(L, "elm.debug style: color must be a Vector4");
                pen.pen.color = *color;
            } else if (std::strcmp(field, "duration") == 0) {
                const double duration = lua_tonumber(L, -1);
                if (!lua_isnumber(L, -1) || !std::isfinite(duration) || duration < 0.0)
                    luaL_error(L, "elm.debug style: duration must be a number of seconds >= 0");
                pen.pen.duration = static_cast<float>(duration);
            } else if (std::strcmp(field, "on_top") == 0) {
                pen.pen.depth = lua_toboolean(L, -1) ? render::DebugDepth::OnTop : render::DebugDepth::Tested;
            } else {
                const auto channel = lua::StackTraits<ScriptDebugChannel>::Get(L, -1);
                if (!channel)
                    luaL_error(L, "elm.debug style: channel must come from elm.debug.channel");
                pen.channel = channel->channel;
            }
            lua_pop(L, 1);
        }

        /// style: nil or { color = Vector4, duration = seconds, on_top = boolean, channel = DebugChannel }.
        ScriptPen ReadPen(lua_State* L, const std::optional<lua::Ref>& style)
        {
            DebugContext& context = Context(L);
            RequireSystem(L, context.debugDraw, "elm.debug");
            ScriptPen pen { {}, context.channels.front() };
            if (!style)
                return pen;
            style->Push(L);
            const int index = lua_gettop(L);
            if (!lua_istable(L, index))
                luaL_error(L, "elm.debug style is a table { color, duration, on_top, channel }, got %s", luaL_typename(L, index));
            CheckStyleFields(L, index);
            for (const char* field : StyleFields)
                ReadStyleField(L, index, field, pen);
            lua_pop(L, 1);
            return pen;
        }

        /// text(position, text, style?): centered on position, over the scene (on_top does not matter).
        void Text(lua_State* L, const Vector3& position, StringView text, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Text(pen.channel, position, text, pen.pen);
        }

        void Line(lua_State* L, const Vector3& from, const Vector3& to, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Line(pen.channel, from, to, pen.pen);
        }

        void Arrow(lua_State* L, const Vector3& from, const Vector3& to, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Arrow(pen.channel, from, to, pen.pen);
        }

        /// ray(origin, direction, length, style?): an arrow of the given length along direction.
        void Ray(lua_State* L, const Vector3& origin, const Vector3& direction, float length, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Arrow(pen.channel, origin, origin + direction.Normalized() * length, pen.pen);
        }

        void Point(lua_State* L, const Vector3& position, std::optional<float> size, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Point(pen.channel, position, size.value_or(DefaultPointSize), pen.pen);
        }

        void Box(lua_State* L, const Vector3& center, const Vector3& halfExtents, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Box(pen.channel, math::Matrix4x4::Translation(center), halfExtents, pen.pen);
        }

        void Sphere(lua_State* L, const Vector3& center, float radius, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Sphere(pen.channel, math::Matrix4x4::Translation(center), radius, pen.pen);
        }

        /// capsule(center, radius, half_height, style?): along the world Y axis.
        void Capsule(lua_State* L, const Vector3& center, float radius, float halfHeight, const std::optional<lua::Ref>& style)
        {
            const ScriptPen pen = ReadPen(L, style);
            DebugDrawOf(L).Capsule(pen.channel, math::Matrix4x4::Translation(center), radius, halfHeight, pen.pen);
        }

        /// channel(name, enabled_by_default?): the channel Script/<name>, made the first time.
        ScriptDebugChannel Channel(lua_State* L, StringView name, std::optional<bool> enabledByDefault)
        {
            DebugContext& context = Context(L);
            RequireSystem(L, context.debugDraw, "elm.debug");
            if (name.empty() || name.find('/') != StringView::npos)
                luaL_error(L, "elm.debug.channel: the name must be non-empty and have no '/'");
            return { RegisterScriptChannel(context, name, enabledByDefault.value_or(true)), String(name) };
        }

    } // namespace

    void RegisterDebug(lua::State& state, graphics::DebugDraw* debugDraw)
    {
        DebugContext context { debugDraw };
        if (debugDraw)
            RegisterScriptChannel(context, DefaultChannelName, true);
        state.SetContext(std::move(context));

        lua::Module module(state, "elm.debug");
        module
            .Function<&Text>("text", { "position", "text", "style" })
            .Function<&Line>("line", { "from", "to", "style" })
            .Function<&Arrow>("arrow", { "from", "to", "style" })
            .Function<&Ray>("ray", { "origin", "direction", "length", "style" })
            .Function<&Point>("point", { "position", "size", "style" })
            .Function<&Box>("box", { "center", "half_extents", "style" })
            .Function<&Sphere>("sphere", { "center", "radius", "style" })
            .Function<&Capsule>("capsule", { "center", "radius", "half_height", "style" })
            .Function<&Channel>("channel", { "name", "enabled_by_default" });

        lua::Class<ScriptDebugChannel>(module, "DebugChannel")
            .Property<[](const ScriptDebugChannel& channel) { return channel.name; }>("name")
            .Property<[](lua_State* L, const ScriptDebugChannel& channel) { return DebugDrawOf(L).IsEnabled(channel.channel); },
                [](lua_State* L, ScriptDebugChannel& channel, bool enabled) { DebugDrawOf(L).SetEnabled(channel.channel, enabled); }>("enabled");
    }

    void ClearScriptDebugDraw(lua::State& state)
    {
        DebugContext* context = lua::State::Context<DebugContext>(state);
        if (!context || !context->debugDraw)
            return;
        for (const DebugChannel channel : context->channels)
            context->debugDraw->ClearChannel(channel);
    }

} // namespace elm::script::bindings
