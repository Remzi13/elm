-- Demo: a ring of spinning cubes around a pivot. Set entry.mainModule of Sandbox.elmproject to "demo.spinner" to run it.
-- Space reverses the spin, R toggles the colors. Edit SPIN_SPEED or BOB_HEIGHT and save: the file
-- reloads while the engine runs, the cubes keep their place because the state is persisted.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4
local input = elm.input

local demo = {}

local COUNT = 12
local RADIUS = 6
local SPIN_SPEED = 0.8
local BOB_HEIGHT = 0.75

-- Module locals start over on every reload, the persisted table does not
local state = elm.script.persist("demo.spinner", { cubes = {}, direction = 1, colorful = true })

local function paint()
    for i, cube in ipairs(state.cubes) do
        if cube.valid then
            local hue = (i - 1) / COUNT
            cube.color = state.colorful and Vector4(0.5 + 0.5 * math.cos(hue * 6.28), 0.5 + 0.5 * math.sin(hue * 6.28), 1 - hue, 1)
                or Vector4(0.8, 0.8, 0.8, 1)
        end
    end
end

function demo.on_start()
    local model = elm.models.find("Cube") or elm.models.cube(1.0)
    state.pivot = elm.scene.spawn("Spinner")
    state.pivot.position = Vector3(0, 3, 0)
    state.cubes = {}

    for i = 1, COUNT do
        local angle = (i - 1) / COUNT * 2 * math.pi
        local cube = elm.scene.spawn("Spinner Cube " .. i, model, state.pivot)
        cube.position = Vector3(math.cos(angle) * RADIUS, 0, math.sin(angle) * RADIUS)
        cube.scale = Vector3(0.6, 0.6, 0.6)
        state.cubes[i] = cube
    end
    paint()
    elm.log.info(string.format("spinner: %d cubes around %s", COUNT, tostring(state.pivot)))
end

function demo.on_reload()
    paint()
    elm.log.info(string.format("spinner reloaded: speed %.2f, bob %.2f", SPIN_SPEED, BOB_HEIGHT))
end

function demo.on_update(dt)
    local pivot = state.pivot
    if not (pivot and pivot.valid) then
        demo.on_start() -- the scene was replaced or the spinner removed: build it again
        pivot = state.pivot
    end
    if input.is_pressed(input.key.Space) then
        state.direction = -state.direction
    end
    if input.is_pressed(input.key.R) then
        state.colorful = not state.colorful
        paint()
    end

    pivot:rotate(Vector3.up(), state.direction * dt * SPIN_SPEED)
    local t = elm.time.now()
    for i, cube in ipairs(state.cubes) do
        if cube.valid then
            cube:rotate(Vector3(1, 1, 0):normalized(), dt * 2)
            local p = cube.position
            p.y = math.sin(t * 2 + i) * BOB_HEIGHT
            cube.position = p
        end
    end
end

function demo.on_shutdown()
    if state.pivot and state.pivot.valid then
        state.pivot:destroy()
    end
end

return demo
