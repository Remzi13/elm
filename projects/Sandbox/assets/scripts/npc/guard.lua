-- Entity script: a guard that patrols around its post and chases the player when it comes close.
-- Attach it to an entity:
--     guard:add("Script", { module = "npc.guard", props = { speed = 3 } })
-- Every entity gets its own instance (self): the props below are its defaults, its state lives in self.
-- Save this file while the engine runs: every guard picks up the new code and keeps its state.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4

local Guard = {
    props = {
        speed = 2.5,            -- units per second on patrol, a chase is 1.5 times faster
        view_distance = 8,      -- how close the target has to come to be noticed
        patrol_radius = 4,      -- waypoints are picked this far from the post
        target_name = "Player", -- the entity to chase
        line_of_sight = false,  -- bodies (crates, walls) hide the target; needs physics colliders
    },
}

local COLORS = {
    patrol = Vector4(0.85, 0.75, 0.30, 1),
    chase = Vector4(0.95, 0.25, 0.20, 1),
    returning = Vector4(0.60, 0.60, 0.60, 1),
}

function Guard:on_start()
    self.post = self.entity.position
    self.waypoint = self:pick_waypoint()
    self:set_state("patrol")
end

function Guard:on_update(dt)
    local target = self:find_target()
    if target and self:can_see(target) then
        self:set_state("chase")
    elseif self.state == "chase" then
        self:set_state("returning")
    end

    if self.state == "chase" then
        self:move_towards(target.position, self.speed * 1.5, dt)
    elseif self.state == "returning" then
        if self:move_towards(self.post, self.speed, dt) then
            self:set_state("patrol")
        end
    elseif self:move_towards(self.waypoint, self.speed, dt) then
        self.waypoint = self:pick_waypoint()
    end
end

function Guard:on_destroy()
    elm.log.info(self.entity.name .. " left its post")
end

-- After this file is saved: the colors may have changed
function Guard:on_script_reload()
    self.entity.color = COLORS[self.state]
end

-- Methods of the class are shared by all guards; other scripts may call them too (guard.script:set_state("chase"))

function Guard:set_state(state)
    if self.state ~= state then
        self.state = state
        self.entity.color = COLORS[state]
    end
end

function Guard:find_target()
    -- elm.scene.find walks the whole scene: keep the result while the entity lives
    if not (self.target and self.target.valid) then
        self.target = elm.scene.find(self.target_name)
    end
    return self.target
end

function Guard:can_see(target)
    local position = self.entity.position
    local to_target = target.position - position
    local distance = to_target.length
    if distance >= self.view_distance then
        return false
    end
    if not self.line_of_sight then
        return true
    end
    -- The first body along the ray has to be the target; the ray starts inside the guard's own collider
    local hit = elm.physics.raycast(position, to_target, distance + 1, self.entity)
    return hit == target
end

function Guard:pick_waypoint()
    local angle = math.random() * 2 * math.pi
    return self.post + Vector3(math.cos(angle), 0, math.sin(angle)) * self.patrol_radius
end

--- Moves over the ground plane towards a point; true once it is there.
function Guard:move_towards(point, speed, dt)
    local position = self.entity.position
    local delta = point - position
    delta.y = 0
    local distance = delta.length
    if distance < 0.1 then
        return true
    end
    self.entity.position = position + delta:normalized() * math.min(distance, speed * dt)
    return false
end

return Guard
