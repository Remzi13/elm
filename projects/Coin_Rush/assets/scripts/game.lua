-- Entity script: the rules of Coin Rush. Coins report to it (add_coin, collect), hazards report hits
-- (player_hit). It stretches the score bar and turns the ground green when every coin is taken.
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4

local Game = {
    props = {
        bar = "score_bar", -- stretched along X as coins are taken; may be missing
        bar_length = 20,
        ground = "ground",
    },
}

local WIN_COLOR = Vector4(0.20, 0.55, 0.25, 1)

-- Coins may start before the game: add_coin works before on_start too
function Game:add_coin()
    self.total = (self.total or 0) + 1
    self:show_score()
end

function Game:on_start()
    self.total = self.total or 0
    self.taken = 0
    self:show_score()
end

function Game:collect()
    self.taken = (self.taken or 0) + 1
    self:show_score()
    print(string.format("Coin Rush: %d / %d", self.taken, self.total))
    if self.taken == self.total then
        self:finish(WIN_COLOR, "all coins taken")
    end
end

function Game:finish(color, reason)
    local ground = elm.scene.find(self.ground)
    if ground then
        ground.color = color
    end
    print("Coin Rush: " .. reason .. ". Stop and Play to play again")
end

function Game:show_score()
    local bar = elm.scene.find(self.bar)
    if not bar then
        return
    end
    local share = (self.total or 0) > 0 and (self.taken or 0) / self.total or 0
    local length = math.max(share * self.bar_length, 0.01)
    bar.scale = Vector3(length, 0.5, 0.5)
    -- The end at x = bar_length / 2 stays in place, the bar grows from it
    local position = bar.position
    position.x = (self.bar_length - length) / 2
    bar.position = position
end

return Game