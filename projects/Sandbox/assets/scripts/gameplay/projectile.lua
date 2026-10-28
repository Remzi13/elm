-- Entity script: a thrown body that flashes white on every hit and goes away after a while.
--     stone:add("Script", { module = "gameplay.projectile", props = { lifetime = 10 } })
local Vector4 = elm.math.Vector4

local Projectile = {
    props = {
        lifetime = 8, -- seconds before the entity is destroyed
    },
}

local FLASH = Vector4(1, 1, 1, 1)
local FLASH_TIME = 0.15

function Projectile:on_start()
    self.age = 0
    self.color = self.entity.color
end

function Projectile:on_update(dt)
    self.age = self.age + dt
    if self.flash then
        self.flash = self.flash - dt
        if self.flash <= 0 then
            self.flash = nil
            self.entity.color = self.color
        end
    end
    if self.age > self.lifetime then
        self.entity:destroy()
    end
end

-- contact.point and contact.normal (from this entity towards the other one) are there too
function Projectile:on_collision_enter(other, contact)
    self.flash = FLASH_TIME
    self.entity.color = FLASH
end

return Projectile
