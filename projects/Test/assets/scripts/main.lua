local Vector3 = elm.math.Vector3

local main = {}

function main.on_start()
    elm.camera.set_position(Vector3(0, 3, -6))
    elm.camera.look_at(Vector3(0, 0.5, 0))
end

return main