-- Module loaded by tests/modules_test.lua: require must run it once.
loads = (loads or 0) + 1

return { loads = loads }
