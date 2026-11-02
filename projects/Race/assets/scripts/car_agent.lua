
local Agent = {}

local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(max_value, value))
end

local function random_range(min_value, max_value)
    return min_value + math.random() * (max_value - min_value)
end

local function copy_policy(policy)
    return {
        bias = policy.bias,
        steer_gain = policy.steer_gain,
        speed_gain = policy.speed_gain,
        brake_gain = policy.brake_gain,
    }
end

local function random_policy()
    return {
        bias = random_range(-0.2, 0.2),
        steer_gain = random_range(0.3, 1.0),
        speed_gain = random_range(0.5, 1.0),
        brake_gain = random_range(0.3, 1.0),
    }
end

function Agent.new()
    return setmetatable({
        generation = 0,

        best = random_policy(),
        best_reward = -math.huge,

        candidate = nil,
        episode_reward = 0,
        episode_time = 0,
        episode_distance = 0,

        mutation = 0.25,
        last_reward = 0,
    }, { __index = Agent })
end

function Agent:begin_episode()
    self.generation = self.generation + 1

    self.episode_reward = 0
    self.episode_time = 0
    self.episode_distance = 0

    -- Первый кандидат исследует случайную стратегию.
    if self.generation == 1 then
        self.candidate = copy_policy(self.best)
        return
    end

    -- Большая часть попыток исследует изменения лучшей стратегии.
    if math.random() < 0.8 then
        self.candidate = copy_policy(self.best)

        self.candidate.bias = clamp(
            self.candidate.bias
                + random_range(-self.mutation, self.mutation),
            -1, 1
        )

        self.candidate.steer_gain = clamp(
            self.candidate.steer_gain
                + random_range(-self.mutation, self.mutation),
            0.05, 2
        )

        self.candidate.speed_gain = clamp(
            self.candidate.speed_gain
                + random_range(-self.mutation, self.mutation),
            0.1, 1
        )

        self.candidate.brake_gain = clamp(
            self.candidate.brake_gain
                + random_range(-self.mutation, self.mutation),
            0.1, 1
        )
    else
        -- Иногда пробуем совершенно другую стратегию.
        self.candidate = random_policy()
    end
end

-- clearance: 0 = препятствие рядом, 1 = луч ничего не обнаружил.
function Agent:act(observation)
    local p = self.candidate or self.best

    local left = observation.left
    local front = observation.front
    local right = observation.right

    -- Если слева препятствие, а справа свободно,
    -- steering становится положительным, то есть вправо.
    local avoidance = right - left

    -- Если впереди стена, усиливаем попытку объехать её.
    if front < 0.5 then
        avoidance = avoidance * 1.5
    end

    local steering = clamp(
        p.bias + avoidance * p.steer_gain,
        -1, 1
    )

    -- При приближении к препятствию снижаем газ.
    local throttle_factor = clamp(
        (front - 0.1) * 2.5,
        0.15, 1
    )

    local throttle = clamp(
        throttle_factor * p.speed_gain,
        0, 1
    )

    local brake = 0

    if front < 0.2 then
        brake = p.brake_gain
        throttle = 0
    end

    return {
        steering = steering,
        throttle = throttle,
        brake = brake,
    }
end

function Agent:record(dt, distance)
    self.episode_time = self.episode_time + dt
    self.episode_distance = self.episode_distance + distance

    -- Небольшая награда за продвижение.
    self.episode_reward =
        self.episode_reward + distance * 0.1

    -- Небольшой штраф за затягивание попытки.
    self.episode_reward =
        self.episode_reward - dt * 0.01
end

function Agent:finish_episode(collision)
    local reward = self.episode_reward

    if collision then
        reward = reward - 10
    end

    -- Дополнительная награда за пройденное расстояние.
    reward = reward + self.episode_distance

    self.last_reward = reward

    if reward > self.best_reward then
        self.best_reward = reward
        self.best = copy_policy(self.candidate)

        -- Успешные изменения можно исследовать подробнее.
        self.mutation = math.max(0.05, self.mutation * 0.95)
    else
        -- Если улучшения нет, увеличиваем разнообразие поиска.
        self.mutation = math.min(0.5, self.mutation * 1.05)
    end

    self.candidate = nil

    return reward
end

return Agent
