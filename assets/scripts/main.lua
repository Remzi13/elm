-- Entry point of the game scripts (setting Script/MainModule).
--
-- build_scene() describes the scene. Edit this file (Scripts window > Editor, or any editor)
-- and save: the module reloads and on_reload builds the scene again with the new code.
-- "Rebuild scene" in the Scripts window (or /scene in its console) calls build_scene() too.
--
-- Set BUILD_SCENE = false to keep the scene of --scene or of the Scene Hierarchy window.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4

local main = {}

local BUILD_SCENE = true

local GRID = 4          -- boxes from -GRID to GRID along x and z
local SPACING = 3
local GROUND_COLOR = Vector4(0.30, 0.34, 0.30, 1)

-- Entities of the current scene, rebuilt by build_scene
local boxes = {}

function main.build_scene()
    elm.engine.clear_scene()
    boxes = {}

    local ground = elm.scene.spawn("Ground", elm.models.plane(60, 60))
    ground.color = GROUND_COLOR

    local cube = elm.models.cube(1)
    for x = -GRID, GRID do
        for z = -GRID, GRID do
            local box = elm.scene.spawn(string.format("Box %d,%d", x, z), cube)
            local height = 1 + ((x * x + z * z) % 3)
            box.scale = Vector3(1, height, 1)
            box.position = Vector3(x * SPACING, height / 2, z * SPACING)
            box.color = Vector4(0.5 + x / (4 * GRID), 0.6, 0.5 + z / (4 * GRID), 1)
            boxes[#boxes + 1] = box
        end
    end

    elm.camera.set_position(Vector3(0, 3, -(GRID * SPACING + 12)))
    elm.camera.look_at(Vector3(0, 0, 0))
    print(string.format("main: scene built, %d entities", elm.scene.count()))
end

function main.on_start()
    if BUILD_SCENE then
        main.build_scene()
    end
end

-- The new version of this file is running: show what it builds
function main.on_reload()
    if BUILD_SCENE then
        main.build_scene()
    end
end

function main.on_update(dt)
end

function main.on_fixed_update(dt)
end

function main.on_shutdown()
end

return main
