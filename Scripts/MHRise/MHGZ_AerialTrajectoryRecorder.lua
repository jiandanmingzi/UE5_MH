-- MHGZ Aerial Trajectory Recorder
--
-- Read-only REFramework recorder for Monster Hunter Rise.
-- F9 toggles a capture. F8 writes a read-only motion-state probe. F9 samples
-- both the master hunter's world Transform and the animation state evaluated
-- on each relevant motion layer, so a single capture contains trajectory,
-- facing and animation-frame data.  Facing is sampled from the same Transform
-- read as the position: without it a lateral displacement cannot be separated
-- from a heading change, and every lateral figure needs an orientation guess.
-- It deliberately does not change player state, input, save data, motion, or
-- time scale.

local SCRIPT_NAME = "MHGZ Aerial Trajectory Recorder"
local SCRIPT_VERSION = "1.6.0"
local OUTPUT_DIRECTORY = "MHGZ_AerialTrajectoryRecorder"
local VK_F9 = 0x78
local VK_F8 = 0x77
-- RE Engine / Monster Hunter Rise uses Y as the world-up axis.
local VERTICAL_AXIS = "Y"
local PHASE_SPEED_THRESHOLD = 0.02
local MOTION_LAYER_CAPTURE_COUNT = 5
local NODE_VALUE_KEYWORDS = {
    "motion", "id", "frame", "time", "weight", "blend", "rate", "state",
}

local state = {
    recording = false,
    samples_file = nil,
    events_file = nil,
    sample_count = 0,
    event_count = 0,
    capture_time = 0.0,
    previous_sample = nil,
    previous_phase = "unknown",
    -- Resolved lazily on the first sample of a capture; false means "probed and
    -- nothing worked", so the retry does not run 60 times a second.
    rotation_accessor = nil,
    previous_f9_down = false,
    previous_f8_down = false,
    previous_clock = os.clock(),
    previous_uptime_second = nil,
    toast_text = nil,
    toast_until = 0.0,
    toast_color = 0xFFFFFFFF,
    last_status = "Ready. Press F9 to start a capture.",
    last_error = nil,
    serial = 0,
    probe_serial = 0,
    last_probe_path = nil,
}

local MOTION_KEYWORDS = {
    "motion",
    "anim",
    "frame",
    "time",
    "rate",
    "blend",
    "layer",
    "action",
    "state",
    -- Facing is the one trajectory dimension the CSV used to lose, so let the
    -- F8 probe surface every rotation-shaped accessor it can find.
    "rot",
    "quat",
    "yaw",
    "facing",
    "forward",
}

local application_type = sdk.find_type_definition("via.Application")
local get_uptime_second = application_type ~= nil and application_type:get_method("get_UpTimeSecond") or nil

local function report_status(message)
    state.last_status = message
    if log ~= nil then
        log.info("[" .. SCRIPT_NAME .. "] " .. message)
    end
end

local function set_error(message)
    state.last_error = message
    report_status("Error: " .. message)
end

local function show_toast(text, seconds, color)
    state.toast_text = text
    state.toast_until = os.clock() + (seconds or 2.0)
    state.toast_color = color or 0xFFFFFFFF
end

local function csv_text(value)
    if value == nil then
        return ""
    end

    local text = tostring(value)
    return '"' .. text:gsub('"', '""') .. '"'
end

local function csv_number(value)
    if value == nil then
        return ""
    end

    return string.format("%.8f", value)
end

local function write_event(event_name, details)
    if state.events_file == nil then
        return
    end

    state.event_count = state.event_count + 1
    state.events_file:write(table.concat({
        state.event_count,
        csv_number(state.capture_time),
        csv_text(event_name),
        csv_text(state.previous_phase),
        csv_text(details or ""),
    }, ",") .. "\n")
end

local function read_delta_time()
    local now = os.clock()
    local fallback_delta = now - state.previous_clock
    state.previous_clock = now

    if get_uptime_second ~= nil then
        local ok, uptime_second = pcall(function()
            return get_uptime_second:call(nil)
        end)

        if ok and type(uptime_second) == "number" then
            local uptime_delta = nil
            if state.previous_uptime_second ~= nil then
                uptime_delta = uptime_second - state.previous_uptime_second
            end
            state.previous_uptime_second = uptime_second

            if uptime_delta ~= nil and uptime_delta > 0.0 and uptime_delta < 1.0 then
                return uptime_delta, "via.Application.get_UpTimeSecond"
            end
        end
    end

    if fallback_delta > 0.0 and fallback_delta < 1.0 then
        return fallback_delta, "os.clock_fallback"
    end

    return 1.0 / 60.0, "fixed_fallback"
end

-- Rotation is a *native* Transform accessor, so its name can never be read off
-- the managed method table: `get_methods()` does not list native methods, and
-- the F8 probe's MOTION_KEYWORDS filter would have dropped anything named
-- rotation/yaw/angle in any case.  Probe the known spellings once per capture
-- and remember the winner; when none resolves the columns stay empty instead of
-- aborting the recording.
local ROTATION_ACCESSORS = { "get_Rotation", "get_WorldRotation", "get_LocalRotation" }

local function read_quaternion(value)
    if value == nil then
        return nil
    end
    local ok, x, y, z, w = pcall(function()
        return value.x, value.y, value.z, value.w
    end)
    if not ok or type(x) ~= "number" then
        return nil
    end
    return { x = x, y = y, z = z, w = w }
end

-- Local forward (+Z) rotated by the unit quaternion.  MHRise is Y-up, so the
-- XZ pair is the facing plane.  The raw quaternion is written as well, so a
-- wrong axis convention can be corrected offline rather than by re-recording.
local function quaternion_forward(quaternion)
    if quaternion == nil then
        return nil
    end
    local x, y, z, w = quaternion.x, quaternion.y, quaternion.z, quaternion.w
    return {
        x = 2.0 * (x * z + w * y),
        y = 2.0 * (y * z - w * x),
        z = 1.0 - 2.0 * (x * x + y * y),
    }
end

local atan2 = math.atan2 or math.atan

local function forward_yaw_degrees(forward)
    if forward == nil then
        return nil
    end
    if forward.x == 0.0 and forward.z == 0.0 then
        return nil
    end
    return math.deg(atan2(forward.x, forward.z))
end

local function read_transform_rotation(transform)
    if state.rotation_accessor == false then
        return nil, nil
    end

    if state.rotation_accessor == nil then
        for _, candidate in ipairs(ROTATION_ACCESSORS) do
            local ok, value = pcall(function()
                return transform:call(candidate)
            end)
            if ok and read_quaternion(value) ~= nil then
                state.rotation_accessor = candidate
                break
            end
        end
        if state.rotation_accessor == nil then
            state.rotation_accessor = false
            return nil, "no readable rotation accessor on via.Transform"
        end
        write_event(
            "rotation_accessor_resolved",
            "facing is being captured via Transform." .. state.rotation_accessor
        )
    end

    local ok, value = pcall(function()
        return transform:call(state.rotation_accessor)
    end)
    if not ok then
        return nil, "rotation read failed via " .. tostring(state.rotation_accessor)
    end
    return read_quaternion(value), nil
end

local function get_master_player_position()
    local ok, result, detail = pcall(function()
        local player_manager = sdk.get_managed_singleton("snow.player.PlayerManager")
        if player_manager == nil then
            return nil, "PlayerManager is unavailable"
        end

        local player = player_manager:call("findMasterPlayer")
        if player == nil then
            return nil, "Master player is unavailable"
        end

        local game_object = player:call("get_GameObject")
        if game_object == nil then
            return nil, "Master player's GameObject is unavailable"
        end

        local transform = game_object:call("get_Transform")
        if transform == nil then
            return nil, "Master player's Transform is unavailable"
        end

        local position = transform:call("get_Position")
        if position == nil then
            return nil, "Master player's world position is unavailable"
        end

        -- Facing travels with the position sample so the two can never desync.
        -- A rotation failure is not a position failure: it downgrades the
        -- facing columns to empty and leaves the trajectory usable.
        local quaternion, rotation_error = read_transform_rotation(transform)
        local forward = quaternion_forward(quaternion)

        return {
            x = position.x,
            y = position.y,
            z = position.z,
            quaternion = quaternion,
            forward = forward,
            yaw = forward_yaw_degrees(forward),
            rotation_error = rotation_error,
        }, nil
    end)

    if not ok then
        return nil, tostring(result)
    end

    return result, detail
end

local function get_master_player_and_game_object()
    local ok, player, game_object, detail = pcall(function()
        local player_manager = sdk.get_managed_singleton("snow.player.PlayerManager")
        if player_manager == nil then
            return nil, nil, "PlayerManager is unavailable"
        end

        local master_player = player_manager:call("findMasterPlayer")
        if master_player == nil then
            return nil, nil, "Master player is unavailable"
        end

        local master_game_object = master_player:call("get_GameObject")
        if master_game_object == nil then
            return nil, nil, "Master player's GameObject is unavailable"
        end

        return master_player, master_game_object, nil
    end)

    if not ok then
        return nil, nil, tostring(player)
    end

    return player, game_object, detail
end

local function contains_motion_keyword(text)
    local lower_text = string.lower(tostring(text or ""))
    for _, keyword in ipairs(MOTION_KEYWORDS) do
        if string.find(lower_text, keyword, 1, true) ~= nil then
            return true
        end
    end
    return false
end

local function safe_type_name(type_definition)
    if type_definition == nil then
        return "<unknown type>"
    end

    local ok, name = pcall(function()
        return type_definition:get_full_name()
    end)
    return ok and tostring(name) or "<unreadable type>"
end

local function safe_object_type(object)
    if object == nil then
        return nil, nil
    end

    local ok, type_definition = pcall(function()
        return object:get_type_definition()
    end)
    if not ok or type_definition == nil then
        return nil, nil
    end

    return type_definition, safe_type_name(type_definition)
end

local function safe_object_address(object)
    local ok, address = pcall(function()
        return object:get_address()
    end)
    return ok and tostring(address) or "<no address>"
end

local function safe_call_no_arguments(object, method_name)
    local ok, value = pcall(function()
        return object:call(method_name)
    end)
    return ok, value
end

local function safe_call_layer_method(object, method_name, layer_index)
    local ok, value = pcall(function()
        return object:call(method_name, layer_index)
    end)
    return ok, value
end

local function motion_value_text(ok, value)
    if not ok then
        return "<unavailable>"
    end
    if value == nil then
        return "nil"
    end
    if type(value) == "number" then
        return string.format("%.4f", value)
    end
    return tostring(value)
end

local function safe_get_field_value(object, field_name)
    local ok, value = pcall(function()
        return object:get_field(field_name)
    end)
    return ok, value
end

local function contains_node_value_keyword(text)
    local lower_text = string.lower(tostring(text or ""))
    for _, keyword in ipairs(NODE_VALUE_KEYWORDS) do
        if string.find(lower_text, keyword, 1, true) ~= nil then
            return true
        end
    end
    return false
end

-- The layer getter represents the base layer, which can remain idle while a
-- child node plays the actual weapon action.  Capture the highest-weight node
-- as a compact, self-describing CSV value so the active action can be read
-- without manual F8 snapshots or assumptions about private field names.
local function highest_weight_node_summary(behavior, layer_index)
    local node_ok, node = safe_call_layer_method(behavior, "getHighestWeightMotionNode", layer_index)
    if not node_ok or node == nil then
        return nil
    end

    local type_definition = safe_object_type(node)
    if type_definition == nil then
        return nil
    end

    local entries = {}
    local ok_fields, fields = pcall(function()
        return type_definition:get_fields()
    end)
    if not ok_fields or fields == nil then
        return nil
    end

    for _, field in ipairs(fields) do
        local field_name = tostring(field:get_name())
        if contains_node_value_keyword(field_name) then
            local value_ok, value = pcall(function()
                return field:get_data(node)
            end)
            if value_ok and value ~= nil then
                local nested_type = safe_object_type(value)
                if nested_type == nil then
                    table.insert(entries, field_name .. "=" .. tostring(value))
                end
            end
        end
    end

    if #entries == 0 then
        return nil
    end
    return table.concat(entries, ";")
end

-- `snow.player.IG_Insect` inherits the CharacterBase motion accessors.  These
-- calls are intentionally read-only: they return the motion bank/id and frame
-- that the game is currently evaluating on each animation layer.
local function append_runtime_motion_snapshot(lines, player)
    table.insert(lines, "RUNTIME MOTION SNAPSHOT:")

    local behavior_ok, behavior = pcall(function()
        return player:get_field("_IG_InsectBehavior")
    end)
    if not behavior_ok or behavior == nil then
        table.insert(lines, "  IG behavior unavailable; cannot read motion layers.")
        return
    end

    local type_definition, type_name = safe_object_type(behavior)
    table.insert(lines, "  Behavior: " .. (type_name or "<unknown>"))
    if type_definition == nil then
        table.insert(lines, "  Motion-layer API unavailable on behavior.")
        return
    end

    local layer_count_ok, layer_count = safe_call_no_arguments(behavior, "getMotionLayerCount")
    if not layer_count_ok or type(layer_count) ~= "number" then
        table.insert(lines, "  Layer count: <unavailable>")
        return
    end

    -- The inspected player has five layers.  A cap protects the probe if a
    -- future build exposes an unexpectedly large number of auxiliary layers.
    local captured_layer_count = math.min(math.floor(layer_count), 16)
    table.insert(lines, string.format("  Layer count: %d", captured_layer_count))
    for layer = 0, captured_layer_count - 1 do
        local bank_ok, bank_id = safe_call_layer_method(behavior, "getMotionBankID_Layer", layer)
        local motion_ok, motion_id = safe_call_layer_method(behavior, "getMotionID_Layer", layer)
        local now_ok, now_frame = safe_call_layer_method(behavior, "getMotionNowFrame_Layer", layer)
        local previous_ok, previous_frame = safe_call_layer_method(behavior, "getMotionPrevFrame_Layer", layer)
        local end_ok, end_frame = safe_call_layer_method(behavior, "getMotionEndFrame_Layer", layer)
        local at_end_ok, at_end = safe_call_layer_method(behavior, "isMotionEnd_Layer", layer)
        local loop_ok, is_loop = safe_call_layer_method(behavior, "isLoopMotion", layer)

        table.insert(lines, string.format(
            "  L%d bank=%s motion=%s frame=%s prev=%s end=%s at_end=%s loop=%s",
            layer,
            motion_value_text(bank_ok, bank_id),
            motion_value_text(motion_ok, motion_id),
            motion_value_text(now_ok, now_frame),
            motion_value_text(previous_ok, previous_frame),
            motion_value_text(end_ok, end_frame),
            motion_value_text(at_end_ok, at_end),
            motion_value_text(loop_ok, is_loop)
        ))
    end
end

-- Keep this compact runtime read separate from the verbose F8 report.  It is
-- called once per captured game frame and supplies the columns written by F9.
-- Missing layers/methods deliberately become empty CSV cells instead of
-- aborting a trajectory capture.
local function read_runtime_motion_layers()
    local result = {
        layer_count = nil,
        layers = {},
        player_motion_old_id = nil,
        player_motion_old_id_ok = false,
    }

    local player, _, player_error = get_master_player_and_game_object()
    if player == nil then
        return result, player_error
    end

    local control_ok, motion_control = safe_get_field_value(player, "_RefPlayerMotionCtrl")
    if control_ok and motion_control ~= nil then
        result.player_motion_old_id_ok, result.player_motion_old_id = safe_get_field_value(
            motion_control,
            "_OldMotionID"
        )
    end

    local behavior_ok, behavior = pcall(function()
        return player:get_field("_IG_InsectBehavior")
    end)
    if not behavior_ok or behavior == nil then
        return result, "IG behavior is unavailable"
    end

    local count_ok, layer_count = safe_call_no_arguments(behavior, "getMotionLayerCount")
    if count_ok and type(layer_count) == "number" then
        result.layer_count = math.floor(layer_count)
    end

    for layer = 0, MOTION_LAYER_CAPTURE_COUNT - 1 do
        local data = {}
        if result.layer_count == nil or layer < result.layer_count then
            data.bank_ok, data.bank_id = safe_call_layer_method(behavior, "getMotionBankID_Layer", layer)
            data.motion_ok, data.motion_id = safe_call_layer_method(behavior, "getMotionID_Layer", layer)
            data.frame_ok, data.frame = safe_call_layer_method(behavior, "getMotionNowFrame_Layer", layer)
            data.end_ok, data.end_frame = safe_call_layer_method(behavior, "getMotionEndFrame_Layer", layer)
            data.at_end_ok, data.at_end = safe_call_layer_method(behavior, "isMotionEnd_Layer", layer)
            data.loop_ok, data.is_loop = safe_call_layer_method(behavior, "isLoopMotion", layer)
            data.highest_node = highest_weight_node_summary(behavior, layer)
        end
        result.layers[layer + 1] = data
    end

    return result, nil
end

local function captured_motion_value(ok, value)
    return ok and value or nil
end

local function safe_field_type_name(field)
    local ok, type_definition = pcall(function()
        return field:get_type()
    end)
    return ok and safe_type_name(type_definition) or "<unreadable field type>"
end

local function describe_field_value(field, object)
    local ok, value = pcall(function()
        return field:get_data(object)
    end)
    if not ok then
        return "<unreadable>"
    end
    if value == nil then
        return "nil"
    end

    local type_definition, type_name = safe_object_type(value)
    if type_definition ~= nil then
        return "object<" .. type_name .. ">@" .. safe_object_address(value), value
    end

    return tostring(value), nil
end

local function append_type_catalog(lines, object, label, visited, depth, include_all_fields)
    if object == nil or depth > 2 then
        return
    end

    local type_definition, type_name = safe_object_type(object)
    if type_definition == nil then
        table.insert(lines, string.rep("  ", depth) .. label .. ": <not a managed object>")
        return
    end

    local object_key = safe_object_address(object)
    if visited[object_key] then
        table.insert(lines, string.rep("  ", depth) .. label .. ": " .. type_name .. " (already listed)")
        return
    end
    visited[object_key] = true

    local indent = string.rep("  ", depth)
    table.insert(lines, indent .. "OBJECT " .. label .. " : " .. type_name .. " @" .. object_key)

    local current_type = type_definition
    local inheritance_depth = 0
    while current_type ~= nil and inheritance_depth < 8 do
        local current_type_name = safe_type_name(current_type)
        table.insert(lines, indent .. "  TYPE " .. current_type_name)

        local ok_fields, fields = pcall(function()
            return current_type:get_fields()
        end)
        if ok_fields and fields ~= nil then
            for _, field in ipairs(fields) do
                local field_name = tostring(field:get_name())
                local field_type_name = safe_field_type_name(field)
                local candidate = contains_motion_keyword(field_name) or contains_motion_keyword(field_type_name)
                if include_all_fields or candidate then
                    local value_text = ""
                    local nested_object = nil
                    if candidate then
                        value_text, nested_object = describe_field_value(field, object)
                    end
                    table.insert(
                        lines,
                        indent .. "    FIELD " .. field_name .. " : " .. field_type_name
                            .. (candidate and " = " .. value_text .. " [candidate]" or "")
                    )
                    if candidate and nested_object ~= nil and depth < 2 then
                        append_type_catalog(
                            lines,
                            nested_object,
                            label .. "." .. field_name,
                            visited,
                            depth + 1,
                            false
                        )
                    end
                end
            end
        end

        local ok_methods, methods = pcall(function()
            return current_type:get_methods()
        end)
        if ok_methods and methods ~= nil then
            for _, method in ipairs(methods) do
                local method_name = tostring(method:get_name())
                if contains_motion_keyword(method_name) then
                    local ok_params, parameter_count = pcall(function()
                        return method:get_num_params()
                    end)
                    local return_type = "<unknown return>"
                    local ok_return, return_type_definition = pcall(function()
                        return method:get_return_type()
                    end)
                    if ok_return then
                        return_type = safe_type_name(return_type_definition)
                    end
                    table.insert(
                        lines,
                        indent .. "    METHOD " .. method_name .. " ("
                            .. (ok_params and tostring(parameter_count) or "?")
                            .. " params) -> " .. return_type .. " [candidate]"
                    )
                end
            end
        end

        local ok_parent, parent_type = pcall(function()
            return current_type:get_parent_type()
        end)
        current_type = ok_parent and parent_type or nil
        inheritance_depth = inheritance_depth + 1
    end
end

local function write_motion_probe()
    state.probe_serial = state.probe_serial + 1
    local timestamp = os.date("%Y%m%d_%H%M%S")
    local path = string.format(
        "%s/mhrise_motion_probe_%s_%02d.txt",
        OUTPUT_DIRECTORY,
        timestamp,
        state.probe_serial
    )
    local handle, open_error = io.open(path, "w")
    if handle == nil then
        set_error("Cannot create motion probe " .. path .. ": " .. tostring(open_error))
        show_toast("[PROBE ERROR] See REFramework UI", 3.0, 0xFF0000FF)
        return
    end

    local lines = {
        SCRIPT_NAME .. " v" .. SCRIPT_VERSION .. " - read-only motion probe",
        "Snapshot time: " .. os.date("%Y-%m-%d %H:%M:%S"),
        "Candidate keywords: " .. table.concat(MOTION_KEYWORDS, ", "),
        "The runtime snapshot calls read-only motion getters; the remainder only reads reflected fields/method descriptors.",
        "",
    }
    local player, game_object, player_error = get_master_player_and_game_object()
    if player == nil or game_object == nil then
        table.insert(lines, "PLAYER UNAVAILABLE: " .. tostring(player_error))
    else
        append_runtime_motion_snapshot(lines, player)
        table.insert(lines, "")
        local visited = {}
        append_type_catalog(lines, player, "MasterPlayer", visited, 0, true)
        append_type_catalog(lines, game_object, "MasterPlayer.GameObject", visited, 0, false)

        local ok_components, components = pcall(function()
            local component_array = game_object:call("get_Components")
            return component_array ~= nil and component_array:get_elements() or {}
        end)
        if not ok_components then
            table.insert(lines, "COMPONENT ENUMERATION FAILED: " .. tostring(components))
        else
            table.insert(lines, "")
            table.insert(lines, "COMPONENTS: " .. tostring(#components))
            for index, component in ipairs(components) do
                local _, component_type_name = safe_object_type(component)
                table.insert(lines, string.format("COMPONENT[%d] %s", index, component_type_name or "<unknown>"))
                append_type_catalog(
                    lines,
                    component,
                    "Component[" .. tostring(index) .. "]",
                    visited,
                    1,
                    false
                )
            end
        end
    end

    handle:write(table.concat(lines, "\n") .. "\n")
    handle:flush()
    handle:close()
    state.last_probe_path = path
    state.last_error = nil
    report_status("Motion probe written: " .. path)
    show_toast("[PROBE SAVED] Motion report written", 3.0, 0xFF00FFFF)
end

local function vertical_phase(vertical_speed)
    if vertical_speed == nil then
        return "unknown"
    end
    if vertical_speed > PHASE_SPEED_THRESHOLD then
        return "rising"
    end
    if vertical_speed < -PHASE_SPEED_THRESHOLD then
        return "falling"
    end
    return "stable"
end

local function stop_capture(reason)
    if not state.recording then
        return
    end

    write_event("capture_stopped", reason or "F9")

    if state.samples_file ~= nil then
        state.samples_file:flush()
        state.samples_file:close()
    end
    if state.events_file ~= nil then
        state.events_file:flush()
        state.events_file:close()
    end

    state.recording = false
    state.samples_file = nil
    state.events_file = nil
    report_status(string.format("Capture stopped: %d samples written.", state.sample_count))
    show_toast("[SAVED] " .. tostring(state.sample_count) .. " SAMPLES WRITTEN", 2.5, 0xFF00FF00)
end

local function start_capture()
    if state.recording then
        return
    end

    state.serial = state.serial + 1
    local timestamp = os.date("%Y%m%d_%H%M%S")
    local basename = string.format("mhrise_%s_%02d", timestamp, state.serial)
    local samples_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_samples.csv"
    local events_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_events.csv"

    local samples_file, samples_error = io.open(samples_path, "w")
    if samples_file == nil then
        set_error("Cannot create " .. samples_path .. ": " .. tostring(samples_error))
        return
    end

    local events_file, events_error = io.open(events_path, "w")
    if events_file == nil then
        samples_file:close()
        set_error("Cannot create " .. events_path .. ": " .. tostring(events_error))
        return
    end

    state.recording = true
    state.samples_file = samples_file
    state.events_file = events_file
    state.sample_count = 0
    state.event_count = 0
    state.capture_time = 0.0
    state.previous_sample = nil
    state.previous_phase = "unknown"
    state.previous_clock = os.clock()
    state.previous_uptime_second = nil
    state.last_error = nil
    state.rotation_accessor = nil

    local sample_header = {
        "sample_index", "capture_time_s", "delta_time_s", "time_source",
        "world_x", "world_y", "world_z",
        "velocity_x", "velocity_y", "velocity_z",
        "acceleration_x", "acceleration_y", "acceleration_z", "vertical_phase",
        "rot_qx", "rot_qy", "rot_qz", "rot_qw",
        "forward_x", "forward_y", "forward_z", "facing_yaw_deg",
        "motion_layer_count",
        "player_motion_old_id",
    }
    for layer = 0, MOTION_LAYER_CAPTURE_COUNT - 1 do
        local prefix = "motion_l" .. tostring(layer) .. "_"
        table.insert(sample_header, prefix .. "bank_id")
        table.insert(sample_header, prefix .. "id")
        table.insert(sample_header, prefix .. "frame")
        table.insert(sample_header, prefix .. "end_frame")
        table.insert(sample_header, prefix .. "at_end")
        table.insert(sample_header, prefix .. "loop")
        table.insert(sample_header, prefix .. "highest_weight_node")
    end
    samples_file:write(table.concat(sample_header, ",") .. "\n")
    events_file:write("event_index,capture_time_s,event,vertical_phase,details\n")
    write_event("capture_started", "F9; raw world Transform capture")
    report_status("Capture started: " .. samples_path)
    show_toast("[REC] TRAJECTORY CAPTURE STARTED", 2.5, 0xFF00FFFF)
end

local function toggle_capture()
    if state.recording then
        stop_capture("F9")
    else
        start_capture()
    end
end

local function record_sample()
    local position, position_error = get_master_player_position()
    if position == nil then
        if state.last_error ~= position_error then
            state.last_error = position_error
            write_event("player_unavailable", position_error)
            report_status("Waiting for player: " .. position_error)
        end
        return
    end

    local delta_time, time_source = read_delta_time()
    state.capture_time = state.capture_time + delta_time

    local velocity_x, velocity_y, velocity_z = nil, nil, nil
    local acceleration_x, acceleration_y, acceleration_z = nil, nil, nil
    if state.previous_sample ~= nil then
        velocity_x = (position.x - state.previous_sample.x) / delta_time
        velocity_y = (position.y - state.previous_sample.y) / delta_time
        velocity_z = (position.z - state.previous_sample.z) / delta_time

        if state.previous_sample.velocity_x ~= nil then
            acceleration_x = (velocity_x - state.previous_sample.velocity_x) / delta_time
            acceleration_y = (velocity_y - state.previous_sample.velocity_y) / delta_time
            acceleration_z = (velocity_z - state.previous_sample.velocity_z) / delta_time
        end
    end

    local phase = vertical_phase(velocity_y)
    if phase ~= state.previous_phase then
        write_event("vertical_phase_changed", state.previous_phase .. " -> " .. phase)
        state.previous_phase = phase
    end

    if position.rotation_error ~= nil and state.last_error ~= position.rotation_error then
        state.last_error = position.rotation_error
        write_event("facing_unavailable", position.rotation_error)
    end

    state.sample_count = state.sample_count + 1
    local motion_layers, motion_error = read_runtime_motion_layers()
    if motion_error ~= nil and state.last_error ~= motion_error then
        state.last_error = motion_error
        write_event("motion_state_unavailable", motion_error)
    end

    local row = {
        state.sample_count,
        csv_number(state.capture_time),
        csv_number(delta_time),
        csv_text(time_source),
        csv_number(position.x),
        csv_number(position.y),
        csv_number(position.z),
        csv_number(velocity_x),
        csv_number(velocity_y),
        csv_number(velocity_z),
        csv_number(acceleration_x),
        csv_number(acceleration_y),
        csv_number(acceleration_z),
        csv_text(phase),
        csv_number(position.quaternion and position.quaternion.x),
        csv_number(position.quaternion and position.quaternion.y),
        csv_number(position.quaternion and position.quaternion.z),
        csv_number(position.quaternion and position.quaternion.w),
        csv_number(position.forward and position.forward.x),
        csv_number(position.forward and position.forward.y),
        csv_number(position.forward and position.forward.z),
        csv_number(position.yaw),
        csv_number(motion_layers.layer_count),
        csv_number(captured_motion_value(motion_layers.player_motion_old_id_ok, motion_layers.player_motion_old_id)),
    }
    for layer = 1, MOTION_LAYER_CAPTURE_COUNT do
        local layer_data = motion_layers.layers[layer] or {}
        table.insert(row, csv_number(captured_motion_value(layer_data.bank_ok, layer_data.bank_id)))
        table.insert(row, csv_number(captured_motion_value(layer_data.motion_ok, layer_data.motion_id)))
        table.insert(row, csv_number(captured_motion_value(layer_data.frame_ok, layer_data.frame)))
        table.insert(row, csv_number(captured_motion_value(layer_data.end_ok, layer_data.end_frame)))
        table.insert(row, csv_text(captured_motion_value(layer_data.at_end_ok, layer_data.at_end)))
        table.insert(row, csv_text(captured_motion_value(layer_data.loop_ok, layer_data.is_loop)))
        table.insert(row, csv_text(layer_data.highest_node))
    end
    state.samples_file:write(table.concat(row, ",") .. "\n")

    if state.sample_count % 120 == 0 then
        state.samples_file:flush()
        state.events_file:flush()
    end

    state.previous_sample = {
        x = position.x,
        y = position.y,
        z = position.z,
        velocity_x = velocity_x,
        velocity_y = velocity_y,
        velocity_z = velocity_z,
    }
    if motion_error == nil then
        state.last_error = nil
    end
end

local function draw_capture_feedback()
    if state.recording then
        draw.filled_rect(20, 20, 420, 46, 0xB0000000)
        draw.outline_rect(20, 20, 420, 46, 0xFF0000FF)
        draw.text(
            "[REC] TRAJECTORY CAPTURE  |  F9: STOP  |  Samples: " .. tostring(state.sample_count),
            32,
            34,
            0xFF0000FF
        )
    end

    if state.toast_text ~= nil and os.clock() < state.toast_until then
        draw.filled_rect(20, 78, 420, 38, 0xC0000000)
        draw.outline_rect(20, 78, 420, 38, state.toast_color)
        draw.text(state.toast_text, 32, 89, state.toast_color)
    end
end

re.on_frame(function()
    local ok, f9_down = pcall(function()
        return reframework:is_key_down(VK_F9)
    end)

    if ok and f9_down and not state.previous_f9_down then
        toggle_capture()
    end
    state.previous_f9_down = ok and f9_down or false

    local probe_ok, f8_down = pcall(function()
        return reframework:is_key_down(VK_F8)
    end)
    if probe_ok and f8_down and not state.previous_f8_down then
        write_motion_probe()
    end
    state.previous_f8_down = probe_ok and f8_down or false

    if state.recording then
        record_sample()
    end

    draw_capture_feedback()
end)

re.on_draw_ui(function()
    imgui.text(SCRIPT_NAME .. " v" .. SCRIPT_VERSION)
    imgui.text(state.recording and "Status: RECORDING" or "Status: idle")

    if imgui.button(state.recording and "Stop capture (F9)" or "Start capture (F9)") then
        toggle_capture()
    end
    if imgui.button("Write motion probe (F8)") then
        write_motion_probe()
    end

    imgui.text("Output: reframework/data/" .. OUTPUT_DIRECTORY)
    imgui.text("Samples: " .. tostring(state.sample_count))
    imgui.text("F9 rows include layer state plus the highest-weight action node and PlayerMotionControl ID.")
    imgui.text("F8 remains an optional verbose state probe for diagnostics.")
    if state.last_probe_path ~= nil then
        imgui.text("Last probe: " .. state.last_probe_path)
    end
    imgui.text(state.last_status)
    if state.last_error ~= nil then
        imgui.text("Last error: " .. state.last_error)
    end
    imgui.text("Data: world position + animation-layer state each game frame; velocity/acceleration use backward differences.")
end)

re.on_script_reset(function()
    stop_capture("REFramework script reset")
end)

show_toast("Trajectory recorder ready: F9 records trajectory + motion state.", 3.0, 0xFFFFFFFF)
report_status("Loaded. F9 records trajectory plus motion-layer state; F8 writes a verbose motion probe.")
