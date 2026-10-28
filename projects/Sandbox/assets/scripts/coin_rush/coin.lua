-- Entity script: a coin of Coin Rush. It spins and goes to the game when the player touches it.
-- Needs a trigger collider:
--     coin:add("Collider", { shape = "sphere", radius = 0.6, trigger = true })
--     coin:add("Script", "coin_rush.coin")
local Vector3 = elm.math.Vector3

local Coin = {
    props = {
        spin_speed = 3, -- radians per second
        collector = "Player", -- the entity that takes coins
        game = "Game", -- the entity with the coin_rush.game script
    },
}

function Coin:on_start()
    -- Every coin counts itself: copies made in the editor count too
    local game = self:find_game()
    if game then
        game:add_coin()
    end
end

function Coin:on_update(dt)
    self.entity:rotate(Vector3.up(), self.spin_speed * dt)
end

function Coin:on_trigger_enter(other)
    if other.name ~= self.collector then
        return
    end
    local game = self:find_game()
    if game then
        game:collect()
    end
    self.entity:destroy()
end

--- The script instance of the game entity, nil when there is none.
function Coin:find_game()
    local entity = elm.scene.find(self.game)
    return entity and entity.script
end

return Coin
