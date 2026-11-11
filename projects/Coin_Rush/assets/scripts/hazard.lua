local Vector3 = elm.math.Vector3

local Hazard = {
    props = {
        offset = Vector3(6,0,0),
        speed = 0.5,
        game = "game",
        target="player",
    },
}

function Hazard:on_start()
    self.start = self.entity.position
    self.phase = 0
end

function Hazard:on_update(dt)
    self.phase = self.phase + self.speed * dt
    local t = (1 - math.cos(self.phase)) / 2 -- 0 at the start, 1 at the far end
    self.entity.position = self.start:lerp(self.start + self.offset, t)
end

function Hazard:on_trigger_enter(other)
    if other.name ~= self.target then
        return
    end
    local player = self:find_player()    
    if player then
        player:respawn()       
    end
    local game = elm.scene.find(self.game)
    if game then
        game.script:player_hit()
    end
end

function Hazard:find_player()
    local entity = elm.scene.find(self.target)
    return entity and entity.script
end

return Hazard
