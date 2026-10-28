-- elm.math: vectors, quaternions, matrices.
local expect = require "tests.expect"
local m = elm.math
local Vector3, Vector4, Quaternion, Matrix4x4 = m.Vector3, m.Vector4, m.Quaternion, m.Matrix4x4

local function near_vector(actual, expected, epsilon)
    epsilon = epsilon or 1e-4
    if (actual - expected).length > epsilon then
        error(string.format("expected %s, got %s", tostring(expected), tostring(actual)), 2)
    end
end

local tests = {}

function tests.functions()
    expect.eq(m.lerp(0, 10, 0.25), 2.5)
    expect.eq(m.clamp(5, 0, 3), 3)
    expect.near(m.radians(180), m.pi)
    expect.near(m.degrees(m.pi / 2), 90, 1e-4)
end

function tests.vector3_construction()
    local zero = Vector3()
    expect.eq(zero, Vector3(0, 0, 0), "missing components are zero")
    expect.eq(Vector3(1, 2), Vector3(1, 2, 0))
    expect.eq(Vector3.up(), Vector3(0, 1, 0))
    expect.eq(Vector3.one(), Vector3(1, 1, 1))
end

function tests.vector3_arithmetic()
    local a, b = Vector3(1, 2, 3), Vector3(4, 5, 6)
    expect.eq(a + b, Vector3(5, 7, 9))
    expect.eq(b - a, Vector3(3, 3, 3))
    expect.eq(-a, Vector3(-1, -2, -3))
    expect.eq(a * 2, Vector3(2, 4, 6))
    expect.eq(2 * a, Vector3(2, 4, 6))
    expect.eq(a * b, Vector3(4, 10, 18), "per component")
    expect.eq(b / 2, Vector3(2, 2.5, 3))
    expect.eq(a:dot(b), 32)
    expect.eq(Vector3.right():cross(Vector3.up()), Vector3(0, 0, 1))
    expect.near(Vector3(3, 4, 0).length, 5)
    expect.eq(Vector3(3, 4, 0).length_squared, 25)
    expect.near(Vector3(0, 0, 5):normalized().length, 1)
    expect.near(a:distance(a + Vector3(0, 3, 4)), 5)
    expect.eq(a:lerp(b, 0.5), Vector3(2.5, 3.5, 4.5))
    expect.eq(tostring(a), "Vector3(1, 2, 3)")
end

function tests.vector3_reference_semantics()
    local a = Vector3(1, 2, 3)
    local alias = a
    alias.x = 10
    expect.eq(a.x, 10, "assignment shares the object")
    local copy = a:copy()
    copy.x = 0
    expect.eq(a.x, 10, "copy() makes an independent value")
    local target = Vector3()
    target:assign(a)
    expect.eq(target, a)
end

function tests.vector4()
    local v = Vector4(1, 2, 3, 4)
    expect.eq(v.w, 4)
    expect.eq(v.xyz, Vector3(1, 2, 3))
    expect.eq(v + v, Vector4(2, 4, 6, 8))
    expect.eq(v * 2, Vector4(2, 4, 6, 8))
    expect.eq(v:dot(Vector4(1, 1, 1, 1)), 10)
end

function tests.quaternion_identity()
    local q = Quaternion()
    expect.eq(q, Quaternion(0, 0, 0, 1), "no arguments: identity")
    expect.eq(Quaternion.identity(), q)
    near_vector(q * Vector3(1, 2, 3), Vector3(1, 2, 3))
end

function tests.quaternion_rotation()
    -- 90 degrees around Y turns +X into -Z (right-handed, column vectors)
    local q = Quaternion.from_axis_angle(Vector3.up(), m.pi / 2)
    near_vector(q:rotate(Vector3(1, 0, 0)), Vector3(0, 0, -1))
    near_vector(q * Vector3(1, 0, 0), Vector3(0, 0, -1))
    near_vector((q * q) * Vector3(1, 0, 0), Vector3(-1, 0, 0))
    near_vector(q:inverse() * (q * Vector3(1, 2, 3)), Vector3(1, 2, 3))
end

function tests.quaternion_euler_round_trip()
    local angles = Vector3(0.3, -1.1, 0.7)
    local q = Quaternion.from_euler(angles)
    near_vector(q:to_euler(), angles)
    expect.near(q:normalized():dot(q:normalized()), 1)
end

function tests.quaternion_slerp()
    local a = Quaternion.identity()
    local b = Quaternion.from_axis_angle(Vector3.up(), m.pi / 2)
    local half = Quaternion.slerp(a, b, 0.5)
    near_vector(half * Vector3(1, 0, 0), Vector3(math.sqrt(0.5), 0, -math.sqrt(0.5)))
end

function tests.matrix_basics()
    local identity = Matrix4x4()
    expect.eq(identity, Matrix4x4.identity())
    expect.eq(identity:get(1, 1), 1)
    expect.eq(identity:get(1, 2), 0)
    identity:set(1, 4, 7)
    expect.eq(identity.translation, Vector3(7, 0, 0), "column 4 holds the translation")
    expect.error(function() identity:get(0, 1) end, "matrix index must be 1..4")
    expect.error(function() identity:get(1, 5) end, "matrix index must be 1..4")
end

function tests.matrix_transforms()
    local t = Matrix4x4.from_translation(Vector3(1, 2, 3))
    near_vector(t:transform_point(Vector3(1, 1, 1)), Vector3(2, 3, 4))
    near_vector(t:transform_direction(Vector3(1, 1, 1)), Vector3(1, 1, 1))
    local s = Matrix4x4.from_scale(Vector3(2, 2, 2))
    near_vector((t * s):transform_point(Vector3(1, 0, 0)), Vector3(3, 2, 3))
    local v = t * Vector4(0, 0, 0, 1)
    expect.eq(v, Vector4(1, 2, 3, 1))
end

function tests.matrix_trs_round_trip()
    local translation = Vector3(1, -2, 3)
    local rotation = Quaternion.from_euler(Vector3(0.2, 0.4, -0.3))
    local scale = Vector3(2, 0.5, 3)
    local trs = Matrix4x4.trs(translation, rotation, scale)
    local t, r, s = trs:decompose()
    near_vector(t, translation)
    near_vector(s, scale)
    near_vector(r * Vector3(1, 2, 3), rotation * Vector3(1, 2, 3))
end

function tests.matrix_inverse()
    local trs = Matrix4x4.trs(Vector3(1, 2, 3), Quaternion.from_euler(Vector3(0.5, 0.1, 0)), Vector3(2, 2, 2))
    local inverse = trs:inverse()
    near_vector(inverse:transform_point(trs:transform_point(Vector3(4, 5, 6))), Vector3(4, 5, 6))
    expect.eq(Matrix4x4.from_scale(Vector3(0, 1, 1)):inverse(), nil, "singular matrices have no inverse")
end

return tests
