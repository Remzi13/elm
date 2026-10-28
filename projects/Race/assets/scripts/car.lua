local Vector3 = elm.math.Vector3
local input = elm.input
local key = elm.input.key

local Car = {
    props = {
        max_speed = 25, -- units per second, forward
        max_reverse = 8, -- units per second, backward
        acceleration = 18, -- units per second^2 with the throttle down
        brake = 40, -- units per second^2 on Space or against the motion
        coast = 4, -- units per second^2 with no pedal
        turn_rate = 2.2, -- radians per second at full speed
        grip = 8, -- how fast sideways sliding dies out; lower drifts more
        ground_ray = 0.8, -- from the centre down; a bit longer than half the height
    },
}

--- 1, -1 or 0 by which of the two keys is down.
local function axis(positive, negative)
    local value = 0
    if input.is_down(positive) then value = value + 1 end
    if input.is_down(negative) then value = value - 1 end
    return value
end

--- Moves value toward target by at most step.
local function approach(value, target, step)
    if value < target then
        return math.min(value + step, target)
    end
    return math.max(value - step, target)
end

function Car:on_start()
    if not self.entity:has("RigidBody") then
        -- Low friction: grip is the script's job, otherwise the box drags over the ground
        self.entity:add("RigidBody", { mass = 1200, friction = 0.05, angular_damping = 0.9 })
    end
    self.body = self.entity.rigidbody
    self.spawn_position = self.entity.position
    self.spawn_rotation = self.entity.rotation
end

-- Presses are read per frame: a frame may have no fixed step or two of them
function Car:on_update(dt)
    if input.is_pressed(key.R) then
        self:respawn()
    end
end

function Car:on_fixed_update(dt)
    if not self:is_grounded() then
        return -- in the air the wheels do nothing, inertia and gravity carry the car
    end
    local rotation = self.entity.rotation
    local forward = rotation:rotate(Vector3.forward())
    local right = rotation:rotate(Vector3.right())
    local up = rotation:rotate(Vector3.up())

    local velocity = self.body.velocity
    local speed = self:next_speed(velocity:dot(forward), dt)
    local side = velocity:dot(right) * math.max(1 - self.grip * dt, 0)
    local vertical = velocity:dot(up)
    self.body.velocity = forward * speed + right * side + up * vertical

    -- Only the spin around the car's up is the script's; pitch and roll stay with physics (ramps, bumps)
    local spin = self.body.angular_velocity
    local tilt = spin - up * spin:dot(up)
    self.body.angular_velocity = tilt + up * self:yaw_rate(speed)
end

--- Forward speed after this step: throttle, brake or coasting.
function Car:next_speed(speed, dt)
    if input.is_down(key.Space) then
        return approach(speed, 0, self.brake * dt)
    end
    local throttle = axis(key.W, key.S)
    if throttle == 0 then
        return approach(speed, 0, self.coast * dt)
    end
    -- Pressing against the motion brakes first, then drives the other way
    if speed * throttle < 0 then
        return approach(speed, 0, self.brake * dt)
    end
    local limit = throttle > 0 and self.max_speed or self.max_reverse
    return approach(speed, throttle * limit, self.acceleration * dt)
end

--- Turn rate around the car's up: a standing car does not turn, a reversing one turns the other way.
function Car:yaw_rate(speed)
    local steer = axis(key.D, key.A)
    local share = elm.math.clamp(speed / self.max_speed, -1, 1)
    return steer * self.turn_rate * share
end

--- True when the ground is under the wheels; false in the air or upside down.
function Car:is_grounded()
    local down = -self.entity.rotation:rotate(Vector3.up())
    local hit = elm.physics.raycast(self.entity.world_position, down, self.ground_ray, self.entity)
    return hit ~= nil
end

function Car:respawn()
    -- Moving the node teleports a dynamic body
    self.entity.position = self.spawn_position
    self.entity.rotation = self.spawn_rotation
    self.body.velocity = Vector3.zero()
    self.body.angular_velocity = Vector3.zero()
end

return Car