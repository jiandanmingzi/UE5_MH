-- MHGZ Monster Hunter: World runtime recorder
-- Read-only LuaEngine script. F9 toggles capture; the optional ImGui panel has
-- the same control. Samples come from LuaEngine's on_time callback, not a
-- fixed-rate timer. The recorder never writes game memory.
--
-- LuaEngine exposes digital Xbox button checks. Optional raw analog axes are
-- read from the project's XInput companion bridge; without that process these
-- columns remain blank. The recorder never reads or writes game memory.

local SCRIPT_NAME = "MHGZ MHW Aerial Trajectory Recorder"
local SCRIPT_VERSION = "0.2.7"
-- Pin capture output to this installation so Steam's working directory does
-- not silently send CSV files somewhere else.
local GAME_LUA_DIRECTORY = "F:/games/Steam/steamapps/common/Monster Hunter World/Lua"
local OUTPUT_PREFIX = GAME_LUA_DIRECTORY .. "/MHGZ_MHW_AerialTrajectoryRecorder"
local DIAGNOSTIC_PATH = GAME_LUA_DIRECTORY .. "/MHGZ_MHW_AerialTrajectoryRecorder_diagnostics.log"
local XINPUT_LIVE_STATE_PATH = "D:/study/MH/MHGZ/Saved/AssetProbe/DescendingThrust/XInput/live_state.pipe"
local CAPTURE_KEY = "F9"
local FLUSH_EVERY_SAMPLES = 60
local DIAGNOSTIC_HEARTBEAT_SECONDS = 5
local AOB_REFRESH_MAX_ATTEMPTS_PER_CAPTURE = 2
local AOB_REFRESH_RETRY_DELAY_SECONDS = 5
local WORLD_UP_AXIS = "Y"
-- This is the same player-root fallback shipped by LuaEngine's Engine_player.lua.
-- Use it only when SearchPattern returns a root that fails live hunter probes.
local PLAYER_AOB_FALLBACK = 0x1450139A0

local BUTTONS = {
    { api = "L↑", name = "left_stick_up" },
    { api = "L→", name = "left_stick_right" },
    { api = "L↓", name = "left_stick_down" },
    { api = "L←", name = "left_stick_left" },
    { api = "LDown", name = "left_stick_click" },
    { api = "R↑", name = "right_stick_up" },
    { api = "R→", name = "right_stick_right" },
    { api = "R↓", name = "right_stick_down" },
    { api = "R←", name = "right_stick_left" },
    { api = "RDown", name = "right_stick_click" },
    { api = "LT", name = "lt" },
    { api = "RT", name = "rt" },
    { api = "LB", name = "lb" },
    { api = "RB", name = "rb" },
    { api = "↑", name = "dpad_up" },
    { api = "→", name = "dpad_right" },
    { api = "↓", name = "dpad_down" },
    { api = "←", name = "dpad_left" },
    { api = "Y", name = "y" },
    { api = "B", name = "b" },
    { api = "A", name = "a" },
    { api = "X", name = "x" },
    { api = "Window", name = "view" },
    { api = "Menu", name = "menu" },
}

local state = {
    player = nil, world = nil, source_error = nil,
    recording = false, pending_toggle = false, pending_toggle_reason = nil, previous_f9_down = false,
    previous_sample = nil, previous_buttons = {}, previous_action = nil,
    sample_count = 0, event_count = 0,
    samples_file = nil, events_file = nil,
    samples_path = nil, events_path = nil, run_serial = 0,
    last_game_time = nil, last_status = "Initializing LuaEngine data access",
    last_error = nil, waiting_for_player_reason = nil,
    snapshot_reject_reason = nil, snapshot_reject_detail = nil, rejected_sample_count = 0,
    unavailable_actor_sample_count = 0,
    missing_position_sample_count = 0, invalid_orientation_sample_count = 0,
    first_sample_probe_started = false, first_sample_row_recorded = false,
    diagnostic_last_heartbeat = 0, diagnostic_first_time_logged = false,
    diagnostic_last_key_error = nil,
    sampling_interval_count = 0, sampling_interval_sum = 0,
    sampling_interval_max = 0, sampling_gaps_over_25ms = 0,
    capture_start_game_time = nil,
    capture_start_clock = nil,
    aob_refresh_attempt_count = 0, aob_refresh_last_attempt_clock = nil,
    xinput_last_qpc_ns = nil, xinput_same_qpc_count = 0,
    xinput_session_id = nil,
}

-- Forward declaration is required because the AOB recovery helper is defined
-- before this function but calls it to rebuild LuaEngine's cached wrappers.
local resolve_sources

local function safe_read(fn)
    local ok, value = pcall(fn)
    if not ok then return nil end
    return value
end

local function safe_number(fn)
    local value = safe_read(fn)
    if type(value) ~= "number" or value ~= value or math.abs(value) > 1.0e30 then return nil end
    return value
end

local function number_cell(value)
    if type(value) ~= "number" or value ~= value then return "" end
    return string.format("%.7f", value)
end

local function bool_cell(value)
    if type(value) ~= "boolean" then return "" end
    return value and "1" or "0"
end

local function as_boolean(value)
    if type(value) == "boolean" then return value end
    if type(value) == "number" then return value ~= 0 end
    return nil
end

local function csv_cell(value)
    if value == nil then return "" end
    return '"' .. tostring(value):gsub('"', '""') .. '"'
end

local function write_cells(file, cells)
    if file == nil then return false end
    local encoded = {}
    for i, value in ipairs(cells) do encoded[i] = csv_cell(value) end
    local ok, result, err = pcall(function() return file:write(table.concat(encoded, ","), "\n") end)
    if not ok or result == nil then
        state.last_error = tostring(ok and err or result)
        return false
    end
    return true
end

local function diagnostic_write(event_name, detail)
    if type(io) ~= "table" or type(io.open) ~= "function" then return false end
    local timestamp = "unknown-time"
    if type(os) == "table" and type(os.date) == "function" then
        local ok, value = pcall(os.date, "%Y-%m-%d %H:%M:%S")
        if ok and value ~= nil then timestamp = tostring(value) end
    end
    local line = string.format("[%s] %s | %s\n", timestamp,
        tostring(event_name), tostring(detail or ""))
    local candidates = {
        DIAGNOSTIC_PATH,
        "Lua/MHGZ_MHW_AerialTrajectoryRecorder_diagnostics.log",
    }
    for _, path in ipairs(candidates) do
        local open_ok, file = pcall(io.open, path, "a")
        if open_ok and file ~= nil then
            local write_ok = pcall(function()
                local result, err = file:write(line)
                if result == nil then error(err or "diagnostic write failed") end
                local flushed, flush_error = file:flush()
                if flushed == nil then error(flush_error or "diagnostic flush failed") end
                file:close()
            end)
            if write_ok then return true end
            pcall(function() file:close() end)
        end
    end
    return false
end

local function report(message, native_feedback)
    state.last_status = message
    -- Generic diagnostics do not call game UI/logging bridges from on_time.
    -- Capture start/stop uses the separate, one-shot notify function below.
    if native_feedback == false then
        diagnostic_write("status", tostring(message) .. " | native_feedback=skipped_in_on_time")
        return
    end
    local message_ok, message_error = false, "Message API unavailable"
    if type(Message) == "function" then
        message_ok, message_error = pcall(Message, message)
    end

    -- Always mirror status into LuaEngine's logger. Previously a failing
    -- Message() call was swallowed and prevented any fallback output.
    local console_ok, console_error = false, "Console_Info API unavailable"
    if type(Console_Info) == "function" then
        console_ok, console_error = pcall(Console_Info, "[MHGZ Recorder] " .. tostring(message))
    end
    if not console_ok and type(print) == "function" then
        pcall(print, "[MHGZ Recorder] " .. tostring(message))
    end
    diagnostic_write("status", string.format("%s | Message=%s (%s) | Console_Info=%s (%s)",
        tostring(message), tostring(message_ok), tostring(message_error),
        tostring(console_ok), tostring(console_error)))
end

local function valid_pointer(value)
    return type(value) == "number" and value > 0x10000
end

local function address_text(value)
    if type(value) == "number" then return string.format("%.0f", value) end
    return tostring(value)
end

-- Read-only inspection of LuaEngine's shared AOB cache and its pointer chains.
-- The cached roots are process addresses; logging them helps distinguish a
-- stale AOB root from a valid root whose hunter object is not ready yet.
local function inspect_player_roots()
    local player_base = safe_number(function() return GlobalVariable_int("Engine_aob_Player") end)
    local world_base = safe_number(function() return GlobalVariable_int("Engine_aob_World") end)
    local save_base = safe_number(function() return GlobalVariable_int("Engine_aob_Save") end)
    local player_ptr = player_base and safe_read(function() return GetAddress(player_base, { 0x50 }) end) or nil
    local map_ptr = player_base and safe_read(function() return GetAddress(player_base, { 0x50, 0x7D20 }) end) or nil
    local world_ptr = world_base and safe_read(function()
        return GetAddress(world_base, { 0x90, 0x40, 0x90, 0x18 })
    end) or nil

    local q = {}
    if valid_pointer(player_ptr) then
        for i, offset in ipairs({ 0x170, 0x174, 0x178, 0x17C }) do
            q[i] = safe_number(function() return GetAddressData(player_ptr + offset, "float") end)
        end
    end
    local qnorm = 0
    for i = 1, 4 do
        local value = q[i]
        if type(value) == "number" then qnorm = qnorm + value * value end
    end
    local health_ptr = valid_pointer(player_ptr) and safe_read(function()
        return GetAddress(player_ptr, { 0x7630 })
    end) or nil
    local health_max = valid_pointer(health_ptr) and safe_number(function()
        return GetAddressData(health_ptr + 0x60, "float")
    end) or nil
    local map_id = valid_pointer(map_ptr) and safe_number(function()
        return GetAddressData(map_ptr + 0xB88, "int")
    end) or nil
    local usable = valid_pointer(player_ptr) and
        ((health_max ~= nil and health_max > 0) or qnorm > 0.25)

    return {
        player_base = player_base, world_base = world_base, save_base = save_base,
        player_ptr = player_ptr, map_ptr = map_ptr, world_ptr = world_ptr,
        qnorm = math.sqrt(qnorm), health_max = health_max, map_id = map_id,
        usable = usable == true,
    }
end

local function describe_player_roots(probe)
    return string.format(
        "player_aob=%s world_aob=%s save_aob=%s player_ptr=%s map_ptr=%s world_ptr=%s qnorm=%.6f health_max=%s map_id=%s usable=%s",
        address_text(probe.player_base), address_text(probe.world_base), address_text(probe.save_base),
        address_text(probe.player_ptr), address_text(probe.map_ptr), address_text(probe.world_ptr),
        probe.qnorm or 0, tostring(probe.health_max), tostring(probe.map_id), tostring(probe.usable))
end

local function refresh_aob_roots_once(reason)
    local now = os.time()
    if state.aob_refresh_attempt_count >= AOB_REFRESH_MAX_ATTEMPTS_PER_CAPTURE then
        diagnostic_write("aob_refresh_skipped", string.format(
            "attempt limit reached (%d per capture)", AOB_REFRESH_MAX_ATTEMPTS_PER_CAPTURE))
        return false
    end
    if state.aob_refresh_last_attempt_clock ~= nil and
        now - state.aob_refresh_last_attempt_clock < AOB_REFRESH_RETRY_DELAY_SECONDS then
        diagnostic_write("aob_refresh_skipped", "retry cooldown has not elapsed")
        return false
    end
    state.aob_refresh_attempt_count = state.aob_refresh_attempt_count + 1
    state.aob_refresh_last_attempt_clock = now
    if type(GlobalVariable_int) ~= "function" or type(setGlobalVariable_int) ~= "function" then
        diagnostic_write("aob_refresh_unavailable", "LuaEngine global-variable APIs unavailable")
        return false
    end

    local before = inspect_player_roots()
    diagnostic_write("aob_refresh_begin", "reason=" .. tostring(reason or "unspecified") ..
        " attempt=" .. tostring(state.aob_refresh_attempt_count) .. " | " .. describe_player_roots(before))
    local old_player = before.player_base or 0
    local transaction_ok, rebuilt, after, recovery_source = pcall(function()
        local invalidated = pcall(setGlobalVariable_int, "Engine_aob_Player", 0)
        if not invalidated then return false, nil, "cache_invalidate_failed" end

        state.player, state.world = nil, nil
        local sources_rebuilt = resolve_sources(true)
        local probe = inspect_player_roots()
        if sources_rebuilt and probe.usable then
            return true, probe, "pattern_scan"
        end

        -- LuaEngine's short wildcard signature can resolve to a nearby false
        -- match. Its own Engine_player.lua carries this fallback address; try it
        -- only after the scanned root fails the pointer/quaternion/health probe.
        diagnostic_write("aob_refresh_candidate_rejected", "source=pattern_scan | " ..
            describe_player_roots(probe))
        local fallback_set = pcall(setGlobalVariable_int, "Engine_aob_Player", PLAYER_AOB_FALLBACK)
        if not fallback_set then return false, probe, "fallback_cache_write_failed" end

        state.player, state.world = nil, nil
        sources_rebuilt = resolve_sources(true)
        local fallback_probe = inspect_player_roots()
        return sources_rebuilt and fallback_probe.usable, fallback_probe, "module_fallback"
    end)
    if transaction_ok and rebuilt and after ~= nil then
        diagnostic_write("aob_refresh_result", "rebuilt=true source=" ..
            tostring(recovery_source) .. " | " .. describe_player_roots(after))
        diagnostic_write("aob_refresh_accepted", "validated player root resolves to live hunter data")
        return true
    end

    -- Restore the shared player root even if rebuilding the wrappers raises.
    -- The world root is not invalidated or modified by this transaction.
    pcall(setGlobalVariable_int, "Engine_aob_Player", old_player)
    state.player, state.world = nil, nil
    local restore_ok, restore_result = pcall(function() return resolve_sources(true) end)
    local detail
    if not transaction_ok then
        detail = "rescan raised: " .. tostring(rebuilt)
    elseif after ~= nil then
        detail = "pattern scan and LuaEngine fallback did not prove live hunter data; source=" ..
            tostring(recovery_source) .. " | " .. describe_player_roots(after)
    else
        detail = "could not invalidate both shared AOB roots"
    end
    diagnostic_write("aob_refresh_rolled_back", detail ..
        " | wrappers_restored=" .. tostring(restore_ok and restore_result == true))
    return false
end

local function notify_capture_status(message)
    state.last_status = message
    diagnostic_write("native_prompt_begin", tostring(message))
    if type(Message) ~= "function" then
        diagnostic_write("native_prompt_unavailable", "Message API is not registered")
        return false
    end
    local ok, err = pcall(Message, tostring(message))
    diagnostic_write("native_prompt_end", "ok=" .. tostring(ok) .. (ok and "" or " error=" .. tostring(err)))
    return ok
end

resolve_sources = function(force_refresh)
    if not force_refresh and state.player ~= nil and state.world ~= nil then return true end
    if type(engine) ~= "table" or type(engine.Player) ~= "table" or type(engine.World) ~= "table" then
        state.source_error = "LuaEngine engine.Player/engine.World unavailable; install LuaEngine first"
        return false
    end
    local ok, player_or_error = pcall(function() return engine.Player:new() end)
    if not ok or player_or_error == nil then
        state.source_error = "engine.Player creation failed: " .. tostring(player_or_error)
        return false
    end
    local world_ok, world_or_error = pcall(function() return engine.World:new() end)
    if not world_ok or world_or_error == nil then
        state.source_error = "engine.World creation failed: " .. tostring(world_or_error)
        return false
    end
    state.player, state.world, state.source_error = player_or_error, world_or_error, nil
    return true
end

local function player_snapshot()
    if not resolve_sources() then return nil end
    local p, w = state.player, state.world
    local s = {
        map_id = safe_number(function() return w.MapId end),
        game_time = safe_number(function() return w.Time end),
        x = safe_number(function() return p.Position.position.x end),
        y = safe_number(function() return p.Position.position.y end),
        z = safe_number(function() return p.Position.position.z end),
        dx_internal = safe_number(function() return p.Position.incremental.x end),
        dy_internal = safe_number(function() return p.Position.incremental.y end),
        dz_internal = safe_number(function() return p.Position.incremental.z end),
        qw = safe_number(function() return p.Angle.Quaternion.w end),
        qx = safe_number(function() return p.Angle.Quaternion.x end),
        qy = safe_number(function() return p.Angle.Quaternion.y end),
        qz = safe_number(function() return p.Angle.Quaternion.z end),
        euler_x = safe_number(function() return p.Angle.Eulerian.x end),
        euler_y = safe_number(function() return p.Angle.Eulerian.y end),
        euler_z = safe_number(function() return p.Angle.Eulerian.z end),
        lmt_id = safe_number(function() return p.Action.lmtID end),
        fsm_id = safe_number(function() return p.Action.fsm.fsmID end),
        fsm_target = safe_number(function() return p.Action.fsm.fsmTarget end),
        use_item = safe_number(function() return p.Action.useItem end),
        frame = safe_number(function() return p.Frame.frame end),
        frame_end = safe_number(function() return p.Frame.frameEnd end),
        frame_speed = safe_number(function() return p.Frame.frameSpeed end),
        frame_speed_multiplier = safe_number(function() return p.Frame.frameSpeedMultiplies end),
        health = safe_number(function() return p.Characteristic.health.health_current end),
        health_max = safe_number(function() return p.Characteristic.health.health_max end),
        stamina = safe_number(function() return p.Characteristic.stamina.stamina_current end),
        stamina_max = safe_number(function() return p.Characteristic.stamina.stamina_max end),
        gravity = safe_number(function() return p.Gravity.gravity end),
        fall_speed = safe_number(function() return p.Gravity.fall end),
        airborne = safe_read(function() return p.Gravity.liftoff end),
        aiming = safe_read(function() return p.Collimator.aimingState end),
        aim_x = safe_number(function() return p.Collimator.straightPos.x end),
        aim_y = safe_number(function() return p.Collimator.straightPos.y end),
        aim_z = safe_number(function() return p.Collimator.straightPos.z end),
        aim_parabola_x = safe_number(function() return p.Collimator.parabolaPos.x end),
        aim_parabola_y = safe_number(function() return p.Collimator.parabolaPos.y end),
        aim_parabola_z = safe_number(function() return p.Collimator.parabolaPos.z end),
        weapon_x = safe_number(function() return p.Weapon.position.x end),
        weapon_y = safe_number(function() return p.Weapon.position.y end),
        weapon_z = safe_number(function() return p.Weapon.position.z end),
        weapon_type = safe_number(function() return p.Weapon.type end),
        weapon_id = safe_number(function() return p.Weapon.id end),
        weapon_hit = safe_number(function() return p.Weapon.hit end),
    }

    -- A missing rotation must not discard otherwise useful movement/input data.
    -- LuaEngine can return zero-filled structures before the hunter is ready,
    -- so use independent live hunter-state fields as the readiness signal.
    local qnorm = 0
    for _, key in ipairs({ "qw", "qx", "qy", "qz" }) do
        local value = s[key]
        if type(value) == "number" then qnorm = qnorm + value * value end
    end
    local position_valid = s.x ~= nil and s.y ~= nil and s.z ~= nil
    local hunter_state_ready =
        (s.health_max ~= nil and s.health_max > 0) or
        (s.health ~= nil and s.health > 0) or
        (s.lmt_id ~= nil and s.lmt_id ~= 0) or
        (s.frame_end ~= nil and s.frame_end > 0)
    if not hunter_state_ready then
        state.snapshot_reject_reason = "hunter_state_not_ready"
        state.snapshot_reject_detail = string.format(
            "position_valid=%s quaternion_norm=%.6f health=%s health_max=%s lmt_id=%s frame_end=%s map_id=%s game_time=%s",
            tostring(position_valid), math.sqrt(qnorm), tostring(s.health), tostring(s.health_max),
            tostring(s.lmt_id), tostring(s.frame_end), tostring(s.map_id), tostring(s.game_time))
        -- Preserve a sample row for input/camera telemetry even if LuaEngine
        -- currently exposes only its zero-filled no-hunter structures.
        for _, key in ipairs({
            "map_id", "game_time", "x", "y", "z", "dx_internal", "dy_internal", "dz_internal",
            "qw", "qx", "qy", "qz", "euler_x", "euler_y", "euler_z",
            "lmt_id", "fsm_id", "fsm_target", "use_item", "frame", "frame_end", "frame_speed",
            "frame_speed_multiplier", "health", "health_max", "stamina", "stamina_max",
            "gravity", "fall_speed", "airborne", "aiming", "aim_x", "aim_y", "aim_z",
            "aim_parabola_x", "aim_parabola_y", "aim_parabola_z", "weapon_x", "weapon_y",
            "weapon_z", "weapon_type", "weapon_id", "weapon_hit",
        }) do
            s[key] = nil
        end
        s._actor_state_ready = false
        s._position_valid = false
        s._orientation_valid = false
        return s
    end
    state.snapshot_reject_reason = nil
    state.snapshot_reject_detail = string.format(
        "position_valid=%s quaternion_valid=%s health=%s health_max=%s lmt_id=%s frame_end=%s",
        tostring(position_valid), tostring(qnorm >= 0.25), tostring(s.health), tostring(s.health_max),
        tostring(s.lmt_id), tostring(s.frame_end))
    s._position_valid = position_valid
    s._orientation_valid = qnorm >= 0.25
    s._actor_state_ready = true
    return s
end

local function read_camera()
    if type(GetCameraData) ~= "function" then return nil, nil, nil, nil end
    local ok, x, y, z, locked = pcall(GetCameraData)
    if not ok or type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then
        return nil, nil, nil, nil
    end
    return x, y, z, locked
end

local function read_buttons()
    local result = {}
    local api_available = type(engine) == "table" and type(engine.keypad) == "function"
    for _, button in ipairs(BUTTONS) do
        local ok, down = false, nil
        if api_available then ok, down = pcall(engine.keypad, button.api, true) end
        if ok then down = as_boolean(down) else down = nil end
        if ok and down ~= nil then result[button.name] = down
        else result[button.name] = nil end
        if not ok then api_available = false end
    end
    return result, api_available
end

local atan2 = math.atan2 or function(y, x)
    if x > 0 then return math.atan(y / x) end
    if x < 0 and y >= 0 then return math.atan(y / x) + math.pi end
    if x < 0 and y < 0 then return math.atan(y / x) - math.pi end
    if x == 0 and y > 0 then return math.pi / 2 end
    if x == 0 and y < 0 then return -math.pi / 2 end
    return 0
end

local function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end

local function signed_angle_delta(current, previous)
    if type(current) ~= "number" or type(previous) ~= "number" then return nil end
    return (current - previous + 180) % 360 - 180
end

-- MHW uses RE Engine's Y-up world. This derived heading assumes local +Z is
-- character forward; raw quaternion columns remain available for re-derivation.
local function facing_direction(s)
    local qw, qx, qy, qz = s.qw, s.qx, s.qy, s.qz
    if type(qw) ~= "number" or type(qx) ~= "number" or type(qy) ~= "number" or type(qz) ~= "number" then
        return nil
    end
    local norm = math.sqrt(qw * qw + qx * qx + qy * qy + qz * qz)
    if norm < 0.5 then return nil end
    qw, qx, qy, qz = qw / norm, qx / norm, qy / norm, qz / norm
    local x = 2 * (qx * qz + qw * qy)
    local y = 2 * (qy * qz - qw * qx)
    local z = 1 - 2 * (qx * qx + qy * qy)
    local length = math.sqrt(x * x + y * y + z * z)
    if length < 0.5 then return nil end
    x, y, z = x / length, y / length, z / length
    return {
        x = x, y = y, z = z,
        yaw = math.deg(atan2(x, z)),
        pitch = math.deg(math.asin(clamp(y, -1, 1))),
    }
end

-- The game API exposes the collimator world point, not camera rotation. This
-- vector is a useful aim-direction proxy only while aiming and when the point
-- is distinct from the player origin.
local function collimator_direction(s)
    if s.aiming ~= true or type(s.aim_x) ~= "number" or type(s.aim_y) ~= "number" or type(s.aim_z) ~= "number" then
        return nil
    end
    local x, y, z = s.aim_x - s.x, s.aim_y - s.y, s.aim_z - s.z
    local length = math.sqrt(x * x + y * y + z * z)
    if length < 1 then return nil end
    x, y, z = x / length, y / length, z / length
    return {
        x = x, y = y, z = z,
        yaw = math.deg(atan2(x, z)),
        pitch = math.deg(math.asin(clamp(y, -1, 1))),
    }
end

local function digital_stick_xy(buttons, up_name, right_name, down_name, left_name)
    local function axis(positive, negative)
        if buttons[positive] == nil or buttons[negative] == nil then return nil end
        return (buttons[positive] and 1 or 0) - (buttons[negative] and 1 or 0)
    end
    local x = axis(right_name, left_name)
    local y = axis(up_name, down_name)
    local magnitude = nil
    if x ~= nil and y ~= nil then magnitude = math.sqrt(x * x + y * y) end
    return x, y, magnitude
end

local function read_xinput_live_state()
    if type(io) ~= "table" or type(io.open) ~= "function" then return nil end
    local ok, file = pcall(io.open, XINPUT_LIVE_STATE_PATH, "r")
    if not ok or file == nil then return nil end
    local line_ok, line = pcall(function() return file:read("*l") end)
    pcall(function() file:close() end)
    if not line_ok or type(line) ~= "string" then return nil end

    local fields = {}
    for field in line:gmatch("([^|]+)") do table.insert(fields, field) end
    if #fields ~= 20 or fields[1] ~= "MHGZ_XINPUT_V1" then return nil end
    local connected = fields[7] == "1"
    local qpc_ns, utc_ns = fields[4], fields[5]
    if not qpc_ns:match("^%d+$") or not utc_ns:match("^%d+$") then return nil end

    if state.xinput_session_id ~= fields[3] then
        state.xinput_session_id = fields[3]
        state.xinput_last_qpc_ns = nil
        state.xinput_same_qpc_count = 0
        state.xinput_qpc_advanced = false
    end
    if state.xinput_last_qpc_ns == qpc_ns then
        state.xinput_same_qpc_count = state.xinput_same_qpc_count + 1
    else
        if state.xinput_last_qpc_ns ~= nil then
            state.xinput_qpc_advanced = true
        end
        state.xinput_last_qpc_ns = qpc_ns
        state.xinput_same_qpc_count = 0
    end

    local live = {
        session_id = fields[3], qpc_ns = qpc_ns, utc_ns = utc_ns,
        controller_index = tonumber(fields[6]), connected = connected,
        packet_number = tonumber(fields[8]), button_mask = tonumber(fields[9]),
        left_trigger_raw = tonumber(fields[10]), right_trigger_raw = tonumber(fields[11]),
        lx_raw = tonumber(fields[12]), ly_raw = tonumber(fields[13]),
        rx_raw = tonumber(fields[14]), ry_raw = tonumber(fields[15]),
        lx = tonumber(fields[16]), ly = tonumber(fields[17]),
        rx = tonumber(fields[18]), ry = tonumber(fields[19]),
        poll_duration_ns = tonumber(fields[20]),
    }
    live.supported = connected and live.lx ~= nil and live.ly ~= nil and
        live.rx ~= nil and live.ry ~= nil and live.poll_duration_ns ~= nil and
        state.xinput_qpc_advanced == true and state.xinput_same_qpc_count <= 3
    return live
end

local function sample_header()
    local h = {
        "sample_index", "map_id", "quest_time_s", "delta_time_s",
        "position_x", "position_y", "position_z", "delta_x", "delta_y", "delta_z",
        "velocity_x_per_game_second", "velocity_y_per_game_second", "velocity_z_per_game_second",
        "internal_increment_x", "internal_increment_y", "internal_increment_z",
        "facing_qw", "facing_qx", "facing_qy", "facing_qz", "euler_x_rad", "euler_y_rad", "euler_z_rad",
        "action_lmt_id", "action_fsm_id", "action_fsm_target", "action_use_item",
        "action_frame", "action_frame_end", "action_frame_speed", "action_frame_speed_multiplier",
        "health", "health_max", "stamina", "stamina_max", "gravity", "fall_speed", "airborne",
        "aiming", "aim_x", "aim_y", "aim_z", "aim_parabola_x", "aim_parabola_y", "aim_parabola_z",
        "weapon_x", "weapon_y", "weapon_z", "weapon_type", "weapon_id", "weapon_hit",
        "camera_x", "camera_y", "camera_z", "camera_locked", "input_api_available", "input_axes_supported",
        "input_axis_l_x", "input_axis_l_y", "input_axis_r_x", "input_axis_r_y",
    }
    for _, button in ipairs(BUTTONS) do
        table.insert(h, "input_" .. button.name .. "_down")
        table.insert(h, "input_" .. button.name .. "_pressed")
        table.insert(h, "input_" .. button.name .. "_released")
    end
    for _, name in ipairs({
        "sample_elapsed_game_s", "facing_forward_x", "facing_forward_y", "facing_forward_z",
        "facing_yaw_yup_local_z_deg", "facing_pitch_yup_local_z_deg",
        "facing_yaw_delta_deg", "facing_yaw_rate_deg_s", "facing_pitch_delta_deg", "facing_pitch_rate_deg_s",
        "collimator_dir_x", "collimator_dir_y", "collimator_dir_z",
        "collimator_yaw_deg", "collimator_pitch_deg", "collimator_relative_yaw_deg",
        "input_l_stick_digital_x", "input_l_stick_digital_y", "input_l_stick_digital_magnitude",
        "input_r_stick_digital_x", "input_r_stick_digital_y", "input_r_stick_digital_magnitude",
        "camera_rotation_supported", "hunter_state_available",
        "input_axis_qpc_ns", "input_axis_utc_ns", "input_axis_controller_index",
        "input_axis_connected", "input_axis_packet_number", "input_axis_button_mask",
        "input_axis_left_trigger_raw", "input_axis_right_trigger_raw", "input_axis_session_id",
        "input_axis_poll_duration_ns",
    }) do
        table.insert(h, name)
    end
    return h
end

local function write_event(event_type, name, old_value, new_value)
    if not state.recording or state.events_file == nil then return end
    state.event_count = state.event_count + 1
    write_cells(state.events_file, {
        number_cell(state.event_count), number_cell(state.sample_count), number_cell(state.last_game_time),
        event_type or "", name or "", old_value, new_value,
    })
end

local function close_files()
    for _, key in ipairs({ "samples_file", "events_file" }) do
        local file = state[key]
        if file ~= nil then pcall(function() file:flush(); file:close() end); state[key] = nil end
    end
end

local function stop_capture(reason)
    if not state.recording then return end
    write_event("capture_stopped", reason or CAPTURE_KEY, nil, nil)
    close_files()
    state.recording, state.previous_sample, state.previous_action = false, nil, nil
    state.previous_buttons = {}
    local average_hz = state.sampling_interval_sum > 0 and
        state.sampling_interval_count / state.sampling_interval_sum or 0
    diagnostic_write("capture_stopped", string.format(
        "reason=%s samples=%d valid_actor_samples=%d rejected_samples=%d unavailable_actor_samples=%d missing_position_samples=%d invalid_orientation_samples=%d events=%d avg_hz=%.2f max_gap_s=%.6f gaps_over_25ms=%d samples_path=%s events_path=%s",
        tostring(reason or CAPTURE_KEY), state.sample_count,
        math.max(0, state.sample_count - state.unavailable_actor_sample_count), state.rejected_sample_count,
        state.unavailable_actor_sample_count, state.missing_position_sample_count,
        state.invalid_orientation_sample_count,
        state.event_count, average_hz, state.sampling_interval_max,
        state.sampling_gaps_over_25ms,
        tostring(state.samples_path), tostring(state.events_path)))
    local valid_actor_samples = math.max(0, state.sample_count - state.unavailable_actor_sample_count)
    if state.sample_count > 0 and valid_actor_samples == 0 then
        notify_capture_status(string.format(
            "已记录 %d 行输入数据，但猎人/动作数据为 0 行；本次不能用于动作分析",
            state.sample_count))
    elseif valid_actor_samples < state.sample_count then
        notify_capture_status(string.format("录制停止：%d 行；角色数据 %d 行；平均 %.1f Hz",
            state.sample_count, valid_actor_samples, average_hz))
    else
        notify_capture_status(string.format("录制已停止：%d 行；平均 %.1f Hz", state.sample_count, average_hz))
    end
end

local function start_capture(reason)
    if state.recording then return end
    state.capture_start_clock = os.time()
    state.aob_refresh_attempt_count = 0
    state.aob_refresh_last_attempt_clock = nil
    diagnostic_write("capture_start_requested", "reason=" .. tostring(reason or CAPTURE_KEY))
    if not resolve_sources(true) then
        diagnostic_write("capture_start_failed", tostring(state.source_error or "LuaEngine data access unavailable"))
        report(state.source_error or "LuaEngine data access unavailable", false)
        return
    end
    local root_probe = inspect_player_roots()
    diagnostic_write("player_root_probe", describe_player_roots(root_probe))
    local refresh_roots = not root_probe.usable
    state.run_serial = state.run_serial + 1
    local stamp = os.date("%Y%m%d_%H%M%S") .. string.format("_%02d", state.run_serial)
    state.samples_path = OUTPUT_PREFIX .. "_samples_" .. stamp .. ".csv"
    state.events_path = OUTPUT_PREFIX .. "_events_" .. stamp .. ".csv"
    local samples, samples_error = io.open(state.samples_path, "w")
    if samples == nil then
        diagnostic_write("capture_start_failed", "samples open failed: " .. tostring(samples_error) ..
            " | path=" .. tostring(state.samples_path))
        report("无法创建样本 CSV：" .. tostring(samples_error) .. "；请确认 Lua 文件夹可写", false)
        return
    end
    local events, events_error = io.open(state.events_path, "w")
    if events == nil then
        pcall(function() samples:close() end)
        diagnostic_write("capture_start_failed", "events open failed: " .. tostring(events_error) ..
            " | path=" .. tostring(state.events_path))
        report("无法创建事件 CSV：" .. tostring(events_error) .. "；请确认 Lua 文件夹可写", false)
        return
    end

    state.samples_file, state.events_file = samples, events
    state.sample_count, state.event_count = 0, 0
    state.sampling_interval_count, state.sampling_interval_sum = 0, 0
    state.sampling_interval_max, state.sampling_gaps_over_25ms = 0, 0
    state.capture_start_game_time = nil
    state.previous_sample, state.previous_action, state.previous_buttons = nil, nil, {}
    state.last_game_time, state.waiting_for_player_reason = nil, nil
    state.snapshot_reject_reason, state.snapshot_reject_detail = nil, nil
    state.rejected_sample_count = 0
    state.unavailable_actor_sample_count = 0
    state.missing_position_sample_count, state.invalid_orientation_sample_count = 0, 0
    state.first_sample_probe_started, state.first_sample_row_recorded = false, false
    state.recording = true
    write_cells(samples, sample_header())
    write_cells(events, { "event_index", "sample_index", "quest_time_s", "event_type", "name", "old_value", "new_value" })
    write_event("capture_started", reason or CAPTURE_KEY, nil, nil)
    -- Make the header/start marker survive a later crash or forced shutdown.
    pcall(function() samples:flush(); events:flush() end)
    diagnostic_write("capture_started", string.format("reason=%s samples_path=%s events_path=%s",
        tostring(reason or CAPTURE_KEY), tostring(state.samples_path), tostring(state.events_path)))
    notify_capture_status("录制中：按 F9 停止并保存")
    -- Defer the initial bounded AOB rescan until after the capture files are open,
    -- flushed, and the user has received the start prompt. If a native scan
    -- stalls, the diagnostic trail still proves that capture was armed.
    if refresh_roots then refresh_aob_roots_once("capture_start") end
end

local function toggle_capture(reason)
    if state.recording then stop_capture(reason) else start_capture(reason) end
end

local function collect_sample()
    local probe_first_attempt = not state.first_sample_probe_started
    if probe_first_attempt then
        diagnostic_write("first_sample_begin", "entering player_snapshot")
        state.first_sample_probe_started = true
    end
    local s = player_snapshot()
    local hunter_data_unavailable = s == nil or not s._actor_state_ready
    local capture_age = state.capture_start_clock ~= nil and
        (os.time() - state.capture_start_clock) or 0
    if hunter_data_unavailable and capture_age >= AOB_REFRESH_RETRY_DELAY_SECONDS and
        state.aob_refresh_attempt_count < AOB_REFRESH_MAX_ATTEMPTS_PER_CAPTURE then
        local probe_ok, root_probe = pcall(inspect_player_roots)
        if probe_ok and not root_probe.usable then
            local refresh_ok, refreshed = pcall(refresh_aob_roots_once, "delayed_player_data_retry")
            diagnostic_write("aob_refresh_retry", "call_ok=" .. tostring(refresh_ok) ..
                " refreshed=" .. tostring(refresh_ok and refreshed == true))
            if refresh_ok and refreshed then s = player_snapshot() end
        end
    end
    if s == nil then
        state.rejected_sample_count = state.rejected_sample_count + 1
        local reason = state.snapshot_reject_reason or "snapshot_unavailable"
        if probe_first_attempt then
            diagnostic_write("first_sample_rejected", state.snapshot_reject_detail or reason)
        end
        if state.waiting_for_player_reason ~= reason then
            local detail = state.snapshot_reject_detail or reason
            write_event("waiting_for_player", detail, state.waiting_for_player_reason, reason)
            diagnostic_write("sample_rejected", detail)
            state.waiting_for_player_reason = reason
        end
        return
    end
    if not s._actor_state_ready then
        state.unavailable_actor_sample_count = state.unavailable_actor_sample_count + 1
        local reason = state.snapshot_reject_reason or "hunter_state_not_ready"
        if probe_first_attempt then
            diagnostic_write("first_sample_data_unavailable", state.snapshot_reject_detail or reason)
        end
        if state.waiting_for_player_reason ~= reason then
            local detail = state.snapshot_reject_detail or reason
            write_event("player_data_unavailable", detail, state.waiting_for_player_reason, reason)
            diagnostic_write("player_data_unavailable", detail)
            state.waiting_for_player_reason = reason
        end
    elseif state.waiting_for_player_reason ~= nil then
        write_event("player_data_ready", state.snapshot_reject_detail or "hunter_state_ready",
            state.waiting_for_player_reason, "ready")
        diagnostic_write("player_data_ready", state.snapshot_reject_detail or "hunter_state_ready")
        state.waiting_for_player_reason = nil
    end
    if not s._position_valid then state.missing_position_sample_count = state.missing_position_sample_count + 1 end
    if not s._orientation_valid then state.invalid_orientation_sample_count = state.invalid_orientation_sample_count + 1 end
    if state.capture_start_game_time == nil then state.capture_start_game_time = s.game_time end

    local previous = state.previous_sample
    local dt, dx, dy, dz, vx, vy, vz
    local clock_reset = false
    if previous ~= nil then
        local current_position_valid = s.x ~= nil and s.y ~= nil and s.z ~= nil
        local previous_position_valid = previous.x ~= nil and previous.y ~= nil and previous.z ~= nil
        if current_position_valid and previous_position_valid then
            dx, dy, dz = s.x - previous.x, s.y - previous.y, s.z - previous.z
        end
        if s.game_time ~= nil and previous.game_time ~= nil then
            dt = s.game_time - previous.game_time
            if dt < 0 then clock_reset = true
            elseif dt > 0.000001 then
                state.sampling_interval_count = state.sampling_interval_count + 1
                state.sampling_interval_sum = state.sampling_interval_sum + dt
                state.sampling_interval_max = math.max(state.sampling_interval_max, dt)
                if dt > 0.025 then state.sampling_gaps_over_25ms = state.sampling_gaps_over_25ms + 1 end
                if dx ~= nil and dy ~= nil and dz ~= nil then
                    vx, vy, vz = dx / dt, dy / dt, dz / dt
                end
            end
        end
    end

    local buttons, input_api_available = read_buttons()
    local xinput = read_xinput_live_state()
    local camera_x, camera_y, camera_z, camera_locked = read_camera()
    local facing = facing_direction(s)
    local collimator = collimator_direction(s)
    local previous_facing = previous and facing_direction(previous) or nil
    local yaw_delta = facing and previous_facing and signed_angle_delta(facing.yaw, previous_facing.yaw) or nil
    local pitch_delta = facing and previous_facing and (facing.pitch - previous_facing.pitch) or nil
    local yaw_rate = yaw_delta ~= nil and dt ~= nil and dt > 0.000001 and yaw_delta / dt or nil
    local pitch_rate = pitch_delta ~= nil and dt ~= nil and dt > 0.000001 and pitch_delta / dt or nil
    local relative_aim_yaw = collimator and facing and signed_angle_delta(collimator.yaw, facing.yaw) or nil
    local left_x, left_y, left_magnitude = digital_stick_xy(buttons,
        "left_stick_up", "left_stick_right", "left_stick_down", "left_stick_left")
    local right_x, right_y, right_magnitude = digital_stick_xy(buttons,
        "right_stick_up", "right_stick_right", "right_stick_down", "right_stick_left")
    state.sample_count = state.sample_count + 1
    local row = {
        number_cell(state.sample_count), number_cell(s.map_id), number_cell(s.game_time), number_cell(dt),
        number_cell(s.x), number_cell(s.y), number_cell(s.z), number_cell(dx), number_cell(dy), number_cell(dz),
        number_cell(vx), number_cell(vy), number_cell(vz),
        number_cell(s.dx_internal), number_cell(s.dy_internal), number_cell(s.dz_internal),
        number_cell(s.qw), number_cell(s.qx), number_cell(s.qy), number_cell(s.qz),
        number_cell(s.euler_x), number_cell(s.euler_y), number_cell(s.euler_z),
        number_cell(s.lmt_id), number_cell(s.fsm_id), number_cell(s.fsm_target), number_cell(s.use_item),
        number_cell(s.frame), number_cell(s.frame_end), number_cell(s.frame_speed), number_cell(s.frame_speed_multiplier),
        number_cell(s.health), number_cell(s.health_max), number_cell(s.stamina), number_cell(s.stamina_max),
        number_cell(s.gravity), number_cell(s.fall_speed), bool_cell(s.airborne),
        bool_cell(s.aiming), number_cell(s.aim_x), number_cell(s.aim_y), number_cell(s.aim_z),
        number_cell(s.aim_parabola_x), number_cell(s.aim_parabola_y), number_cell(s.aim_parabola_z),
        number_cell(s.weapon_x), number_cell(s.weapon_y), number_cell(s.weapon_z),
        number_cell(s.weapon_type), number_cell(s.weapon_id), number_cell(s.weapon_hit),
        number_cell(camera_x), number_cell(camera_y), number_cell(camera_z), bool_cell(camera_locked),
        input_api_available and "1" or "0",
        xinput and xinput.supported and "1" or "0",
        number_cell(xinput and xinput.supported and xinput.lx or nil),
        number_cell(xinput and xinput.supported and xinput.ly or nil),
        number_cell(xinput and xinput.supported and xinput.rx or nil),
        number_cell(xinput and xinput.supported and xinput.ry or nil),
    }

    local input_events = {}
    for _, button in ipairs(BUTTONS) do
        local down, was_down = buttons[button.name], state.previous_buttons[button.name]
        local pressed = down == true and was_down ~= true
        local released = down == false and was_down == true
        table.insert(row, bool_cell(down))
        table.insert(row, down == nil and "" or (pressed and "1" or "0"))
        table.insert(row, down == nil and "" or (released and "1" or "0"))
        if pressed then table.insert(input_events, { "input_pressed", button.name, "0", "1" }) end
        if released then table.insert(input_events, { "input_released", button.name, "1", "0" }) end
        if down ~= nil then state.previous_buttons[button.name] = down end
    end

    for _, value in ipairs({
        number_cell(s.game_time ~= nil and state.capture_start_game_time ~= nil and
            (s.game_time - state.capture_start_game_time) or nil),
        number_cell(facing and facing.x), number_cell(facing and facing.y), number_cell(facing and facing.z),
        number_cell(facing and facing.yaw), number_cell(facing and facing.pitch),
        number_cell(yaw_delta), number_cell(yaw_rate), number_cell(pitch_delta), number_cell(pitch_rate),
        number_cell(collimator and collimator.x), number_cell(collimator and collimator.y), number_cell(collimator and collimator.z),
        number_cell(collimator and collimator.yaw), number_cell(collimator and collimator.pitch), number_cell(relative_aim_yaw),
        number_cell(left_x), number_cell(left_y), number_cell(left_magnitude),
        number_cell(right_x), number_cell(right_y), number_cell(right_magnitude), "0",
        bool_cell(s._actor_state_ready),
        xinput and xinput.qpc_ns or "", xinput and xinput.utc_ns or "",
        number_cell(xinput and xinput.controller_index or nil),
        bool_cell(xinput and xinput.connected),
        number_cell(xinput and xinput.packet_number or nil),
        number_cell(xinput and xinput.button_mask or nil),
        number_cell(xinput and xinput.left_trigger_raw or nil),
        number_cell(xinput and xinput.right_trigger_raw or nil),
        xinput and xinput.session_id or "",
        number_cell(xinput and xinput.poll_duration_ns or nil),
    }) do
        table.insert(row, value)
    end

    if not write_cells(state.samples_file, row) then stop_capture("sample_write_error"); return end
    if not state.first_sample_row_recorded then
        diagnostic_write("first_sample_written", "sample_index=" .. tostring(state.sample_count))
        state.first_sample_row_recorded = true
    end
    state.last_game_time = s.game_time
    if clock_reset then write_event("clock_reset", "quest_time_s", previous.game_time, s.game_time) end
    for _, event in ipairs(input_events) do
        write_event(event[1], event[2], event[3], event[4])
    end
    local action = { lmt = s.lmt_id, fsm = s.fsm_id, target = s.fsm_target }
    if state.previous_action == nil then
        write_event("action_initial", "lmt_id", nil, action.lmt)
        write_event("action_initial", "fsm_id", nil, action.fsm)
    else
        if action.lmt ~= state.previous_action.lmt then write_event("action_changed", "lmt_id", state.previous_action.lmt, action.lmt) end
        if action.fsm ~= state.previous_action.fsm then write_event("action_changed", "fsm_id", state.previous_action.fsm, action.fsm) end
        if action.target ~= state.previous_action.target then write_event("action_changed", "fsm_target", state.previous_action.target, action.target) end
    end
    state.previous_action = action
    state.previous_sample = { x = s.x, y = s.y, z = s.z, game_time = s.game_time }
    state.previous_sample.qw, state.previous_sample.qx = s.qw, s.qx
    state.previous_sample.qy, state.previous_sample.qz = s.qy, s.qz
    state.previous_sample.aiming = s.aiming
    state.previous_sample.aim_x, state.previous_sample.aim_y, state.previous_sample.aim_z = s.aim_x, s.aim_y, s.aim_z
    if state.sample_count % FLUSH_EVERY_SAMPLES == 0 then
        pcall(function() state.samples_file:flush(); state.events_file:flush() end)
    end
end

function on_init()
    diagnostic_write("callback_on_init", "entered; engine=" .. tostring(type(engine)) ..
        "; keypad=" .. tostring(type(engine) == "table" and type(engine.keypad) or "missing") ..
        "; Message=" .. tostring(type(Message)) .. "; Console_Info=" .. tostring(type(Console_Info)))
    if resolve_sources() then report(SCRIPT_NAME .. " v" .. SCRIPT_VERSION .. " loaded; press F9 to record")
    else report(state.source_error or "LuaEngine initialization failed") end
end

function on_time()
    local ok, err = pcall(function()
        local key_ok, f9_down = false, false
        local key_error = nil
        if type(engine) == "table" and type(engine.keypad) == "function" then
            key_ok, f9_down = pcall(engine.keypad, CAPTURE_KEY, false)
            if not key_ok then key_error = tostring(f9_down) end
        else
            key_error = "engine.keypad is unavailable"
        end
        local normalized_f9_down = key_ok and (f9_down == true or f9_down == 1) or false
        if not state.diagnostic_first_time_logged then
            diagnostic_write("callback_on_time", "first tick; keypad_ok=" .. tostring(key_ok) ..
                "; F9_down=" .. tostring(normalized_f9_down) .. "; recording=" .. tostring(state.recording))
            state.diagnostic_first_time_logged = true
        end
        if key_error ~= state.diagnostic_last_key_error then
            if key_error ~= nil then diagnostic_write("keyboard_poll_error", key_error) end
            state.diagnostic_last_key_error = key_error
        end
        if normalized_f9_down ~= state.previous_f9_down then
            diagnostic_write("f9_state", "down=" .. tostring(normalized_f9_down) ..
                "; keypad_ok=" .. tostring(key_ok))
        end
        local f9_pressed = key_ok and normalized_f9_down and not state.previous_f9_down
        state.previous_f9_down = normalized_f9_down

        local now = os.time()
        if now - state.diagnostic_last_heartbeat >= DIAGNOSTIC_HEARTBEAT_SECONDS then
            diagnostic_write("on_time_heartbeat", "keypad_ok=" .. tostring(key_ok) ..
                "; F9_down=" .. tostring(normalized_f9_down) ..
                "; recording=" .. tostring(state.recording) ..
                "; samples=" .. tostring(state.sample_count) ..
                "; rejected_samples=" .. tostring(state.rejected_sample_count) ..
                "; unavailable_actor_samples=" .. tostring(state.unavailable_actor_sample_count) ..
                "; missing_position_samples=" .. tostring(state.missing_position_sample_count) ..
                "; invalid_orientation_samples=" .. tostring(state.invalid_orientation_sample_count) ..
                "; reject_reason=" .. tostring(state.waiting_for_player_reason or "none") ..
                "; source_ready=" .. tostring(state.player ~= nil and state.world ~= nil))
            state.diagnostic_last_heartbeat = now
        end
        if f9_pressed or state.pending_toggle then
            local reason = f9_pressed and CAPTURE_KEY or state.pending_toggle_reason or "ImGui"
            state.pending_toggle = false
            state.pending_toggle_reason = nil
            toggle_capture(reason)
        end
        if state.recording then collect_sample() end
    end)
    if not ok then
        state.last_error = tostring(err)
        diagnostic_write("on_time_error", tostring(err))
        if state.recording then stop_capture("callback_error") end
        report("录制回调错误：" .. tostring(err), false)
    end
end

function on_switch_scenes()
    if state.recording then write_event("scene_switch", "map_or_scene", nil, nil) end
end

function on_imgui()
    if ImGui == nil then return end
    local ok, err = pcall(function()
        ImGui.Begin("MHGZ MHW Recorder", true)
        ImGui.Text(SCRIPT_NAME .. " v" .. SCRIPT_VERSION)
        ImGui.Text(state.recording and ("录制中：" .. tostring(state.sample_count) .. " samples") or "空闲")
        if state.last_status ~= nil then ImGui.Text(state.last_status) end
        if state.recording then
            if ImGui.Button("STOP AND SAVE") then
                state.pending_toggle, state.pending_toggle_reason = true, "ImGui"
            end
        else
            if ImGui.Button("START RECORDING") then
                state.pending_toggle, state.pending_toggle_reason = true, "ImGui"
            end
        end
        ImGui.Text("F9: 开始/停止；由 LuaEngine on_time 回调采样")
        ImGui.Text("摇杆：方向布尔值可读，模拟量不可读；相机旋转不可读")
        if state.source_error ~= nil then ImGui.Text(state.source_error) end
        if state.last_error ~= nil then ImGui.Text("错误：" .. state.last_error) end
        ImGui.End()
    end)
    if not ok then
        state.last_error = tostring(err)
        diagnostic_write("on_imgui_error", tostring(err))
    end
end

-- This runs when the Lua file is evaluated, before on_init/on_time. If this
-- marker is absent after launch/reload, LuaEngine never executed this file.
diagnostic_write("script_loaded", "version=" .. SCRIPT_VERSION ..
    "; source=" .. tostring(debug and debug.getinfo and debug.getinfo(1, "S").source or "unknown") ..
    "; engine=" .. tostring(type(engine)) .. "; keypad=" ..
    tostring(type(engine) == "table" and type(engine.keypad) or "missing"))
