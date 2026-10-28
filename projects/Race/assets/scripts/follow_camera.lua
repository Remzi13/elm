local Vector3 = elm.math.Vector3
local camera = elm.camera

--- Third-person chase camera script.
--- Can be attached as a script component to any entity,
--- or required and reused in other scripts (e.g. FollowCamera.new(entity, config)).
local FollowCamera = {
    props = {
        distance = 3, -- units behind the target
        height = 1.2, -- units above the target
        look_height = 0.3, -- height above the target to look at
        smoothness = 8, -- how fast the camera follows (higher = stiffer, 0 = instant snap)
    },
}

--- Creates a camera controller instance for an entity.
function FollowCamera.new(entity, config)
    config = config or {}
    local instance = {
        entity = entity,
        distance = config.distance or FollowCamera.props.distance,
        height = config.height or FollowCamera.props.height,
        look_height = config.look_height or FollowCamera.props.look_height,
        smoothness = config.smoothness or FollowCamera.props.smoothness,
    }
    return setmetatable(instance, { __index = FollowCamera })
end

--- Smoothly or instantly positions the camera behind the target entity.
function FollowCamera:update(dt, snap)
    local target = self.entity
    if not target or (target.valid ~= nil and not target.valid) then
        return
    end

    local target_pos = target.world_position
    local forward = target.rotation:rotate(Vector3.forward())
    local heading = Vector3(forward.x, 0, forward.z)
    if heading.length > 0.001 then
        heading = heading:normalized()
    else
        heading = Vector3.forward()
    end

    local distance = self.distance or FollowCamera.props.distance
    local height = self.height or FollowCamera.props.height
    local look_height = self.look_height or FollowCamera.props.look_height
    local smoothness = self.smoothness or FollowCamera.props.smoothness

    local desired_pos = target_pos - heading * distance + Vector3(0, height, 0)
    local target_look = target_pos + Vector3(0, look_height, 0)

    if snap or smoothness <= 0 then
        camera.set_position(desired_pos)
    else
        local current_pos = camera.position()
        local t = elm.math.clamp(1.0 - math.exp(-smoothness * (dt or 0)), 0, 1)
        camera.set_position(current_pos:lerp(desired_pos, t))
    end

    camera.look_at(target_look)
end

--- Instantly places the camera behind the target without smoothing.
function FollowCamera:snap()
    self:update(0, true)
end

function FollowCamera:on_start()
    self:snap()
end

function FollowCamera:on_update(dt)
    self:update(dt, false)
end

return FollowCamera

