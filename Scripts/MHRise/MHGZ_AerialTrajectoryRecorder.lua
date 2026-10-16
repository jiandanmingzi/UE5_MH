-- MHGZ Aerial Trajectory Recorder
--
-- Read-only REFramework recorder for Monster Hunter Rise.
-- F9 toggles a capture. F8 writes a read-only motion-state probe. F9 samples
-- the master hunter's world Transform, the primary camera's world Transform and
-- legacy behavior-layer state. A separate *_animation.csv reads the actual
-- via.motion.Motion component; unavailable node frames are never invented.
-- Facing is sampled from the same Transform read as the position: without it a
-- lateral displacement cannot be separated from a heading change, and every
-- lateral figure needs an orientation guess.  The camera is sampled alongside
-- it because the *input* mapping ("is this move's back the hunter's back or the
-- camera's back?") cannot be recovered from the hunter's facing alone.
-- It deliberately does not change player state, input, save data, motion, or
-- time scale.

local SCRIPT_NAME = "MHGZ Aerial Trajectory Recorder"
local SCRIPT_VERSION = "1.13.0"
local OUTPUT_DIRECTORY = "MHGZ_AerialTrajectoryRecorder"
local HOTKEY_CONFIG_FILE = "MHGZ_AerialTrajectoryRecorder_hotkey.txt"
local VK_F9 = 0x78
local VK_F8 = 0x77
local VK_ESCAPE = 0x1B
local VK_INSERT = 0x2D
local VK_SHIFT = 0x10
local VK_CONTROL = 0x11
local VK_MENU = 0x12
local VK_LWIN = 0x5B
local VK_RWIN = 0x5C
local VK_LSHIFT = 0xA0
local VK_RSHIFT = 0xA1
local VK_LCONTROL = 0xA2
local VK_RCONTROL = 0xA3
local VK_LMENU = 0xA4
local VK_RMENU = 0xA5
-- RE Engine / Monster Hunter Rise uses Y as the world-up axis.
local VERTICAL_AXIS = "Y"
local PHASE_SPEED_THRESHOLD = 0.02
local MOTION_LAYER_CAPTURE_COUNT = 5
local CAPTURE_HEALTH_TIMEOUT_SECONDS = 3
local NODE_VALUE_KEYWORDS = {
    "motion", "id", "frame", "time", "weight", "blend", "rate", "state",
}

local state = {
    recording = false,
    samples_file = nil,
    events_file = nil,
    sample_count = 0,
    health_state = "idle",
    health_message = "",
    health_started_at = nil,
    health_last_valid_at = nil,
    health_valid_sample_count = 0,
    health_problem = nil,
    event_count = 0,
    capture_time = 0.0,
    previous_sample = nil,
    previous_phase = "unknown",
    -- Resolved lazily on the first sample of a capture; false means "probed and
    -- nothing worked", so the retry does not run 60 times a second.
    rotation_accessor = nil,
    -- Camera.  Kept separate from the player path above: it is a different
    -- GameObject and the camera object itself is *not* cached (see
    -- resolve_primary_camera), so only the accessor name and the log bookkeeping
    -- live here.  "No camera right now" is transient, never fatal.
    camera_rotation_accessor = nil,
    camera_resolved_logged = false,
    camera_logged_error = nil,
    previous_f8_down = false,
    previous_capture_key_down = false,
    f9_key_down = false,
    f8_key_down = false,
    capture_key_down = false,
    capture_vk = VK_F9,
    capture_key_name = "F9",
    binding_capture_key = false,
    capture_key_binding_armed = false,
    rejected_binding_vk = nil,
    keyboard_backend = "initializing",
    keyboard_backend_logged = nil,
    last_keyboard_key = "none",
    native_keyboard_ready = false,
    native_keyboard_device = nil,
    native_key_previous = {},
    gamepad_backend = "initializing",
    gamepad_backend_logged = nil,
    gamepad_device_type_name = nil,
    gamepad_snapshot = {
        available = nil,
        axis_l_x = nil, axis_l_y = nil, axis_l_z = nil,
        axis_r_x = nil, axis_r_y = nil, axis_r_z = nil,
        buttons = {}, pressed = {}, released = {},
    },
    gamepad_previous_buttons = {},
    gamepad_pending_pressed = {},
    gamepad_pending_released = {},
    gamepad_status_logged = nil,
    frame_callback_seen = false,
    frame_callback_count = 0,
    last_frame_error = nil,
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
local keyboard_singleton_ok, keyboard_singleton = pcall(sdk.get_native_singleton, "via.hid.Keyboard")
local keyboard_type_ok, keyboard_type = pcall(sdk.find_type_definition, "via.hid.Keyboard")
local keyboard_key_type_ok, keyboard_key_type = pcall(sdk.find_type_definition, "via.hid.KeyboardKey")
if not keyboard_singleton_ok then keyboard_singleton = nil end
if not keyboard_type_ok then keyboard_type = nil end
if not keyboard_key_type_ok then keyboard_key_type = nil end
local keyboard_key_values = {}
local keyboard_key_names = {}
local bindable_keyboard_key_names = {}

local function is_bindable_hid_key(name)
    if type(name) ~= "string" or name == "" then
        return false
    end

    local normalized = name:upper():gsub("[^A-Z0-9]", "")
    return normalized ~= "F8"
        and normalized ~= "ESC"
        and normalized ~= "ESCAPE"
        and normalized ~= "INSERT"
        and normalized ~= "SHIFT"
        and normalized ~= "LSHIFT"
        and normalized ~= "RSHIFT"
        and normalized ~= "CONTROL"
        and normalized ~= "CTRL"
        and normalized ~= "LCONTROL"
        and normalized ~= "RCONTROL"
        and normalized ~= "MENU"
        and normalized ~= "ALT"
        and normalized ~= "LMENU"
        and normalized ~= "RMENU"
        and normalized ~= "LWIN"
        and normalized ~= "RWIN"
        and normalized ~= "NONE"
        and normalized ~= "INVALID"
        and normalized ~= "UNKNOWN"
end

local function initialize_keyboard_key_map()
    if keyboard_key_type == nil then
        state.keyboard_backend = "via.hid enum unavailable"
        return
    end

    local ok, fields = pcall(function()
        return keyboard_key_type:get_fields()
    end)
    if not ok or fields == nil then
        state.keyboard_backend = "via.hid enum unreadable"
        return
    end

    for _, field in ipairs(fields) do
        local field_ok, is_static = pcall(function() return field:is_static() end)
        if field_ok and is_static then
            local value_ok, value = pcall(function() return field:get_data(nil) end)
            local name_ok, name = pcall(function() return field:get_name() end)
            if value_ok and type(value) == "number" and name_ok and type(name) == "string" then
                keyboard_key_values[name] = value
                if keyboard_key_names[value] == nil then
                    keyboard_key_names[value] = name
                end
                if is_bindable_hid_key(name) then
                    table.insert(bindable_keyboard_key_names, name)
                end
            end
        end
    end

    table.sort(bindable_keyboard_key_names)
end

initialize_keyboard_key_map()

-- REFramework's own FreeCam reads controller state through
-- via.hid.GamePad:get_LastInputDevice(), GamePadDevice.AxisL/AxisR and
-- GamePadDevice:isDown(via.hid.GamePadButton). Keep this recorder read-only and
-- resolve the button enum dynamically so the CSV captures the enum names
-- available in this game's RE Engine build instead of assuming a layout.
local gamepad_singleton_ok, gamepad_singleton = pcall(sdk.get_native_singleton, "via.hid.GamePad")
local gamepad_type_ok, gamepad_type = pcall(sdk.find_type_definition, "via.hid.GamePad")
local gamepad_device_type_ok, gamepad_device_type = pcall(sdk.find_type_definition, "via.hid.GamePadDevice")
local gamepad_button_type_ok, gamepad_button_type = pcall(sdk.find_type_definition, "via.hid.GamePadButton")
if not gamepad_singleton_ok then gamepad_singleton = nil end
if not gamepad_type_ok then gamepad_type = nil end
if not gamepad_device_type_ok then gamepad_device_type = nil end
if not gamepad_button_type_ok then gamepad_button_type = nil end

local gamepad_button_values = {}
local gamepad_button_names = {}
local gamepad_is_down_method = nil
if gamepad_device_type ~= nil then
    local method_ok, method = pcall(function()
        return gamepad_device_type:get_method("isDown(via.hid.GamePadButton)")
    end)
    if method_ok then
        gamepad_is_down_method = method
    end
end

local function initialize_gamepad_button_map()
    if gamepad_button_type == nil then
        state.gamepad_backend = "via.hid.GamePadButton enum unavailable"
        return
    end

    local ok, fields = pcall(function()
        return gamepad_button_type:get_fields()
    end)
    if not ok or fields == nil then
        state.gamepad_backend = "via.hid.GamePadButton enum unreadable"
        return
    end

    for _, field in ipairs(fields) do
        local field_ok, is_static = pcall(function() return field:is_static() end)
        if field_ok and is_static then
            local value_ok, value = pcall(function() return field:get_data(nil) end)
            local name_ok, name = pcall(function() return field:get_name() end)
            if value_ok and type(value) == "number" and name_ok and type(name) == "string" then
                gamepad_button_values[name] = value
                table.insert(gamepad_button_names, name)
            end
        end
    end

    table.sort(gamepad_button_names)
    if gamepad_singleton == nil or gamepad_type == nil then
        state.gamepad_backend = "via.hid.GamePad singleton unavailable"
    elseif gamepad_device_type == nil then
        state.gamepad_backend = "via.hid.GamePadDevice type unavailable"
    else
        state.gamepad_backend = "Waiting for controller input device"
    end
end

initialize_gamepad_button_map()

local function gamepad_button_column_name(name)
    return tostring(name):gsub("[^A-Za-z0-9_]", "_")
end

local function read_gamepad_axis(device, axis_name)
    -- AxisL/AxisR are exposed as properties in the native FreeCam API. Try the
    -- property getter first, then the backing field for RE Engine builds that
    -- surface them directly in the TDB.
    local ok, value = pcall(function()
        return device:call("get_" .. axis_name)
    end)
    if ok and value ~= nil then
        local vector_ok, x, y, z = pcall(function()
            return value.x, value.y, value.z
        end)
        if vector_ok and type(x) == "number" and type(y) == "number" then
            return { x = x, y = y, z = type(z) == "number" and z or nil }
        end
    end

    ok, value = pcall(function()
        return device:get_field(axis_name)
    end)
    if ok and value ~= nil then
        local vector_ok, x, y, z = pcall(function()
            return value.x, value.y, value.z
        end)
        if vector_ok and type(x) == "number" and type(y) == "number" then
            return { x = x, y = y, z = type(z) == "number" and z or nil }
        end
    end

    return nil
end

local function read_gamepad_button(device, button_value)
    local ok, down = false, false
    if gamepad_is_down_method ~= nil then
        ok, down = pcall(function()
            return gamepad_is_down_method:call(device, button_value)
        end)
    end
    if not ok then
        ok, down = pcall(function()
            return device:call("isDown", button_value)
        end)
    end
    return ok, ok and not not down or false
end

local function poll_native_gamepad()
    if gamepad_singleton == nil or gamepad_type == nil or gamepad_device_type == nil then
        state.gamepad_backend = "GamePad API unavailable"
        state.gamepad_snapshot = {
            available = false,
            buttons = {},
            pressed = state.gamepad_pending_pressed,
            released = state.gamepad_pending_released,
        }
        state.gamepad_previous_buttons = {}
        return false
    end

    local device_ok, device = pcall(function()
        return sdk.call_native_func(gamepad_singleton, gamepad_type, "get_LastInputDevice")
    end)
    if not device_ok then
        device_ok, device = pcall(function()
            return gamepad_singleton:call("get_LastInputDevice")
        end)
    end
    if not device_ok then
        state.gamepad_backend = "get_LastInputDevice read failed"
        state.gamepad_snapshot = {
            available = false,
            buttons = {},
            pressed = state.gamepad_pending_pressed,
            released = state.gamepad_pending_released,
        }
        state.gamepad_previous_buttons = {}
        return false
    end
    if device == nil then
        state.gamepad_backend = "No active gamepad input device"
        state.gamepad_device_type_name = nil
        state.gamepad_snapshot = {
            available = false,
            buttons = {},
            pressed = state.gamepad_pending_pressed,
            released = state.gamepad_pending_released,
        }
        state.gamepad_previous_buttons = {}
        return true
    end

    local device_type_ok, device_type = pcall(function()
        return device:get_type_definition()
    end)
    if device_type_ok and device_type ~= nil then
        local name_ok, type_name = pcall(function()
            return device_type:get_full_name()
        end)
        state.gamepad_device_type_name = name_ok and type_name or "unknown gamepad device"
    else
        state.gamepad_device_type_name = "unknown gamepad device"
    end

    local axis_l = read_gamepad_axis(device, "AxisL")
    local axis_r = read_gamepad_axis(device, "AxisR")
    local buttons, pressed, released = {}, {}, {}
    local states_by_value = {}
    local successful_button_reads = 0

    for _, name in ipairs(gamepad_button_names) do
        local value = gamepad_button_values[name]
        if states_by_value[value] == nil then
            local button_ok, is_down = read_gamepad_button(device, value)
            if button_ok then
                states_by_value[value] = not not is_down
            else
                states_by_value[value] = "unavailable"
            end
        end

        local is_down = states_by_value[value]
        if is_down ~= "unavailable" then
            successful_button_reads = successful_button_reads + 1
            buttons[name] = is_down
            local previous = state.gamepad_previous_buttons[name]
            if previous ~= nil and is_down and not previous then
                state.gamepad_pending_pressed[name] = true
            elseif previous ~= nil and previous and not is_down then
                state.gamepad_pending_released[name] = true
            end
            pressed[name] = state.gamepad_pending_pressed[name] or false
            released[name] = state.gamepad_pending_released[name] or false
        else
            buttons[name] = nil
            pressed[name] = nil
            released[name] = nil
        end
    end

    state.gamepad_previous_buttons = buttons
    state.gamepad_snapshot = {
        available = true,
        axis_l_x = axis_l and axis_l.x or nil,
        axis_l_y = axis_l and axis_l.y or nil,
        axis_l_z = axis_l and axis_l.z or nil,
        axis_r_x = axis_r and axis_r.x or nil,
        axis_r_y = axis_r and axis_r.y or nil,
        axis_r_z = axis_r and axis_r.z or nil,
        buttons = buttons,
        pressed = state.gamepad_pending_pressed,
        released = state.gamepad_pending_released,
    }

    if axis_l ~= nil and axis_r ~= nil and successful_button_reads > 0 then
        state.gamepad_backend = "Connected: axes + " .. tostring(successful_button_reads) .. " button states"
    elseif axis_l ~= nil or axis_r ~= nil then
        state.gamepad_backend = "Connected: partial axis/button reads"
    elseif successful_button_reads > 0 then
        state.gamepad_backend = "Connected: button states; axes unavailable"
    else
        state.gamepad_backend = "Connected: controller input fields unavailable"
    end
    return true
end

local function key_name(vk)
    if type(vk) == "string" then
        return vk
    end

    if vk >= 0x70 and vk <= 0x7B then
        return "F" .. tostring(vk - 0x6F)
    end
    if vk >= 0x41 and vk <= 0x5A then
        return string.char(vk)
    end
    if vk >= 0x30 and vk <= 0x39 then
        return string.char(vk)
    end
    if vk >= 0x60 and vk <= 0x69 then
        return "Numpad" .. tostring(vk - 0x60)
    end

    local names = {
        [0x20] = "Space",
        [0x21] = "PageUp",
        [0x22] = "PageDown",
        [0x23] = "End",
        [0x24] = "Home",
        [0x25] = "Left",
        [0x26] = "Up",
        [0x27] = "Right",
        [0x28] = "Down",
        [0x2E] = "Delete",
        [0x6A] = "Numpad*",
        [0x6B] = "Numpad+",
        [0x6D] = "Numpad-",
        [0x6E] = "Numpad.",
        [0x6F] = "Numpad/",
    }
    return names[vk] or ("VK " .. tostring(vk))
end

local function key_vk_from_name(name)
    if type(name) ~= "string" then
        return nil
    end
    if #name == 1 then
        local byte = string.byte(name:upper())
        if (byte >= 0x30 and byte <= 0x39) or (byte >= 0x41 and byte <= 0x5A) then
            return byte
        end
    end
    local f_number = tonumber(name:match("^[Ff](%d+)$"))
    if f_number ~= nil and f_number >= 1 and f_number <= 24 then
        return 0x6F + f_number
    end
    local name_to_vk = {
        Space = 0x20, PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
        Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Delete = 0x2E,
        Numpad0 = 0x60, Numpad1 = 0x61, Numpad2 = 0x62, Numpad3 = 0x63,
        Numpad4 = 0x64, Numpad5 = 0x65, Numpad6 = 0x66, Numpad7 = 0x67,
        Numpad8 = 0x68, Numpad9 = 0x69,
    }
    return name_to_vk[name]
end

local function load_capture_key()
    local handle = io.open(HOTKEY_CONFIG_FILE, "r")
    if handle == nil then
        return "F9"
    end

    local value = handle:read("*l")
    handle:close()
    if value ~= nil then
        value = value:match("^%s*(.-)%s*$")
        local legacy_vk = tonumber(value)
        if legacy_vk ~= nil and legacy_vk >= 1 and legacy_vk <= 255 and legacy_vk ~= VK_F8 then
            return key_name(math.floor(legacy_vk))
        end
        if keyboard_key_values[value] ~= nil and is_bindable_hid_key(value) then
            return value
        end
    end
    return "F9"
end

state.capture_key_name = load_capture_key()
state.capture_vk = key_vk_from_name(state.capture_key_name)

local function report_status(message)
    state.last_status = message
    if log ~= nil then
        log.info("[" .. SCRIPT_NAME .. "] " .. message)
    end
end

local function show_toast(text, seconds, color)
    state.toast_text = text
    state.toast_until = os.clock() + (seconds or 2.0)
    state.toast_color = color or 0xFFFFFFFF
end

local function set_error(message)
    state.last_error = message
    report_status("Error: " .. message)
    show_toast("[REC ERROR] " .. tostring(message), 5.0, 0xFF0000FF)
end

local function handle_callback_error(context, err)
    local message = context .. ": " .. tostring(err)
    if state.last_frame_error ~= message then
        state.last_frame_error = message
        set_error(message)
    end
end

local function is_bindable_key(vk)
    if type(vk) ~= "number" or vk < 0x30 or vk > 0xFF then
        return false
    end

    return vk ~= VK_F8
        and vk ~= VK_ESCAPE
        and vk ~= VK_INSERT
        and vk ~= VK_SHIFT
        and vk ~= VK_CONTROL
        and vk ~= VK_MENU
        and vk ~= VK_LWIN
        and vk ~= VK_RWIN
        and vk ~= VK_LSHIFT
        and vk ~= VK_RSHIFT
        and vk ~= VK_LCONTROL
        and vk ~= VK_RCONTROL
        and vk ~= VK_LMENU
        and vk ~= VK_RMENU
end

local function save_capture_key(key)
    local key_label = key_name(key)
    local vk = type(key) == "number" and key or key_vk_from_name(key)
    local hid_value = type(key) == "string" and keyboard_key_values[key] or nil
    if hid_value == nil and vk == nil then
        report_status("Cannot bind " .. key_label .. ": this key is not supported by either keyboard reader.")
        show_toast("Unsupported key. Press another key.", 3.0, 0xFFFFAA00)
        return false
    end

    local handle, open_error = io.open(HOTKEY_CONFIG_FILE, "w")
    if handle == nil then
        set_error("Cannot save capture hotkey: " .. tostring(open_error))
        return false
    end

    local write_ok, write_error = handle:write(key_label, "\n")
    handle:close()
    if write_ok == nil then
        set_error("Cannot write capture hotkey: " .. tostring(write_error))
        return false
    end

    state.capture_key_name = key_label
    state.capture_vk = vk
    state.capture_key_down = true
    state.previous_capture_key_down = true
    state.binding_capture_key = false
    state.capture_key_binding_armed = false
    state.rejected_binding_vk = nil
    local description = key_label .. (vk ~= nil and (" (VK " .. tostring(vk) .. ")") or " (game HID)")
    report_status("Capture hotkey saved as " .. description .. ".")
    show_toast("[HOTKEY SAVED] " .. description, 4.0, 0xFF00FFFF)
    return true
end

local function begin_capture_key_binding()
    if state.recording then
        report_status("Stop the current capture before changing its hotkey.")
        show_toast("Stop capture before rebinding the key.", 3.0, 0xFFFFAA00)
        return
    end

    state.binding_capture_key = true
    state.capture_key_binding_armed = false
    state.rejected_binding_vk = nil
    report_status("Hotkey binding: release all keys, then press one unused key.")
    show_toast("[HOTKEY] Release all keys, then press an unused key", 8.0, 0xFF00FFFF)
end

local function cancel_capture_key_binding()
    state.binding_capture_key = false
    state.capture_key_binding_armed = false
    state.rejected_binding_vk = nil
    report_status("Hotkey binding cancelled. Current key: " .. key_name(state.capture_key_name) .. ".")
end

local function update_capture_key_binding_win32()
    if not state.binding_capture_key then
        return false
    end

    local ok, key = pcall(function()
        return reframework:get_first_key_down()
    end)
    if not ok then
        handle_callback_error("Hotkey binding input read failed", key)
        return true
    end

    if key == nil then
        state.capture_key_binding_armed = true
        state.rejected_binding_vk = nil
        return true
    end

    if not state.capture_key_binding_armed then
        return true
    end

    if is_bindable_key(key) then
        state.last_keyboard_key = key_name(key)
        save_capture_key(key_name(key))
    elseif state.rejected_binding_vk ~= key then
        state.rejected_binding_vk = key
        report_status("That key is reserved; press a different unused key.")
        show_toast("Reserved key. Press a different unused key.", 3.0, 0xFFFFAA00)
        state.capture_key_binding_armed = false
    end
    return true
end

local function read_hid_key(device, key_name_to_read)
    local key_value = keyboard_key_values[key_name_to_read]
    if device == nil or key_value == nil then
        return false, false
    end

    local ok, down = pcall(function()
        return device:call("isDown", key_value)
    end)
    return ok, ok and not not down or false
end

local function poll_native_keyboard()
    if keyboard_singleton == nil or keyboard_type == nil or keyboard_key_type == nil then
        state.keyboard_backend = "Win32 fallback (game HID unavailable)"
        state.native_keyboard_ready = false
        return false
    end

    local device_ok, device = pcall(function()
        return sdk.call_native_func(keyboard_singleton, keyboard_type, "get_Device")
    end)
    if not device_ok or device == nil then
        state.keyboard_backend = "Win32 fallback (no game keyboard device)"
        state.native_keyboard_ready = false
        return false
    end

    local capture_ok, capture_down = read_hid_key(device, state.capture_key_name)
    local f8_ok, f8_down = read_hid_key(device, "F8")
    if not capture_ok or not f8_ok then
        state.keyboard_backend = "Win32 fallback (game HID key read failed)"
        state.native_keyboard_ready = false
        return false
    end

    state.native_keyboard_device = device
    state.keyboard_backend = "via.hid.Keyboard + Win32"
    state.native_keyboard_ready = true
    state.f9_key_down = select(2, read_hid_key(device, "F9"))
    state.f8_key_down = f8_down
    state.capture_key_down = capture_down

    if state.binding_capture_key then
        local any_key_down = false
        local newly_pressed_key = nil
        for _, candidate_name in ipairs(bindable_keyboard_key_names) do
            local key_ok, key_down = read_hid_key(device, candidate_name)
            if key_ok then
                local was_down = state.native_key_previous[candidate_name] or false
                if key_down then
                    any_key_down = true
                    if not was_down and newly_pressed_key == nil then
                        newly_pressed_key = candidate_name
                    end
                end
                state.native_key_previous[candidate_name] = key_down
            end
        end

        if not any_key_down then
            state.capture_key_binding_armed = true
            state.rejected_binding_vk = nil
            state.last_keyboard_key = "waiting for key"
        elseif state.capture_key_binding_armed and newly_pressed_key ~= nil then
            state.last_keyboard_key = newly_pressed_key
            if is_bindable_hid_key(newly_pressed_key) then
                save_capture_key(newly_pressed_key)
            else
                state.rejected_binding_vk = keyboard_key_values[newly_pressed_key]
                state.capture_key_binding_armed = false
                report_status("That key is reserved; press a different unused key.")
                show_toast("Reserved key. Press a different unused key.", 3.0, 0xFFFFAA00)
            end
        end
    end

    return true
end

re.on_application_entry("UpdateHID", function()
    local ok, err = pcall(poll_native_keyboard)
    if not ok then
        state.keyboard_backend = "Win32 fallback (game HID error)"
        state.native_keyboard_ready = false
        if tostring(err) ~= "nil" then
            state.last_keyboard_key = "HID error: " .. tostring(err)
        end
    end
    if state.keyboard_backend_logged ~= state.keyboard_backend then
        state.keyboard_backend_logged = state.keyboard_backend
        report_status("Keyboard input source: " .. state.keyboard_backend .. ".")
    end

    local gamepad_ok, gamepad_error = pcall(poll_native_gamepad)
    if not gamepad_ok then
        state.gamepad_backend = "Gamepad polling error"
        state.gamepad_snapshot = {
            available = false,
            buttons = {},
            pressed = state.gamepad_pending_pressed,
            released = state.gamepad_pending_released,
        }
        state.gamepad_previous_buttons = {}
        if tostring(gamepad_error) ~= "nil" then
            state.gamepad_backend = state.gamepad_backend .. ": " .. tostring(gamepad_error)
        end
    end
    if state.gamepad_backend_logged ~= state.gamepad_backend then
        state.gamepad_backend_logged = state.gamepad_backend
        report_status("Controller input source: " .. state.gamepad_backend .. ".")
    end
end)

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

local function csv_bool_number(value)
    if value == nil then
        return ""
    end
    return value and 1 or 0
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

-- `math.asin` is guarded the same way `math.atan2` is above.  Y-up, so pitch is
-- the elevation of `forward` above the XZ plane.
local asin = math.asin

local function forward_pitch_degrees(forward)
    if forward == nil or asin == nil then
        return nil
    end

    local vertical = forward.y
    if vertical > 1.0 then
        vertical = 1.0
    elseif vertical < -1.0 then
        vertical = -1.0
    end
    return math.deg(asin(vertical))
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

-- The camera is a `via.Camera` component on the camera's own GameObject, so its
-- orientation is two hops away through that GameObject's Transform -- the same
-- two hops the player path already uses.  `sdk.get_primary_camera()` is
-- REFramework's own resolver for it (internally via.SceneManager -> get_MainView
-- -> get_PrimaryCamera), so there is no reason to re-walk that chain in Lua.
--
-- A failure is *not* a capture failure: the columns go empty and the capture
-- keeps running, exactly like the facing columns.  Unlike the player's
-- `rotation_accessor` it is retried rather than latched, because the primary
-- camera really is absent while the scene loads.
local CAMERA_METHOD_KEYWORDS = {
    "rotation", "position", "transform", "matrix", "gameobject", "joint", "parent", "fov",
}

-- On failure, name the methods the object actually has.  TDB method names cannot
-- be enumerated offline, so this turns `camera_unavailable` from a dead end into
-- a list of spellings to try -- without spending another recording run.
local function camera_method_hint(object)
    local type_definition = safe_object_type(object)
    if type_definition == nil then
        return "<no type>"
    end

    local names = {}
    local ok_methods, methods = pcall(function()
        return type_definition:get_methods()
    end)
    if ok_methods and methods ~= nil then
        for _, method in ipairs(methods) do
            local method_name = tostring(method:get_name())
            local lower_name = string.lower(method_name)
            for _, keyword in ipairs(CAMERA_METHOD_KEYWORDS) do
                if string.find(lower_name, keyword, 1, true) ~= nil then
                    names[#names + 1] = method_name
                    break
                end
            end
            if #names >= 24 then
                break
            end
        end
    end

    if #names == 0 then
        return "<no method matched " .. table.concat(CAMERA_METHOD_KEYWORDS, "/") .. ">"
    end
    return table.concat(names, " ")
end

local function resolve_primary_camera()
    -- Deliberately re-fetched every sample instead of cached: the recorder spans
    -- scene loads, and holding a managed camera reference across one risks a
    -- dangling pointer.  The lookup is three TDB calls -- negligible against the
    -- per-sample motion-layer reads that already happen.
    local ok, camera = pcall(sdk.get_primary_camera)

    if not ok then
        return nil, "sdk.get_primary_camera() raised: " .. tostring(camera)
    end
    if camera == nil then
        return nil, "sdk.get_primary_camera() returned nil"
    end

    if not state.camera_resolved_logged then
        state.camera_resolved_logged = true
        -- Name the type once per capture: it is the evidence for *which* camera
        -- the rest of the columns came from (a demo/photo-mode camera would say
        -- so, and its Transform is not the one we want).
        local _, type_name = safe_object_type(camera)
        write_event(
            "camera_accessor_resolved",
            "primary camera is " .. tostring(type_name) .. " via sdk.get_primary_camera"
        )
    end

    return camera, nil
end

local function read_camera_rotation(transform)
    if state.camera_rotation_accessor == false then
        return nil, nil
    end

    if state.camera_rotation_accessor == nil then
        for _, candidate in ipairs(ROTATION_ACCESSORS) do
            local ok, value = pcall(function()
                return transform:call(candidate)
            end)
            if ok and read_quaternion(value) ~= nil then
                state.camera_rotation_accessor = candidate
                break
            end
        end
        if state.camera_rotation_accessor == nil then
            state.camera_rotation_accessor = false
            return nil, "no readable rotation accessor on the camera Transform"
        end
    end

    local ok, value = pcall(function()
        return transform:call(state.camera_rotation_accessor)
    end)
    if not ok then
        return nil, "camera rotation read failed via " .. tostring(state.camera_rotation_accessor)
    end
    return read_quaternion(value), nil
end

-- The camera looks down its own local **-Z**, unlike the hunter, whose forward
-- is local +Z.  Measured on a real capture (20260919_005435), not assumed:
-- rotating local -Z against the geometric truth -- the camera->hunter vector,
-- the camera sitting ~7 m behind and above the player -- gives a median error of
-- 0.76 deg with 99.8% of samples inside 5 deg, while local +Z is 179 deg off,
-- i.e. exactly reversed.  Only the derived columns are affected; `cam_q*` stays
-- the raw quaternion, so even a 1.7.0 capture can be corrected offline.
local function camera_forward(quaternion)
    local forward = quaternion_forward(quaternion)
    if forward == nil then
        return nil
    end
    return { x = -forward.x, y = -forward.y, z = -forward.z }
end

local function read_primary_camera()
    local camera, camera_error = resolve_primary_camera()
    if camera == nil then
        return nil, camera_error
    end

    local ok, result, detail = pcall(function()
        local camera_game_object = camera:call("get_GameObject")
        if camera_game_object == nil then
            return nil, "primary camera has no GameObject | " .. camera_method_hint(camera)
        end

        local transform = camera_game_object:call("get_Transform")
        if transform == nil then
            return nil, "primary camera's GameObject has no Transform | "
                .. camera_method_hint(camera_game_object)
        end

        local position = transform:call("get_Position")
        if position == nil then
            return nil, "camera Transform has no Position | " .. camera_method_hint(transform)
        end

        local quaternion, rotation_error = read_camera_rotation(transform)
        local forward = camera_forward(quaternion)

        return {
            x = position.x,
            y = position.y,
            z = position.z,
            quaternion = quaternion,
            forward = forward,
            yaw = forward_yaw_degrees(forward),
            pitch = forward_pitch_degrees(forward),
            rotation_error = rotation_error,
        }, nil
    end)

    if not ok then
        return nil, "camera read raised: " .. tostring(result)
    end
    return result, detail
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
    if ok then return value end
    return nil
end

-- BEGIN DIRECT MOTION READER (also executed by the isolated Lua tests).
-- Read APIs reference: alphazolam/EMV-Engine, EMV Engine/init.lua and
-- Enhanced Model Viewer.lua (get_Layer / get_HighestWeightMotionNode /
-- getMotionNode(0) / get_Frame / findMotionBank). No EMV dependency or setters.
local animation_capture = (function()
    local R = { file = nil, rows = 0, valid_samples = 0, status = "[ANIM] Not checked",
        hooks = {}, hook_count = 0, layer_addresses = {}, requests = {}, dropped_requests = 0 }
    local MAX_LAYERS, MAX_NODES, MAX_OBJECTS = 16, 16, 32
    local columns = { "sample_index", "capture_time_s", "render_callback", "action_state_id",
        "component", "component_address", "layer", "layer_address", "node_index", "node_address",
        "highest_weight", "bank_id", "bank_type", "motion_id", "motion_name", "motion_list",
        "node_frame", "node_end_frame", "node_speed", "node_weight",
        "layer_frame", "layer_end_frame", "layer_speed", "layer_weight", "motion_play_speed",
        "frame", "frame_source", "frame_units", "node_enumeration", "read_status" }
    local methods, catalogs, resources, previous = {}, {}, {}, {}
    local pending_methods = {}

    local function call(object, method, ...)
        if object == nil then return nil end
        local ok, value = pcall(object.call, object, method, ...)
        if ok then return value end
        return nil
    end
    local function address(object)
        if object == nil then return "" end
        local ok, value = pcall(object.get_address, object)
        return ok and tostring(value) or ""
    end
    local function typename(object)
        local td
        -- get_type_definition is a REFramework wrapper API, not an engine method.
        if object ~= nil then
            local ok, value = pcall(object.get_type_definition, object)
            if ok then td = value end
        end
        local ok, name = pcall(function() return td:get_full_name() end)
        return ok and tostring(name) or "<unknown>", td
    end
    local function number(value)
        if type(value) ~= "number" or value ~= value or math.abs(value) == math.huge then return nil end
        return value
    end
    local function property(object, label, candidates)
        local key = typename(object) .. ":" .. label
        local selected = methods[key]
        if selected == false then return nil end
        if selected then return call(object, selected) end
        for _, name in ipairs(candidates) do
            local value = call(object, name)
            if value ~= nil then methods[key] = name; return value end
        end
        methods[key] = false
        return nil
    end
    local function elements(array, limit)
        if array == nil then return nil end
        if type(array) == "table" then
            local result = {}
            for i = 1, math.min(#array, limit) do result[i] = array[i] end
            return result
        end
        local ok, result = pcall(array.get_elements, array)
        if ok and type(result) == "table" then
            local bounded = {}
            for i = 1, math.min(#result, limit) do bounded[i] = result[i] end
            return bounded
        end
        return nil
    end
    local function catalog(object)
        local name, td = typename(object)
        if catalogs[name] or td == nil then return end
        catalogs[name] = true
        local parts = { name }
        for _ = 1, 4 do
            if td == nil then break end
            local ok, list = pcall(td.get_methods, td)
            if ok and list then
                for _, method in ipairs(list) do
                    local mok, text = pcall(method.get_name, method)
                    if mok and (tostring(text):find("Motion") or tostring(text):find("Frame") or
                        tostring(text):find("Weight") or tostring(text):find("Speed")) then
                        parts[#parts + 1] = tostring(text)
                    end
                end
            end
            local fok, fields = pcall(td.get_fields, td)
            if fok and fields then
                for i, field in ipairs(fields) do
                    if i > 128 then break end
                    local nok, text = pcall(field.get_name, field)
                    if nok and (tostring(text):lower():find("frame") or tostring(text):lower():find("weight") or
                        tostring(text):lower():find("motion") or tostring(text):lower():find("time")) then
                        parts[#parts + 1] = "field:" .. tostring(text)
                    end
                end
            end
            local pok, parent = pcall(td.get_parent_type, td)
            td = pok and parent or nil
        end
        write_event("animation_reader_capabilities", table.concat(parts, ";"))
    end
    local function write_row(row)
        local values = {}
        for _, name in ipairs(columns) do values[#values + 1] = csv_text(row[name]) end
        assert(R.file:write(table.concat(values, ",") .. "\n"), "Animation CSV write failed")
        R.rows = R.rows + 1
    end
    local function resolve_components(game_object)
        local result, queue, seen = {}, { game_object }, {}
        local motion_type = sdk.typeof("via.motion.Motion")
        local index = 1
        while index <= #queue and index <= MAX_OBJECTS do
            local go = queue[index]; index = index + 1
            local key = address(go)
            if go ~= nil and not seen[key] then
                seen[key] = true
                local motion = call(go, "getComponent(System.Type)", motion_type)
                if motion ~= nil then
                    result[#result + 1] = { motion = motion, name = tostring(call(go, "get_Name") or "hunter") }
                end
                local transform = call(go, "get_Transform")
                local child = call(transform, "get_Child")
                local siblings = {}
                while child ~= nil and #queue < MAX_OBJECTS and not siblings[address(child)] do
                    siblings[address(child)] = true
                    local child_go = call(child, "get_GameObject")
                    if child_go then queue[#queue + 1] = child_go end
                    child = call(child, "get_Next")
                end
            end
        end
        return result
    end
    local function motion_list(motion, bank_id, bank_type)
        if bank_id == nil or bank_type == nil then return nil end
        local key = address(motion) .. ":" .. tostring(bank_id) .. ":" .. tostring(bank_type)
        if resources[key] ~= nil then return resources[key] or nil end
        local bank = call(motion, "findMotionBank(System.UInt32, System.UInt32)", bank_id, bank_type)
        local resource = call(bank, "get_MotionList")
        local text = call(resource, "ToString()")
        -- A missing resource may be transient while loading; do not cache failure.
        if text ~= nil then resources[key] = tostring(text) end
        return text
    end
    local function observe_requests(layer)
        local _, td = typename(layer)
        if td == nil or sdk.hook == nil or sdk.to_float == nil then return end
        for _, signature in ipairs({
            "changeMotion(System.UInt32, System.UInt32, System.Single)",
            "changeMotion(System.UInt32, System.UInt32, System.Single, System.Single, via.motion.InterpolationMode, via.motion.InterpolationCurve)"
        }) do
            local key = tostring(td) .. signature
            if pending_methods[key] == nil then
                pending_methods[key] = false
                local ok, method = pcall(td.get_method, td, signature)
                if ok and method then
                    local aok, method_address = pcall(method.get_address, method)
                    local hook_key = aok and tostring(method_address) or key
                    if not R.hooks[hook_key] then
                        local hooked = pcall(sdk.hook, method, function(args)
                            -- Observe only known hunter layers, never skip or replace game calls.
                            if state.recording and R.file then
                                pcall(function()
                                    local object = sdk.to_managed_object(args[2])
                                    local owner = R.layer_addresses[address(object)]
                                    if owner ~= nil then
                                        if #R.requests >= 256 then R.dropped_requests = R.dropped_requests + 1; return end
                                        R.requests[#R.requests + 1] = {
                                            owner = owner, after_sample = state.sample_count,
                                            bank = sdk.to_int64(args[3]), motion = sdk.to_int64(args[4]),
                                            frame = number(sdk.to_float(args[5])), signature = signature }
                                    end
                                end)
                            end
                            return sdk.PreHookResult.CALL_ORIGINAL
                        end, function(retval) return retval end)
                        if hooked then R.hooks[hook_key] = true; R.hook_count = R.hook_count + 1 end
                    end
                end
            end
        end
    end
    local function nodes_for(layer)
        local nodes, seen = {}, {}
        local highest = call(layer, "get_HighestWeightMotionNode")
        local highest_address = address(highest)
        local function add(node, index)
            local key = address(node)
            if node and key ~= "" and not seen[key] then
                seen[key] = true; nodes[#nodes + 1] = { object = node, index = index, address = key }
            end
        end
        -- Never probe unbounded native indices. Without a real count/array only
        -- the documented index0 + highest node are read, explicitly labelled partial.
        local array = property(layer, "nodes", { "get_MotionNodes", "get_MotionNode" })
        local list = elements(array, MAX_NODES)
        local mode = "slot0_and_highest_only"
        if list then
            mode = #list >= MAX_NODES and "array_capped" or "array"
            for i, node in ipairs(list) do add(node, i - 1) end
        else
            local count = number(property(layer, "node_count", {
                "get_MotionNodeCount", "getMotionNodeCount", "get_MotionNodeNum", "getMotionNodeNum" }))
            if count and count >= 0 and count == math.floor(count) then
                mode = count > MAX_NODES and "count_capped" or "count"
                for i = 0, math.min(count, MAX_NODES) - 1 do add(call(layer, "getMotionNode", i), i) end
            else
                add(call(layer, "getMotionNode", 0), 0)
            end
        end
        add(highest, "highest")
        return nodes, highest_address, mode
    end
    local function flush_requests()
        for _, request in ipairs(R.requests) do
            write_event("animation_change_requested", request.owner .. ";bank=" .. tostring(request.bank) ..
                ";motion=" .. tostring(request.motion) .. ";requested_start_frame=" .. tostring(request.frame) ..
                ";after_sample=" .. tostring(request.after_sample) .. ";signature=" .. request.signature)
        end
        R.requests = {}
    end

    function R.start(path)
        local file, err = io.open(path, "w")
        if not file then return nil, err end
        R.file, R.rows, R.valid_samples, R.requests, R.dropped_requests = file, 0, 0, {}, 0
        R.layer_addresses, resources, previous, catalogs = {}, {}, {}, {}
        R.status, R.last_status, R.last_error = "[ANIM CHECK] Waiting for real animation frames", nil, nil
        R.file:write(table.concat(columns, ",") .. "\n")
        return true
    end
    function R.sample(sample_index, time, action_id)
        if not R.file then return end
        local _, game_object = get_master_player_and_game_object()
        local components = game_object and resolve_components(game_object) or {}
        local valid, known_layers, last = false, {}, nil
        for _, component in ipairs(components) do
            local motion = component.motion
            local layers = elements(call(motion, "get_Layer"), MAX_LAYERS)
            if not layers then
                layers = {}; local layer = call(motion, "getLayer", 0)
                if layer then layers[1] = layer end
            end
            catalog(motion)
            for layer_index, layer in ipairs(layers) do
                catalog(layer)
                local layer_address = address(layer)
                known_layers[layer_address] = component.name .. ":L" .. tostring(layer_index - 1)
                R.layer_addresses[layer_address] = known_layers[layer_address]
                observe_requests(layer)
                local nodes, highest, mode = nodes_for(layer)
                local lf, le = number(call(layer, "get_Frame")), number(call(layer, "get_EndFrame"))
                for _, node in ipairs(nodes) do
                    local n = node.object
                    catalog(n)
                    local bank, id = number(call(n, "get_MotionBankID")), number(call(n, "get_MotionID"))
                    local bank_type = number(call(n, "get_MotionBankType"))
                    local frame = number(property(n, "frame", { "get_Frame", "get_MotionFrame" }))
                    local ending = number(property(n, "end", { "get_EndFrame", "get_MotionEndFrame" }))
                    local frame_source = frame ~= nil and "node" or "unavailable"
                    -- A layer clock is not each blend participant's own clock.
                    -- Only alias it when the engine enumerated exactly one node.
                    if frame == nil and #nodes == 1 and node.index == 0 and (mode == "count" or mode == "array") then
                        frame, frame_source = lf, "single_node_layer"
                    end
                    local usable = bank ~= nil and id ~= nil and frame ~= nil and
                        ((ending or le or 0) > 1) and not (bank == 0 and id == 1 and (ending or le or 0) <= 1)
                    local row = { sample_index = sample_index, capture_time_s = time,
                        render_callback = state.frame_callback_count, action_state_id = action_id,
                        component = component.name, component_address = address(motion),
                        layer = layer_index - 1, layer_address = layer_address,
                        node_index = node.index, node_address = node.address, highest_weight = node.address == highest and 1 or 0,
                        bank_id = bank, bank_type = bank_type, motion_id = id,
                        motion_name = call(n, "get_MotionName"), motion_list = motion_list(motion, bank, bank_type),
                        node_frame = number(property(n, "frame", { "get_Frame", "get_MotionFrame" })),
                        node_end_frame = ending,
                        node_speed = number(property(n, "speed", { "get_Speed", "get_PlaySpeed" })),
                        node_weight = number(property(n, "weight", { "get_Weight", "get_BlendWeight" })),
                        layer_frame = lf, layer_end_frame = le, layer_speed = number(call(layer, "get_Speed")),
                        layer_weight = number(property(layer, "weight", { "get_Weight", "get_BlendWeight" })),
                        motion_play_speed = number(call(motion, "get_PlaySpeed")), frame = frame,
                        frame_source = frame_source, frame_units = "engine_native_unscaled",
                        node_enumeration = mode, read_status = usable and "readable" or "incomplete_or_placeholder" }
                    write_row(row)
                    if usable then valid, last = true, row end
                    local track = layer_address .. ":" .. tostring(node.index)
                    local identity = tostring(bank) .. ":" .. tostring(id) .. ":" .. node.address
                    if previous[track] ~= identity then
                        write_event("animation_first_observed", known_layers[layer_address] .. ";bank=" .. tostring(bank) ..
                            ";motion=" .. tostring(id) .. ";frame=" .. tostring(frame) .. ";source=" .. frame_source ..
                            ";not_a_requested_start_frame=true")
                        previous[track] = identity
                    end
                end
            end
        end
        R.layer_addresses = known_layers
        flush_requests()
        if valid then R.valid_samples = R.valid_samples + 1 end
        local status = valid and "readable" or "unavailable"
        R.status = valid and string.format("[ANIM READ] %s L%d %s:%s f=%.3f (%s)",
            last.component, last.layer, tostring(last.bank_id), tostring(last.motion_id), last.frame, last.frame_source) or
            "[ANIM WARNING] Real clip/frame unavailable; do not infer entry frames"
        if R.last_status ~= status then
            R.last_status = status
            write_event("animation_reader_status", status .. ";components=" .. tostring(#components) ..
                ";hooks=" .. tostring(R.hook_count))
        end
        if sample_index % 120 == 0 then R.file:flush() end
    end
    function R.stop()
        if not R.file then return end
        flush_requests()
        write_event("animation_reader_summary", "rows=" .. tostring(R.rows) .. ";readable_samples=" ..
            tostring(R.valid_samples) .. ";dropped_requests=" .. tostring(R.dropped_requests))
        R.file:flush(); R.file:close(); R.file = nil
        R.layer_addresses, R.requests, previous = {}, {}, {}
    end
    return R
end)()
-- END DIRECT MOTION READER.

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

    write_event("capture_health", "stopped; valid=" .. tostring(state.health_valid_sample_count))
    animation_capture.stop()
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
    if state.health_valid_sample_count == 0 then
        show_toast("[REC WARNING] NO VALID HUNTER + MOTION SAMPLES", 5.0, 0xFF00AAFF)
    elseif animation_capture.valid_samples == 0 then
        show_toast("[SAVED / ANIM WARNING] No usable real animation frames", 5.0, 0xFF00AAFF)
    else
        show_toast("[SAVED] " .. tostring(state.sample_count) .. " SAMPLES WRITTEN", 2.5, 0xFF00FF00)
    end
end

local function start_capture(trigger)
    if state.recording then
        return
    end

    state.serial = state.serial + 1
    local timestamp = os.date("%Y%m%d_%H%M%S")
    local basename = string.format("mhrise_%s_%02d", timestamp, state.serial)
    local samples_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_samples.csv"
    local events_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_events.csv"
    local animation_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_animation.csv"

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
    local animation_ok, animation_error = animation_capture.start(animation_path)
    if not animation_ok then
        samples_file:close()
        events_file:close()
        set_error("Cannot create " .. animation_path .. ": " .. tostring(animation_error))
        return
    end

    state.recording = true
    state.samples_file = samples_file
    state.events_file = events_file
    state.sample_count = 0
    state.health_state = "checking"
    state.health_message = "Checking hunter + motion samples..."
    state.health_started_at = os.time()
    state.health_last_valid_at = nil
    state.health_valid_sample_count = 0
    state.health_problem = nil
    state.event_count = 0
    state.capture_time = 0.0
    state.previous_sample = nil
    state.previous_phase = "unknown"
    state.previous_clock = os.clock()
    state.previous_uptime_second = nil
    state.last_error = nil
    state.rotation_accessor = nil
    state.camera_rotation_accessor = nil
    state.camera_resolved_logged = false
    state.camera_logged_error = nil
    state.gamepad_status_logged = nil
    state.gamepad_pending_pressed = {}
    state.gamepad_pending_released = {}
    state.gamepad_snapshot.pressed = state.gamepad_pending_pressed
    state.gamepad_snapshot.released = state.gamepad_pending_released

    local sample_header = {
        "sample_index", "capture_time_s", "delta_time_s", "time_source",
        "world_x", "world_y", "world_z",
        "velocity_x", "velocity_y", "velocity_z",
        "acceleration_x", "acceleration_y", "acceleration_z", "vertical_phase",
        "rot_qx", "rot_qy", "rot_qz", "rot_qw",
        "forward_x", "forward_y", "forward_z", "facing_yaw_deg",
        "cam_x", "cam_y", "cam_z",
        "cam_qx", "cam_qy", "cam_qz", "cam_qw",
        "cam_forward_x", "cam_forward_y", "cam_forward_z",
        "cam_yaw_deg", "cam_pitch_deg",
        "input_gamepad_available",
        "input_gamepad_axis_l_x", "input_gamepad_axis_l_y", "input_gamepad_axis_l_z",
        "input_gamepad_axis_r_x", "input_gamepad_axis_r_y", "input_gamepad_axis_r_z",
        "motion_layer_count",
        "player_motion_old_id",
    }
    for _, button_name in ipairs(gamepad_button_names) do
        local column_name = "input_gamepad_button_" .. gamepad_button_column_name(button_name)
        table.insert(sample_header, column_name .. "_down")
        table.insert(sample_header, column_name .. "_pressed")
        table.insert(sample_header, column_name .. "_released")
    end
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
    state.sample_column_count = #sample_header
    events_file:write("event_index,capture_time_s,event,vertical_phase,details\n")
    write_event("capture_started", (trigger or "unknown trigger") .. "; raw hunter + primary camera world Transform capture")
    write_event("gamepad_input_schema", state.gamepad_backend .. "; buttons=" .. table.concat(gamepad_button_names, ","))
    write_event("animation_reader_schema", "v1.13.0; file=" .. animation_path ..
        ";frames=engine_native_unscaled;legacy_layers_are_not_attack_frame_truth")
    samples_file:flush()
    events_file:flush()
    report_status("Capture started by " .. (trigger or "unknown trigger") .. ": " .. samples_path)
    show_toast("[REC] TRAJECTORY CAPTURE STARTED", 2.5, 0xFF00FFFF)
end

local function toggle_capture(trigger)
    if state.recording then
        stop_capture(trigger or "unknown trigger")
    else
        start_capture(trigger)
    end
end

local function record_sample()
    local position, position_error = get_master_player_position()
    if position == nil then
        state.health_problem = position_error or "hunter position unavailable"
        if state.last_error ~= position_error then
            state.last_error = position_error
            write_event("player_unavailable", position_error)
            report_status("Waiting for player: " .. position_error)
        end
        return
    end

    local delta_time, time_source = read_delta_time()
    state.capture_time = state.capture_time + delta_time

    if state.gamepad_status_logged ~= state.gamepad_backend then
        state.gamepad_status_logged = state.gamepad_backend
        write_event("gamepad_input_status", state.gamepad_backend)
    end

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

    -- Same contract as facing: a camera failure empties its columns and is
    -- logged once per distinct reason, never interrupting the capture.
    local camera, camera_error = read_primary_camera()
    if camera ~= nil then
        if camera.rotation_error ~= nil and state.last_error ~= camera.rotation_error then
            state.last_error = camera.rotation_error
            write_event("camera_facing_unavailable", camera.rotation_error)
        end
    elseif camera_error ~= nil and state.camera_logged_error ~= camera_error then
        state.camera_logged_error = camera_error
        write_event("camera_unavailable", camera_error)
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
        csv_number(camera and camera.x),
        csv_number(camera and camera.y),
        csv_number(camera and camera.z),
        csv_number(camera and camera.quaternion and camera.quaternion.x),
        csv_number(camera and camera.quaternion and camera.quaternion.y),
        csv_number(camera and camera.quaternion and camera.quaternion.z),
        csv_number(camera and camera.quaternion and camera.quaternion.w),
        csv_number(camera and camera.forward and camera.forward.x),
        csv_number(camera and camera.forward and camera.forward.y),
        csv_number(camera and camera.forward and camera.forward.z),
        csv_number(camera and camera.yaw),
        csv_number(camera and camera.pitch),
        csv_bool_number(state.gamepad_snapshot.available),
        csv_number(state.gamepad_snapshot.axis_l_x),
        csv_number(state.gamepad_snapshot.axis_l_y),
        csv_number(state.gamepad_snapshot.axis_l_z),
        csv_number(state.gamepad_snapshot.axis_r_x),
        csv_number(state.gamepad_snapshot.axis_r_y),
        csv_number(state.gamepad_snapshot.axis_r_z),
        csv_number(motion_layers.layer_count),
        csv_number(captured_motion_value(motion_layers.player_motion_old_id_ok, motion_layers.player_motion_old_id)),
    }
    for _, button_name in ipairs(gamepad_button_names) do
        table.insert(row, csv_bool_number(state.gamepad_snapshot.buttons[button_name]))
        table.insert(row, csv_bool_number(state.gamepad_snapshot.pressed[button_name]))
        table.insert(row, csv_bool_number(state.gamepad_snapshot.released[button_name]))
    end
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
    assert(#row == state.sample_column_count, "Sample CSV column count mismatch")
    local written, write_error = state.samples_file:write(table.concat(row, ",") .. "\n")
    if written == nil then error("Sample CSV write failed: " .. tostring(write_error)) end
    local animation_ok, animation_error = pcall(animation_capture.sample,
        state.sample_count, state.capture_time,
        captured_motion_value(motion_layers.player_motion_old_id_ok, motion_layers.player_motion_old_id))
    if not animation_ok then
        animation_capture.status = "[ANIM ERROR] Read failed; trajectory capture continues"
        animation_capture.last_status = "error"
        if animation_capture.last_error ~= tostring(animation_error) then
            animation_capture.last_error = tostring(animation_error)
            write_event("animation_reader_error", animation_capture.last_error)
        end
    end
    local first_layer = motion_layers.layers[1] or {}
    local motion_available = (motion_layers.player_motion_old_id_ok and motion_layers.player_motion_old_id ~= nil) or
        (first_layer.motion_ok and first_layer.motion_id ~= nil)
    if motion_error == nil and motion_available then
        state.health_valid_sample_count = state.health_valid_sample_count + 1
        state.health_last_valid_at = os.time()
        state.health_problem = nil
    else
        state.health_problem = motion_error or "motion ID unavailable"
    end

    -- Preserve HID edges if UpdateHID ran more than once between rendered
    -- samples, then consume them exactly once in this CSV row.
    state.gamepad_pending_pressed = {}
    state.gamepad_pending_released = {}
    state.gamepad_snapshot.pressed = state.gamepad_pending_pressed
    state.gamepad_snapshot.released = state.gamepad_pending_released

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

local function update_capture_health()
    if not state.recording then return end
    local now = os.time()
    local last_valid = state.health_last_valid_at or state.health_started_at
    local next_state, message
    if state.health_valid_sample_count >= 3 and now - last_valid < CAPTURE_HEALTH_TIMEOUT_SECONDS then
        next_state, message = "healthy", "[REC OK] Hunter + motion samples verified"
    elseif now - last_valid >= CAPTURE_HEALTH_TIMEOUT_SECONDS then
        next_state, message = "stalled", "[REC WARNING] No valid hunter/motion data for 3s"
    else
        return
    end
    if next_state == state.health_state then return end
    state.health_state, state.health_message = next_state, message
    write_event("capture_health", next_state .. "; valid=" .. tostring(state.health_valid_sample_count) ..
        "; reason=" .. tostring(state.health_problem or "none"))
    state.samples_file:flush()
    state.events_file:flush()
    report_status(message)
    show_toast(message, 4.0, next_state == "healthy" and 0xFF00FF00 or 0xFF00AAFF)
end

local function draw_capture_feedback()
    local capture_key = key_name(state.capture_key_name)
    if state.recording then
        draw.filled_rect(20, 20, 730, 68, 0xB0000000)
        draw.outline_rect(20, 20, 730, 68, 0xFF0000FF)
        draw.text(
            "[REC] TRAJECTORY CAPTURE  |  " .. capture_key .. ": STOP  |  Samples: " .. tostring(state.sample_count),
            32,
            34,
            0xFF0000FF
        )
        draw.text(state.health_message, 32, 52, state.health_state == "stalled" and 0xFF00AAFF or 0xFFFFFFFF)
        draw.text(animation_capture.status, 32, 69,
            animation_capture.last_status == "readable" and 0xFF00FF00 or 0xFF00AAFF)
    elseif state.binding_capture_key then
        draw.filled_rect(20, 20, 530, 34, 0xA0000000)
        draw.outline_rect(20, 20, 530, 34, 0xFFFFAA00)
        local binding_hint = state.capture_key_binding_armed and "Press a key" or "Release all keys"
        draw.text("[HOTKEY] " .. binding_hint .. " | Detected: " .. state.last_keyboard_key, 30, 29, 0xFFFFAA00)
    else
        -- Keep a small ready indicator on screen after the startup toast fades.
        -- This distinguishes "script loaded" from "frame/draw callbacks alive".
        draw.filled_rect(20, 20, 500, 34, 0xA0000000)
        draw.outline_rect(20, 20, 500, 34, 0xFF00FFFF)
        draw.text("[REC READY v" .. SCRIPT_VERSION .. "] " .. capture_key .. ": " .. (state.capture_key_down and "DOWN" or "UP") ..
            " | Press key or click Recorder window", 30, 29, 0xFF00FFFF)
    end

    if state.toast_text ~= nil and os.clock() < state.toast_until then
        draw.filled_rect(20, 94, 730, 48, 0xC0000000)
        draw.outline_rect(20, 94, 730, 48, state.toast_color)
        draw.text(state.toast_text, 32, 105, state.toast_color)
    end
end

local function draw_quick_control_widget()
    -- REFramework permits standalone ImGui windows from on_frame when wrapped
    -- in begin_window/end_window. Keep this small control visible even while
    -- the main REFramework menu is closed, so recording never depends on a key.
    imgui.set_next_window_pos({ 20, 148 }, 4)
    imgui.set_next_window_size({ 540, 245 }, 4)
    local contents_visible = imgui.begin_window("MHGZ Recorder Control", nil, nil)
    if contents_visible then
        imgui.text("Recorder v" .. SCRIPT_VERSION)
        if state.recording then
            imgui.text("Recording: " .. tostring(state.sample_count) .. " samples")
            imgui.text(state.health_message)
            imgui.text(animation_capture.status)
            if imgui.button("STOP AND SAVE") then
                toggle_capture("quick recorder window")
            end
        else
            imgui.text("Ready - click below to record")
            if imgui.button("START RECORDING") then
                toggle_capture("quick recorder window")
            end
            if state.binding_capture_key then
                if imgui.button("CANCEL KEY BINDING") then
                    cancel_capture_key_binding()
                end
                imgui.text("Binding: release all keys, then press one")
                imgui.text("Last detected: " .. state.last_keyboard_key)
            elseif imgui.button("BIND CAPTURE KEY") then
                begin_capture_key_binding()
            end
        end
        imgui.text("Keyboard: " .. state.keyboard_backend)
        imgui.text("Controller: " .. state.gamepad_backend)
        local input = state.gamepad_snapshot
        if input ~= nil and input.available then
            imgui.text(string.format("LS %.2f, %.2f | RS %.2f, %.2f",
                input.axis_l_x or 0.0, input.axis_l_y or 0.0,
                input.axis_r_x or 0.0, input.axis_r_y or 0.0))
            local down_names = {}
            for _, button_name in ipairs(gamepad_button_names) do
                if input.buttons[button_name] then
                    table.insert(down_names, button_name)
                end
            end
            imgui.text("Buttons down: " .. (#down_names > 0 and table.concat(down_names, ",") or "none"))
        end
        imgui.text("Capture key: " .. key_name(state.capture_key_name) ..
            (state.capture_key_down and " [DOWN]" or " [UP]"))
        if not state.binding_capture_key then
            imgui.text("F9 remains optional; no main menu needed.")
        end
    end
    imgui.end_window()
end

local function on_frame_update()
    state.frame_callback_seen = true
    state.frame_callback_count = state.frame_callback_count + 1

    local is_binding_key = state.binding_capture_key and update_capture_key_binding_win32() or false

    local f9_ok, f9_down = pcall(function()
        return reframework:is_key_down(VK_F9)
    end)
    local f8_ok, f8_down = pcall(function()
        return reframework:is_key_down(VK_F8)
    end)
    local win32_capture_ok, win32_capture_down = false, false
    if state.capture_vk ~= nil then
        win32_capture_ok, win32_capture_down = pcall(function()
            return reframework:is_key_down(state.capture_vk)
        end)
    end

    state.f9_key_down = (state.native_keyboard_ready and state.f9_key_down) or (f9_ok and not not f9_down) or false
    local combined_f8_down = (state.native_keyboard_ready and state.f8_key_down) or (f8_ok and not not f8_down) or false
    local combined_capture_down = (state.native_keyboard_ready and state.capture_key_down) or
        (win32_capture_ok and not not win32_capture_down) or false
    local f8_pressed = not is_binding_key and combined_f8_down and not state.previous_f8_down
    local capture_pressed = not is_binding_key and combined_capture_down and not state.previous_capture_key_down
    state.previous_f8_down = combined_f8_down
    state.previous_capture_key_down = combined_capture_down
    state.capture_key_down = combined_capture_down

    if f8_pressed then
        write_motion_probe()
    end
    if capture_pressed and not is_binding_key then
        toggle_capture("hotkey " .. key_name(state.capture_key_name))
    end

    if state.recording then
        record_sample()
        update_capture_health()
    end

    draw_capture_feedback()
    draw_quick_control_widget()
end

re.on_frame(function()
    local ok, err = pcall(on_frame_update)
    if not ok then
        handle_callback_error("Frame callback failed", err)
    else
        state.last_frame_error = nil
    end
end)

re.on_draw_ui(function()
    local capture_key = key_name(state.capture_key_name)
    imgui.text(SCRIPT_NAME .. " v" .. SCRIPT_VERSION)
    imgui.text(state.recording and "Status: RECORDING" or "Status: idle")
    imgui.text("Frame callback: " .. (state.frame_callback_seen and "RUNNING" or "NOT SEEN") ..
        " (" .. tostring(state.frame_callback_count) .. " frames)")
    imgui.text("Keyboard input source: " .. state.keyboard_backend)
    imgui.text("Controller input source: " .. state.gamepad_backend)
    imgui.text("Last detected key: " .. state.last_keyboard_key)
    imgui.text("F9 key state: " .. (state.f9_key_down and "DOWN" or "UP") ..
        " (Windows VK 0x78); F8: " .. (state.f8_key_down and "DOWN" or "UP"))
    local capture_key_code = state.capture_vk ~= nil and ("VK " .. tostring(state.capture_vk)) or "game HID"
    imgui.text("Capture key: " .. capture_key .. " (" .. capture_key_code .. ") - " ..
        (state.capture_key_down and "DOWN" or "UP"))
    imgui.text("If F9 is not detected, rebind the capture key here or use the button.")

    if imgui.button(state.recording and ("Stop capture (" .. capture_key .. ")") or
        ("Start capture (" .. capture_key .. ")")) then
        if state.binding_capture_key then
            cancel_capture_key_binding()
        end
        local ok, err = pcall(toggle_capture, "REFramework script UI button")
        if not ok then
            handle_callback_error("Capture button failed", err)
        end
    end
    if not state.recording then
        if state.binding_capture_key then
            imgui.text("Hotkey binding is active.")
            imgui.text("Release all keys, then press one unused key (F8, Escape and Insert are reserved).")
            imgui.text("Keyboard source: " .. state.keyboard_backend .. " | Last detected: " .. state.last_keyboard_key)
            if imgui.button("Cancel hotkey binding") then
                cancel_capture_key_binding()
            end
        elseif imgui.button("Bind capture hotkey") then
            begin_capture_key_binding()
        end
    end
    if imgui.button("Write motion probe (F8)") then
        local ok, err = pcall(write_motion_probe)
        if not ok then
            handle_callback_error("Probe button failed", err)
        end
    end

    imgui.text("Output: reframework/data/" .. OUTPUT_DIRECTORY)
    imgui.text("Samples: " .. tostring(state.sample_count))
    imgui.text("F9: samples.csv = trajectory/input + legacy behavior layers; animation.csv = actual Motion nodes.")
    imgui.text(animation_capture.status)
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

show_toast("Recorder ready. Click the floating Recorder window or press " .. key_name(state.capture_key_name) .. ".", 7.0, 0xFFFFFFFF)
report_status("Loaded. Click the floating Recorder window or press " .. key_name(state.capture_key_name) .. "; F8 writes a verbose motion probe.")
