-- Entity script for a trigger collider: throws every dynamic body that enters it upwards.
--     pad:add("Collider", { half_extents = Vector3(1.5, 0.5, 1.5), trigger = true })
--     pad:add("Script", { module = "gameplay.launch_pad", props = { launch_speed = 12 } })
local Vector3 = elm.math.Vector3

local LaunchPad = {
    props = {
        launch_speed = 10, -- upward velocity a body gains, m/s, whatever its mass
    },
}

function LaunchPad:on_trigger_enter(other)
    local body = other.rigidbody
    if body and body.type == "dynamic" then
        body:add_impulse(Vector3(0, self.launch_speed * body.mass, 0))
    end
end

return LaunchPad
