local Vector4 = elm.math.Vector4

local Game = { 
    props = {
        lives = 3,
        ground = "ground", 
        next_scene = "",
        next_delay = 2,
    },
 }

local WIN_COLOR = Vector4(0.20, 0.55, 0.25, 1)
local LOSE_COLOR = Vector4(0.55, 0.20, 0.25, 1)

function Game:on_update(dt)
    if self.next_in and not self.over then
        self.next_in = self.next_in -dt
    end
end

function Game:wants_next_scene()
    return self.next_in ~= nil and self.next_in <= 0
end

function Game:add_brick()
    self.brick_count = (self.brick_count or 0) + 1
end

function Game:remove_brick()
    if self.over then return end
    self.brick_count = (self.brick_count or 0) - 1
    if self.brick_count <= 0 then   
        if self.next_scene ~= "" then
            self.next_in = self.next_delay
        else
            self:finish(WIN_COLOR, "all bricks broken")
        end
    end
end

function Game:ball_lost()
    if self.over then return end
    local life = elm.scene.find("life_" .. self.lives)  -- before decrement: 3, 2, 1
    if life then life.visible = false end
    self.lives = self.lives - 1
    if self.lives <= 0 then self:finish(LOSE_COLOR, "no lives left") end    
end

function Game:finish(color, reason)
    self.over = true
    local ground = elm.scene.find(self.ground)
    if ground then
        ground.color = color
    end
    print("Brick Breaker: " .. reason .. ". Stop and Play to play again")
end


return Game
