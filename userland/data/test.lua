-- A tour of Lua features for the port self test; the output must be the
-- same on 68k_HAL and on a Linux host.

print(_VERSION, 1 + 1, 7 // 2, 7 % -3, 2 ^ 10, 10 / 4)
print(math.maxinteger, math.mininteger, math.pi)
print(string.format("%5.2f|%-5d|%x|%s|%q", 3.14159, 42, 255, "str", "a\nb"))
print(("hello world"):gsub("o", "0"))
print(("a,b,,c"):find(",,", 1, true))
print(#string.rep("ab", 100000))

-- tables and sorting
local t = {}
for i = 1, 10 do t[i] = (i * 7) % 11 end
table.sort(t)
print(table.concat(t, " "))
table.sort(t, function(a, b) return a > b end)
print(table.concat(t, " "))

-- closures, varargs, metatables
local function counter()
  local n = 0
  return function() n = n + 1; return n end
end
local c = counter()
c(); c()
print("counter", c())

local V = {}
V.__index = V
V.__add = function(a, b) return setmetatable({x = a.x + b.x}, V) end
V.__tostring = function(v) return "V(" .. v.x .. ")" end
print(tostring(setmetatable({x = 1}, V) + setmetatable({x = 2}, V)))
print(select("#", 1, 2, 3), select(2, "a", "b", "c"))

-- coroutines
local co = coroutine.wrap(function(a)
  local b = coroutine.yield(a + 1)
  local c2 = coroutine.yield(b * 2)
  return "done " .. c2
end)
print(co(1), co(10), co("x"))

-- errors
print(pcall(error, "boom"))
print(pcall(function() local x = nil; return x.y end))
print(select(2, pcall(string.rep, "x", -1)) == "")

-- big numbers and floats
print(2^53, 2^63, math.floor(2.5), math.ceil(2.5), math.sqrt(16), 1e15, 1e100)
print(string.format("%.14g", 0.1), 0.1 + 0.2 == 0.3, math.abs(-3), 5 // 0.0, -5 // 0.0)
print(tostring(1/0), tostring(-1/0), tostring(0/0) == tostring(0/0))
print(math.tointeger(3.0), 3 | 4, 7 & 2, 1 << 40, ~0)

-- the standard library that touches the system
print(type(os.time()), type(os.clock()), os.date("%Y", 0) == "1970")
print(os.getenv("NO_SUCH_VARIABLE"))

local f = assert(io.open("/tmp/lua_test.txt", "w"))
for i = 1, 3 do f:write("line ", i, "\n") end
f:close()
for line in io.lines("/tmp/lua_test.txt") do io.write("[", line, "]") end
print()
print(os.remove("/tmp/lua_test.txt"))
print(io.open("/tmp/lua_test.txt"))

-- garbage collection under pressure
local keep = {}
for i = 1, 20000 do keep[i % 100 + 1] = {i, tostring(i)} end
collectgarbage()
print(#keep, collectgarbage("count") > 0)
