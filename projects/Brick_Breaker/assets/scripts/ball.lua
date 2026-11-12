local input = elm.input
local Vector3 = elm.math.Vector3

local Ball = { 
    props = {
        speed = 8,            -- units per second
        radius = 0.2,         -- radius of the ball model
        paddle = "paddle",
        game = "game",
        lose_z = 8.5,         -- past this Z the ball is lost (near edge of the floor)
        max_angle = 60,       -- degrees: bounce angle off the paddle edge
    }
}

function Ball:on_start()
    self.held = true    
    self.paddle_entity = elm.scene.find(self.paddle)
end

function Ball:on_update(dt)
    if self.held == true then
        self.entity.position = self.paddle_entity.position + Vector3(0, 0, -0.4)    
        if input.is_pressed(input.key.Space) then
            self.held = false
            self.direction = Vector3(0.3, 0, -1):normalized()
        end
        return
    end
    local step = self.speed * dt
    local position = self.entity.position
    local hit, point, normal, distance = elm.physics.raycast(position, self.direction, step + self.radius, self.entity)
    if hit then
        -- stop at the surface and bounce
        self.entity.position = position + self.direction * (distance - self.radius)
        local d = self.direction
        d = d - normal * (2 * d:dot(normal))
        d.y = 0
        if hit.name == self.paddle then
            local offset = elm.math.clamp(point.x - hit.world_position.x, -1, 1) -- 1 = half the paddle width
            local angle = elm.math.radians(self.max_angle) * offset
            self.direction = Vector3(math.sin(angle), 0, -math.cos(angle))
        else
            self.direction = d:normalized()
        end        
        if hit.script and hit.script.hit then
            hit.script:hit()
        end    
    else
        self.entity.position = position + self.direction * step
    end
    if self.entity.position.z > self.lose_z then
        self.held = true
        local game = elm.scene.find(self.game)
        if game and game.script and game.script.ball_lost then
            game.script:ball_lost()
        end
    end
end

return Ball
