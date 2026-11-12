local Vector3 = elm.math.Vector3
local Vector4 = elm.math.Vector4

local Brick = { 
    props = {
        hp = 1,
        game = "game",
    }
}

local COLORS = {
    [1] = Vector4(0.8, 0.2, 0.2, 1),
    [2] = Vector4(0.8, 0.8, 0.2, 1),
    [3] = Vector4(0.2, 0.8, 0.2, 1),
}

function Brick:on_start()
    self.game_entity = elm.scene.find(self.game)
    if self.game_entity then
        self.game_entity.script:add_brick()
    end    
    self.entity.color = COLORS[self.hp] or Vector4(0.8, 0.8, 0.8, 1)
end

function Brick:hit()
    if self.hp <= 0 then
        return
    end
    self.hp = self.hp - 1
    if self.hp <= 0 then
        if self.game_entity then
            self.game_entity.script:remove_brick()
        end
        self.entity:destroy()
    else
        self.entity.color = COLORS[self.hp] or Vector4(0.8, 0.8, 0.8, 1)
    end
end

return Brick
