local Game = { props = { } }

function Game:on_start()
end

function Game:on_update(dt)
end

function Game:add_brick()
    self.brick_count = (self.brick_count or 0) + 1
end

function Game:remove_brick()
    self.brick_count = (self.brick_count or 0) - 1
    if self.brick_count <= 0 then   
    end
end

return Game
