local Vector3, Quaternion = elm.math.Vector3, elm.math.Quaternion
local input = elm.input

local Spinner = { props = {speed = 1.5} }

function Spinner:on_start()
   self.angle = 0
   self.direction = 1
end

function Spinner:on_update(dt)
  if input.is_pressed(input.key.Space) then
    self.direction = -self.direction
  end
  self.angle = self.angle + self.speed * self.direction * dt
  self.entity.rotation = Quaternion.from_axis_angle(Vector3(0, 1, 0), self.angle)
end

return Spinner
