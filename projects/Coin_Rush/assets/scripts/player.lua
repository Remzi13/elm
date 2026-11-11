-- Entity script: the player of Coin Rush. WASD or arrows move it over the ground, relative to the camera.
-- Other scripts send it back to the start: player.script:respawn()
local Vector3 = elm.math.Vector3
local input = elm.input

local Player = {
    props = {
        speed = 6, -- units per second
        arena = 24.5, -- the player stays within -arena..arena along X and Z
    },
}

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
    local ahead = input.axis("MoveForward")
    local side = input.axis("MoveRight")
    if input.is_down(input.key.Up) then ahead = ahead + 1 end
    if input.is_down(input.key.Down) then ahead = ahead - 1 end
    if input.is_down(input.key.Right) then side = side + 1 end
    if input.is_down(input.key.Left) then side = side - 1 end
    return forward * ahead + right * side
end

function Player:respawn()
    self.entity.position = self.start
end

return Player