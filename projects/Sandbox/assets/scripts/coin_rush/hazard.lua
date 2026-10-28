-- Entity script: a hazard of Coin Rush. It goes back and forth between its start and start + offset;
-- the player it touches goes back to the start.
--     hazard:add("Script", { module = "coin_rush.hazard", props = { offset = Vector3(8, 0, 0) } })
local Vector3 = elm.math.Vector3

local Hazard = {
    props = {
        offset = Vector3(6, 0, 0), -- the far end of the route, from the start
        speed = 1.5, -- how fast it swings, radians per second
        reach = 1.1, -- the player this close (centre to centre) is hit
        target = "Player",
        game = "Game",
    },
}

function Hazard:on_start()
    self.from = self.entity.position
    self.to = self.from + self.offset
    self.phase = 0
end

function Hazard:on_update(dt)
    self.phase = self.phase + self.speed * dt
    -- 0 at the start, 1 at the far end, smooth at both ends
    local t = (1 - math.cos(self.phase)) / 2
    self.entity.position = self.from:lerp(self.to, t)

    local player = elm.scene.find(self.target)
    if player and player.position:distance(self.entity.position) < self.reach then
        self:hit(player)
    end
end

function Hazard:hit(player)
    player.script:respawn()
    local game = elm.scene.find(self.game)
    if game then
        game.script:player_hit()
    end
end

return Hazard
