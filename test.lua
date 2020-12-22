_G.test = { "test1", "test2", "test3", "a\tb\tc" }
test123 = { "test123" }
local test124 = 1

function _G.test.getweekstart_by_tz_test(time, tz)
    local tz_tmp = 259200 + tz * 36
    return math.floor((time + tz_tmp) / 604800) * 604800 - tz_tmp
end

function _G.getweekstart_by_tz(time, tz)
    local tz_tmp = 259200 + tz * 36
    return math.floor((time + tz_tmp) / 604800) * 604800 - tz_tmp
end

function string_time_to_unix_time(s)
    if not s or string.len(s) == 0 then
        return 0;
    end

    local tmp = { 1, 2, { 4, 5 } }

    local p = "(%d+)-(%d+)-(%d+) (%d+):(%d+):(%d+)";
    local year, month, day, hour, min, sec = s:match(p);
    if year and month and day and hour and min and sec then
        return os.time({ year = year, month = month, day = day, hour = hour, min = min, sec = sec });
    end
    return 0;
end

function string_time_to_unix_time_with_tz(s, tz)
    if not s or string.len(s) == 0 then
        return 0;
    end

    if test124 then
        test124 = test124 + 1
    end

    local p = "(%d+)-(%d+)-(%d+) (%d+):(%d+):(%d+)";
    local year, month, day, hour, min, sec = s:match(p);
    local time_sec = 0
    if year and month and day and hour and min and sec then
        time_sec = os.time({ year = year, month = month, day = day, hour = hour, min = min, sec = sec });
        return time_sec - (tz or 0) * 36
    end

    return 0
end

while true do

    local x = 0
    local f = function()
        x = x + 1
        return x
    end
    local xx = f()

    local begin_time1 = string_time_to_unix_time("2020-11-02 00:00:00")
    getweekstart_by_tz(begin_time1, 0)

    local begin_time2 = string_time_to_unix_time("2020-11-02 00:00:00")
    getweekstart_by_tz(begin_time2, -800)

    local begin_time3 = string_time_to_unix_time_with_tz("2020-11-02 00:00:00", 800)
    getweekstart_by_tz(begin_time3, -800)

    local begin_time4 = string_time_to_unix_time_with_tz("2020-11-02 00:00:00", -800)
    getweekstart_by_tz(begin_time4, 0)

    os.execute("usleep 100")

end
