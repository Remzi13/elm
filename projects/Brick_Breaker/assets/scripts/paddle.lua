local input = elm.input
local Vector3 = elm.math.Vector3

local Paddle = {
    props = {
        speed = 6,
        limit = 4, 
    },
}

function Paddle:on_start()
end

function Paddle:on_update(dt)
    local side = input.axis("MoveRight")    
    local position = self.entity.position
    if input.is_down(input.key.Left) then side = side - 1 end
    if input.is_down(input.key.Right) then side = side + 1 end
    side = elm.math.clamp(side, -1, 1)
    position.x = position.x + side * elm.camera.right().x * self.speed * dt
    self.entity.position = Vector3(elm.math.clamp(position.x, -self.limit, self.limit), position.y, position.z)
end

return Paddle
