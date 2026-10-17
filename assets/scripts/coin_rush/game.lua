-- Entity script: the rules of Coin Rush. Coins report to it (add_coin, collect), hazards report hits
-- (player_hit). It stretches the score bar and says when every coin is taken.
--     game:add("Script", { module = "coin_rush.game", props = { bar_length = 20 } })
local Vector3, Vector4 = elm.math.Vector3, elm.math.Vector4

local Game = {
    props = {
        bar = "Score bar", -- the entity stretched along X as coins are taken
        bar_length = 20,
        ground = "Ground", -- turns green on a win
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
    self.hits = 0
    self:show_score()
end

function Game:collect()
    self.taken = (self.taken or 0) + 1
    self:show_score()
    print(string.format("Coin Rush: %d / %d", self.taken, self.total))
    if self.taken == self.total then
        self:win()
    end
end

function Game:player_hit()
    self.hits = (self.hits or 0) + 1
    print(string.format("Coin Rush: hit! back to the start (%d hits)", self.hits))
end

function Game:win()
    local ground = elm.scene.find(self.ground)
    if ground then
        ground.color = WIN_COLOR
    end
    print(string.format("Coin Rush: all %d coins, %d hits. F5 twice plays again", self.total, self.hits))
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
