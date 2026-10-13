#include "script/bindings/Bindings.hpp"

#include "math/Rotation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace elm::script::bindings {

    namespace {

        using math::Matrix4x4;
        using math::Quaternion;
        using math::Vector3;
        using math::Vector4;

        String Format(const char* format, auto... values)
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(values)...);
            return buffer;
        }

        // ── Vector3 ─────────────────────────────────────────────────────────────

        Vector3 MakeVector3(std::optional<float> x, std::optional<float> y, std::optional<float> z)
        {
            return { x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f) };
        }

        Vector3 Multiply(const Vector3& a, const Vector3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }

        /// v * s, s * v, v * v (per component)
        int Vector3Mul(lua_State* L)
        {
            using Traits = lua::StackTraits<Vector3>;
            if (const Vector3* left = Traits::Test(L, 1)) {
                if (const Vector3* right = Traits::Test(L, 2))
                    lua::Push(L, Multiply(*left, *right));
                else
                    lua::Push(L, *left * static_cast<float>(luaL_checknumber(L, 2)));
            } else {
                lua::Push(L, Traits::Check(L, 2) * static_cast<float>(luaL_checknumber(L, 1)));
            }
            return 1;
        }

        void RegisterVector3(lua::Module& module)
        {
            lua::Class<Vector3>(module, "Vector3")
                .Factory<&MakeVector3>({ "x", "y", "z" })
                .Field<&Vector3::x>("x")
                .Field<&Vector3::y>("y")
                .Field<&Vector3::z>("z")
                .Property<&Vector3::Length>("length")
                .Property<[](const Vector3& v) { return v.Dot(v); }>("length_squared")
                .Method<&Vector3::Dot>("dot", { "other" })
                .Method<&Vector3::Cross>("cross", { "other" })
                .Method<&Vector3::Normalized>("normalized")
                .Method<[](const Vector3& a, const Vector3& b) { return (b - a).Length(); }>("distance", { "other" })
                .Method<[](const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }>("lerp", { "other", "t" })
                .Method<[](const Vector3& v) { return v; }>("copy")
                .Method<[](Vector3& v, const Vector3& other) { v = other; }>("assign", { "other" })
                .StaticFunction<[] { return Vector3 { }; }>("zero")
                .StaticFunction<[] { return Vector3 { 1.0f, 1.0f, 1.0f }; }>("one")
                .StaticFunction<[] { return Vector3 { 0.0f, 1.0f, 0.0f }; }>("up")
                .StaticFunction<[] { return Vector3 { 1.0f, 0.0f, 0.0f }; }>("right")
                .StaticFunction<[] { return Vector3 { 0.0f, 0.0f, 1.0f }; }>("forward")
                .Meta<[](const Vector3& a, const Vector3& b) { return a + b; }>("__add")
                .Meta<[](const Vector3& a, const Vector3& b) { return a - b; }>("__sub")
                .Meta<[](const Vector3& v) { return v * -1.0f; }>("__unm")
                .Meta<&Vector3Mul>("__mul")
                .Meta<[](const Vector3& v, float s) { return v / s; }>("__div")
                .Meta<[](const Vector3& v) { return Format("Vector3(%g, %g, %g)", v.x, v.y, v.z); }>("__tostring");
        }

        // ── Vector4 ─────────────────────────────────────────────────────────────

        Vector4 MakeVector4(std::optional<float> x, std::optional<float> y, std::optional<float> z, std::optional<float> w)
        {
            return { x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f), w.value_or(0.0f) };
        }

        bool Vector4Equal(const Vector4& a, const Vector4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

        int Vector4Mul(lua_State* L)
        {
            using Traits = lua::StackTraits<Vector4>;
            if (const Vector4* left = Traits::Test(L, 1))
                lua::Push(L, *left * static_cast<float>(luaL_checknumber(L, 2)));
            else
                lua::Push(L, Traits::Check(L, 2) * static_cast<float>(luaL_checknumber(L, 1)));
            return 1;
        }

        void RegisterVector4(lua::Module& module)
        {
            lua::Class<Vector4>(module, "Vector4")
                .Factory<&MakeVector4>({ "x", "y", "z", "w" })
                .Field<&Vector4::x>("x")
                .Field<&Vector4::y>("y")
                .Field<&Vector4::z>("z")
                .Field<&Vector4::w>("w")
                .Property<[](const Vector4& v) { return Vector3 { v.x, v.y, v.z }; }>("xyz")
                .Method<&Vector4::Dot>("dot", { "other" })
                .Method<[](const Vector4& v) { return v; }>("copy")
                .Meta<[](const Vector4& a, const Vector4& b) { return a + b; }>("__add")
                .Meta<[](const Vector4& a, const Vector4& b) { return a - b; }>("__sub")
                .Meta<&Vector4Mul>("__mul")
                .Meta<&Vector4Equal>("__eq")
                .Meta<[](const Vector4& v) { return Format("Vector4(%g, %g, %g, %g)", v.x, v.y, v.z, v.w); }>("__tostring");
        }

        // ── Quaternion ──────────────────────────────────────────────────────────

        Quaternion MakeQuaternion(std::optional<float> x, std::optional<float> y, std::optional<float> z, std::optional<float> w)
        {
            if (!x && !y && !z && !w)
                return { };
            return { x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f), w.value_or(0.0f) };
        }

        bool QuaternionEqual(const Quaternion& a, const Quaternion& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

        /// q * q (q applied after the right one), q * v (rotates v)
        int QuaternionMul(lua_State* L)
        {
            const Quaternion& left = lua::StackTraits<Quaternion>::Check(L, 1);
            if (const Quaternion* right = lua::StackTraits<Quaternion>::Test(L, 2))
                lua::Push(L, math::Multiply(left, *right));
            else
                lua::Push(L, math::Rotate(left, lua::StackTraits<Vector3>::Check(L, 2)));
            return 1;
        }

        void RegisterQuaternion(lua::Module& module)
        {
            lua::Class<Quaternion>(module, "Quaternion")
                .Factory<&MakeQuaternion>({ "x", "y", "z", "w" })
                .Field<&Quaternion::x>("x")
                .Field<&Quaternion::y>("y")
                .Field<&Quaternion::z>("z")
                .Field<&Quaternion::w>("w")
                .Method<[](const Quaternion& q) { return math::Normalize(q); }>("normalized")
                .Method<[](const Quaternion& q) { return math::Conjugate(math::Normalize(q)); }>("inverse")
                .Method<[](const Quaternion& q, const Vector3& v) { return math::Rotate(q, v); }>("rotate", { "v" })
                .Method<[](const Quaternion& q) { return math::ToEuler(q); }>("to_euler")
                .Method<[](const Quaternion& a, const Quaternion& b) { return math::Dot(a, b); }>("dot", { "other" })
                .Method<[](const Quaternion& q) { return q; }>("copy")
                .StaticFunction<[] { return Quaternion { }; }>("identity")
                .StaticFunction<&math::QuaternionFromAxisAngle>("from_axis_angle", { "axis", "radians" })
                .StaticFunction<&math::QuaternionFromEuler>("from_euler", { "pitch_yaw_roll" })
                .StaticFunction<&math::Slerp>("slerp", { "from", "to", "t" })
                .Meta<&QuaternionMul>("__mul")
                .Meta<&QuaternionEqual>("__eq")
                .Meta<[](const Quaternion& q) { return Format("Quaternion(%g, %g, %g, %g)", q.x, q.y, q.z, q.w); }>("__tostring");
        }

        // ── Matrix4x4 ───────────────────────────────────────────────────────────

        /// Rows and columns are 1-based as everything in Lua.
        size_t CheckIndex(lua_State* L, int argument, lua_Integer value)
        {
            if (value < 1 || value > 4)
                luaL_argerror(L, argument, "matrix index must be 1..4");
            return static_cast<size_t>(value - 1);
        }

        float MatrixGet(lua_State* L, const Matrix4x4& m, lua_Integer row, lua_Integer column)
        {
            return m(CheckIndex(L, 2, row), CheckIndex(L, 3, column));
        }

        void MatrixSet(lua_State* L, Matrix4x4& m, lua_Integer row, lua_Integer column, float value)
        {
            m(CheckIndex(L, 2, row), CheckIndex(L, 3, column)) = value;
        }

        bool MatrixEqual(const Matrix4x4& a, const Matrix4x4& b) { return a.m == b.m; }

        /// m * m, m * Vector4
        int MatrixMul(lua_State* L)
        {
            const Matrix4x4& left = lua::StackTraits<Matrix4x4>::Check(L, 1);
            if (const Matrix4x4* right = lua::StackTraits<Matrix4x4>::Test(L, 2))
                lua::Push(L, left * *right);
            else
                lua::Push(L, left.Transform(lua::StackTraits<Vector4>::Check(L, 2)));
            return 1;
        }

        String MatrixToString(const Matrix4x4& m)
        {
            String text = "Matrix4x4(";
            for (size_t row = 0; row < 4; ++row) {
                text += Format(row ? "; %g, %g, %g, %g" : "%g, %g, %g, %g", m(row, 0), m(row, 1), m(row, 2), m(row, 3));
            }
            return text + ")";
        }

        void RegisterMatrix(lua::Module& module)
        {
            lua::Class<Matrix4x4>(module, "Matrix4x4")
                .Factory<[] { return Matrix4x4::Identity(); }>()
                .Property<[](const Matrix4x4& m) { return Vector3 { m(0, 3), m(1, 3), m(2, 3) }; },
                    [](Matrix4x4& m, const Vector3& t) { m(0, 3) = t.x; m(1, 3) = t.y; m(2, 3) = t.z; }>("translation")
                .Method<&MatrixGet>("get", { "row", "column" })
                .Method<&MatrixSet>("set", { "row", "column", "value" })
                .Method<[](const Matrix4x4& m, const Vector3& p) {
                    const Vector4 r = m.TransformPoint(p);
                    return Vector3 { r.x, r.y, r.z };
                }>("transform_point", { "point" })
                .Method<[](const Matrix4x4& m, const Vector3& d) {
                    const Vector4 r = m.Transform(Vector4 { d, 0.0f });
                    return Vector3 { r.x, r.y, r.z };
                }>("transform_direction", { "direction" })
                .Method<&Matrix4x4::Transposed>("transposed")
                .Method<[](const Matrix4x4& m) { return math::Inverse(m); }>("inverse")
                .Method<[](const Matrix4x4& m) {
                    const auto trs = math::DecomposeTRS(m);
                    return std::tuple { trs.translation, trs.rotation, trs.scale };
                }>("decompose")
                .Method<[](const Matrix4x4& m) { return m; }>("copy")
                .StaticFunction<[] { return Matrix4x4::Identity(); }>("identity")
                .StaticFunction<&Matrix4x4::Translation>("from_translation", { "translation" })
                .StaticFunction<&Matrix4x4::Scaling>("from_scale", { "scale" })
                .StaticFunction<&math::RotationMatrix>("from_rotation", { "rotation" })
                .StaticFunction<&math::ComposeTRS>("trs", { "translation", "rotation", "scale" })
                .StaticFunction<&Matrix4x4::LookAt>("look_at", { "eye", "target", "up" })
                .StaticFunction<&Matrix4x4::PerspectiveVk>("perspective", { "fov", "aspect", "near", "far" })
                .Meta<&MatrixMul>("__mul")
                .Meta<&MatrixEqual>("__eq")
                .Meta<&MatrixToString>("__tostring");
        }

    } // namespace

    void RegisterMath(lua::State& state)
    {
        lua::Module module(state, "elm.math");
        module
            .Function<[](float a, float b, float t) { return a + (b - a) * t; }>("lerp", { "a", "b", "t" })
            .Function<[](float value, float low, float high) { return std::clamp(value, low, high); }>("clamp", { "value", "low", "high" })
            .Function<[](float degrees) { return degrees * std::numbers::pi_v<float> / 180.0f; }>("radians", { "degrees" })
            .Function<[](float radians) { return radians * 180.0f / std::numbers::pi_v<float>; }>("degrees", { "radians" })
            .Value("pi", std::numbers::pi);

        RegisterVector3(module);
        RegisterVector4(module);
        RegisterQuaternion(module);
        RegisterMatrix(module);
    }

} // namespace elm::script::bindings
