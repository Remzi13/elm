-- elm.scene entities. The self test scene starts empty and has no model manager.
local expect = require "tests.expect"
local scene = elm.scene
local Vector3, Vector4, Quaternion = elm.math.Vector3, elm.math.Vector4, elm.math.Quaternion

local function near_vector(actual, expected)
    if (actual - expected).length > 1e-4 then
        error(string.format("expected %s, got %s", tostring(expected), tostring(actual)), 2)
    end
end

local function clear()
    for _, entity in ipairs(scene.all()) do
        if entity.valid then entity:destroy() end
    end
end

local tests = {}

function tests.spawn_and_find()
    clear()
    local a = scene.spawn("a")
    local b = scene.spawn("b")
    expect.eq(scene.count(), 2)
    expect.eq(a.name, "a")
    expect.eq(scene.find("b"), b)
    expect.eq(scene.find("missing"), nil)
    expect.eq(#scene.find_all("a"), 1)
    expect.eq(tostring(a), "Entity(a)")
    expect.eq(scene.spawn().name, "Entity", "default name")
end

function tests.entities_are_stable_objects()
    clear()
    local a = scene.spawn("a")
    expect.eq(rawequal(scene.find("a"), a), true, "the same node gives the same object")
    local marks = { [a] = true }
    expect.eq(marks[scene.find("a")], true, "entities work as table keys")
    expect.eq(type(a.id), "number")
end

function tests.properties()
    clear()
    local e = scene.spawn("e")
    e.name = "renamed"
    expect.eq(e.name, "renamed")
    e.visible = false
    expect.eq(e.visible, false)
    e.color = Vector4(1, 0, 0, 1)
    expect.eq(e.color, Vector4(1, 0, 0, 1))
    expect.eq(e.model, nil)
    expect.error(function() e.unknown = 1 end, "no writable field 'unknown'")
end

function tests.local_transform()
    clear()
    local e = scene.spawn("e")
    e.position = Vector3(1, 2, 3)
    expect.eq(e.position, Vector3(1, 2, 3))
    e:translate(Vector3(1, 0, 0))
    expect.eq(e.position, Vector3(2, 2, 3))
    e.scale = Vector3(2, 2, 2)
    near_vector(e.scale, Vector3(2, 2, 2))
    near_vector(e.position, Vector3(2, 2, 3), "scale keeps the position")
    e.rotation = Quaternion.from_axis_angle(Vector3.up(), math.pi / 2)
    near_vector(e.rotation * Vector3(1, 0, 0), Vector3(0, 0, -1))
    near_vector(e.scale, Vector3(2, 2, 2), "rotation keeps the scale")
    e:rotate(Vector3.up(), math.pi / 2)
    near_vector(e.rotation * Vector3(1, 0, 0), Vector3(-1, 0, 0))
end

function tests.hierarchy_and_world_transform()
    clear()
    local parent = scene.spawn("parent")
    local child = scene.spawn("child", nil, parent)
    expect.eq(child.parent, parent)
    expect.eq(#parent:children(), 1)
    parent.position = Vector3(10, 0, 0)
    child.position = Vector3(0, 1, 0)
    near_vector(child.world_position, Vector3(10, 1, 0), "world follows the parent at once")

    child.parent = nil
    expect.eq(child.parent, nil)
    near_vector(child.world_position, Vector3(10, 1, 0), "detaching keeps the world position")
    near_vector(child.position, Vector3(10, 1, 0))

    child.parent = parent
    near_vector(child.position, Vector3(0, 1, 0), "attaching keeps the world position")
    expect.error(function() parent.parent = child end, "cannot become a child of itself or of its descendant")
    expect.error(function() parent.parent = parent end, "cannot become a child of itself")
end

function tests.world_transform_write()
    clear()
    local parent = scene.spawn("parent")
    parent.position = Vector3(5, 0, 0)
    local child = scene.spawn("child", nil, parent)
    local world = child.world_transform
    world.translation = Vector3(5, 5, 5)
    child.world_transform = world
    near_vector(child.position, Vector3(0, 5, 5))
end

function tests.destroy_removes_descendants()
    clear()
    local root = scene.spawn("root")
    local child = scene.spawn("child", nil, root)
    local grandchild = scene.spawn("grandchild", nil, child)
    local other = scene.spawn("other")
    root:destroy()
    expect.eq(root.valid, false)
    expect.eq(child.valid, false)
    expect.eq(grandchild.valid, false)
    expect.eq(other.valid, true)
    expect.eq(scene.count(), 1)
    expect.eq(tostring(root), "Entity(destroyed)")
    expect.error(function() return root.name end, "the entity was destroyed")
    expect.error(function() root.position = Vector3() end, "the entity was destroyed")
end

function tests.ids_survive_other_removals()
    clear()
    local a, b, c = scene.spawn("a"), scene.spawn("b"), scene.spawn("c")
    a:destroy()
    expect.eq(c.name, "c", "c moved to another index but is still found")
    b:destroy()
    expect.eq(c.name, "c")
end

function tests.models_need_the_renderer()
    expect.error(function() elm.models.cube() end, "elm.models is not available here")
end

return tests
