
local Vector3 = elm.math.Vector3
local input = elm.input
local key = elm.input.key
local camera = elm.camera

local Agent = require("car_agent")

local Car = {
    props = {
        max_speed = 25,
        max_reverse = 8,
        acceleration = 18,
        brake = 40,
        coast = 4,
        turn_rate = 2.2,
        grip = 8,
        ground_ray = 0.8,

        camera_distance = 3,
        camera_height = 1.2,
        camera_look_height = 0.3,
        camera_smoothness = 8,

        -- Дальность обнаружения препятствий.
        sensor_distance = 4.0,

        -- Расстояние, на котором считаем попытку проваленной.
        collision_distance = 0.45,

        -- Защита от бесконечной попытки.
        max_episode_time = 60,
    },
}

local function approach(value, target, step)
    if value < target then
        return math.min(value + step, target)
    end

    return math.max(value - step, target)
end

local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(max_value, value))
end

local function axis(positive, negative)
    local value = 0

    if input.is_down(positive) then
        value = value + 1
    end

    if input.is_down(negative) then
        value = value - 1
    end

    return value
end

function Car:on_start()
    if not self.entity:has("RigidBody") then
        self.entity:add("RigidBody", {
            mass = 1200,
            friction = 0.05,
            angular_damping = 0.9,
        })
    end

    self.body = self.entity.rigidbody

    self.spawn_position = self.entity.position
    self.spawn_rotation = self.entity.rotation

    self.agent = Agent.new()
    self.agent:begin_episode()

    self.control = {
        steering = 0,
        throttle = 0,
        brake = 0,
    }

    self.episode_time = 0
    self.previous_position = self.entity.world_position

    self:update_camera(0, true)
end

function Car:on_update(dt)
    -- R позволяет вручную начать новую попытку.
    if input.is_pressed(key.R) then
        self:finish_episode(true)
    end

    self:update_camera(dt, false)
end

-- Основной физический цикл.
function Car:on_fixed_update(dt)

    self.episode_time = self.episode_time + dt

    if self.episode_time >= self.max_episode_time then
        self:finish_episode(false)
        return
    end

    if not self:is_grounded() then
        elm.log.info("Car is airborne, ending episode.")
        return
    end

    local observation = self:observe()

    -- Агент самостоятельно выбирает действия.
    self.control = self.agent:act(observation)

    -- Если стена уже очень близко, завершаем попытку.
    -- Это приближение столкновения, а не реальное событие физики.
    if self:is_collision_imminent() then
        self:finish_episode(true)
        elm.log.info("Car is airborne, ending episode.")
        return
    end

    local rotation = self.entity.rotation
    local forward = rotation:rotate(Vector3.forward())
    local right = rotation:rotate(Vector3.right())
    local up = rotation:rotate(Vector3.up())

    local velocity = self.body.velocity


    local speed = self:next_speed(
        velocity:dot(forward), dt
    )

    local side = velocity:dot(right)
        * math.max(1 - self.grip * dt, 0)

    local vertical = velocity:dot(up)

    self.body.velocity =
        forward * speed
        + right * side
        + up * vertical

    -- Сохраняем исходную модель вращения.
    local spin = self.body.angular_velocity
    local tilt = spin - up * spin:dot(up)

    self.body.angular_velocity =
        tilt + up * self:yaw_rate(speed)

    -- Оцениваем фактически пройденное расстояние.
    local current_position = self.entity.world_position
    local distance =
        (current_position - self.previous_position).length

    self.agent:record(dt, distance)
    self.previous_position = current_position
end

-- Наблюдения агента: три луча в горизонтальной плоскости.
function Car:observe()
    local rotation = self.entity.rotation

    local forward =
        rotation:rotate(Vector3.forward())

    local right =
        rotation:rotate(Vector3.right())

    local left_direction =
        (forward - right * 0.7):normalized()

    local right_direction =
        (forward + right * 0.7):normalized()

    return {
        left = self:clearance(
            left_direction, self.sensor_distance
        ),

        front = self:clearance(
            forward, self.sensor_distance
        ),

        right = self:clearance(
            right_direction, self.sensor_distance
        ),
    }
end

-- 0 означает, что луч встретил препятствие;
-- 1 означает, что препятствий на его пути нет.
function Car:clearance(direction, distance)
    local hit = elm.physics.raycast(
        self.entity.world_position,
        direction,
        distance,
        self.entity
    )

    if hit ~= nil then
        return 0
    end

    return 1
end

function Car:is_collision_imminent()
    local rotation = self.entity.rotation
    local forward = rotation:rotate(Vector3.forward())
    local right = rotation:rotate(Vector3.right())

    local origin = self.entity.world_position
    local distance = self.collision_distance

    local directions = {
        forward,
        (forward + right * 0.7):normalized(),
        (forward - right * 0.7):normalized(),
    }

    for _, direction in ipairs(directions) do
        local hit = elm.physics.raycast(
            origin, direction, distance, self.entity
        )

        if hit ~= nil then
            return true
        end
    end

    return false
end

function Car:next_speed(speed, dt)
    local control = self.control

    if control.brake > 0 then
        return approach(
            speed,
            0,
            self.brake * control.brake * dt
        )
    end

    local throttle = control.throttle

    if throttle == 0 then
        return approach(speed, 0, self.coast * dt)
    end

    if speed * throttle < 0 then
        return approach(speed, 0, self.brake * dt)
    end

    local limit = throttle > 0
        and self.max_speed
        or self.max_reverse

    return approach(
        speed,
        throttle * limit,
        self.acceleration * dt
    )
end

function Car:yaw_rate(speed)
    local steer = self.control.steering

    local share = elm.math.clamp(
        speed / self.max_speed,
        -1,
        1
    )

    return steer * self.turn_rate * share
end

function Car:is_grounded()
    local down =
        -self.entity.rotation:rotate(Vector3.up())

    local hit = elm.physics.raycast(
        self.entity.world_position,
        down,
        self.ground_ray,
        self.entity
    )

    return hit ~= nil
end

-- Завершение эпизода и запуск следующего.
function Car:finish_episode(collision)
    local reward = self.agent:finish_episode(collision)

    print(
        "Episode:", self.agent.generation,
        "Reward:", reward,
        "Best:", self.agent.best_reward
    )

    self:respawn()
end

function Car:respawn()
    self.entity.position = self.spawn_position
    self.entity.rotation = self.spawn_rotation

    self.body.velocity = Vector3.zero()
    self.body.angular_velocity = Vector3.zero()

    self.control = {
        steering = 0,
        throttle = 0,
        brake = 0,
    }

    self.episode_time = 0
    self.previous_position = self.entity.world_position

    -- ВАЖНО: агента не пересоздаём.
    -- Его best, best_reward и generation сохраняются.
    self.agent:begin_episode()

    self:update_camera(0, true)
end

function Car:update_camera(dt, snap)
    local car_pos = self.entity.world_position

    local forward =
        self.entity.rotation:rotate(Vector3.forward())

    local heading = Vector3(forward.x, 0, forward.z)

    if heading.length > 0.001 then
        heading = heading:normalized()
    else
        heading = Vector3.forward()
    end

    local desired_pos =
        car_pos
        - heading * self.camera_distance
        + Vector3(0, self.camera_height, 0)

    local target_look =
        car_pos + Vector3(0, self.camera_look_height, 0)

    if snap or self.camera_smoothness <= 0 then
        camera.set_position(desired_pos)
    else
        local current_pos = camera.position()

        local t = clamp(
            1.0 - math.exp(-self.camera_smoothness * dt),
            0,
            1
        )

        camera.set_position(current_pos:lerp(desired_pos, t))
    end

    camera.look_at(target_look)
end

return Car
