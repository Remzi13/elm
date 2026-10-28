-- Entity script: the player of Coin Rush. Arrows or WASD move it over the ground, relative to the camera.
--     player:add("Script", { module = "coin_rush.player", props = { speed = 6 } })
-- Other scripts send it back to the start: player.script:respawn()
local Vector3 = elm.math.Vector3
local input = elm.input

local Player = {
    props = {
        speed = 6, -- units per second
        arena = 9.5, -- the player stays within -arena..arena along X and Z
    },
}

local function is_down(first, second)
    return input.is_down(first) or input.is_down(second)
end

function Player:on_start()
    self.start = self.entity.position
end

function Player:on_update(dt)
    local move = self:read_direction()
    if move.length == 0 then
        return
    end
    local position = self.entity.position + move:normalized() * self.speed * dt
    position.x = elm.math.clamp(position.x, -self.arena, self.arena)
    position.z = elm.math.clamp(position.z, -self.arena, self.arena)
    self.entity.position = position
end

--- The direction the keys ask for, over the ground plane; zero when no key is down.
function Player:read_direction()
    local forward = elm.camera.forward()
    local right = elm.camera.right()
    forward.y, right.y = 0, 0
    local move = Vector3.zero()
    if is_down(input.key.Up, input.key.W) then move = move + forward end
    if is_down(input.key.Down, input.key.S) then move = move - forward end
    if is_down(input.key.Right, input.key.D) then move = move + right end
    if is_down(input.key.Left, input.key.A) then move = move - right end
    return move
end

function Player:respawn()
    self.entity.position = self.start
end

return Player
