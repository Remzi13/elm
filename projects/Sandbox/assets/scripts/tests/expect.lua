-- Assertions for the *_test.lua files (Engine --script-selftest).
local expect = {}

function expect.eq(actual, expected, message)
    if actual ~= expected then
        error(string.format("%sexpected %s, got %s", message and (message .. ": ") or "",
            tostring(expected), tostring(actual)), 2)
    end
end

function expect.near(actual, expected, epsilon)
    epsilon = epsilon or 1e-5
    if math.abs(actual - expected) > epsilon then
        error(string.format("expected %s (+-%s), got %s", expected, epsilon, actual), 2)
    end
end

--- Calls fn and checks that it fails with a message containing `pattern` (plain text).
function expect.error(fn, pattern)
    local ok, message = pcall(fn)
    if ok then
        error("expected an error" .. (pattern and (" containing '" .. pattern .. "'") or ""), 2)
    end
    message = tostring(message)
    if pattern and not message:find(pattern, 1, true) then
        error(string.format("expected an error containing '%s', got '%s'", pattern, message), 2)
    end
    return message
end

return expect
