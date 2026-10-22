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
local SCRIPT_VERSION = "1.21.0"
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
            player = player,
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

-- BEGIN HUNTER STAMINA READER: raw units only; no setters or guessed conversion.
local function read_hunter_stamina(player)
    local result = { available = false, source = "PlayerData._stamina/_staminaMax" }
    local ok, data = pcall(function() return player:call("get_PlayerData") end)
    if not ok or data == nil then
        result.error = "get_PlayerData unavailable"
        return result
    end
    local current_ok, current = pcall(function() return tonumber(data:get_field("_stamina")) end)
    local maximum_ok, maximum = pcall(function() return tonumber(data:get_field("_staminaMax")) end)
    local function finite(value)
        return type(value) == "number" and value == value and value > -math.huge and value < math.huge
    end
    if not current_ok or not maximum_ok or not finite(current) or not finite(maximum)
        or current < 0 or maximum <= 0 or current > maximum + 0.001 then
        result.error = "stamina fields missing, nonfinite, or outside 0..max"
        return result
    end
    result.available, result.current, result.maximum = true, current, maximum
    return result
end
-- END HUNTER STAMINA READER.

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

-- BEGIN AUDIO CAPABILITY PROBE: metadata only; no audio method calls or hooks.
local audio_probe = (function()
    local P = { status = "[AUDIO PROBE] F9 exports interface metadata",
        cache = nil, type_count = 0, candidate_count = 0, last_path = nil, auto_attempted = false }
    local targets = {
        "via.wwise.WwiseManager", "via.wwise.WwiseContainer", "via.wwise.WwiseContainerList",
        "via.wwise.WwiseEventParam", "via.wwise.WwiseTriggerParam",
        "via.wwise.WwiseSwitchParam", "via.wwise.WwiseStateParam",
        "via.wwiselib.WwiselibManager", "snow.wwise.SoundTracks", "snow.wwise.SoundTriggerTracks",
        "snow.wwise.WwisePlayerBaseParameters", "snow.wwise.WwisePlayerMonitoredParameters",
        "via.wwise.RequestInfo", -- postEvent argument layout: descriptors only.
    }
    local MAX_METHODS, MAX_FIELDS, MAX_PARAMS, MAX_PARENTS = 192, 64, 16, 2
    local function text(value)
        return tostring(value):gsub("[\r\n\t]", " ")
    end
    local function metadata(object, method)
        if object == nil then return nil, "missing descriptor" end
        local ok, value = pcall(function() return object[method](object) end)
        if ok then return value end
        return nil, text(value)
    end
    local function type_name(td)
        return metadata(td, "get_full_name") or "<unavailable>"
    end
    -- REFramework exposes std::vector metadata lists as sol userdata, not only tables.
    -- Read the container's own length and bounded 1-based indices; no engine getters.
    local function descriptor_list(value, limit)
        local kind = type(value)
        if kind ~= "table" and kind ~= "userdata" then return nil, nil, "unsupported container: " .. kind end
        local ok, count = pcall(function() return #value end)
        if not ok or type(count) ~= "number" or count < 0 or count ~= math.floor(count) then
            return nil, nil, "container length unavailable"
        end
        local result = {}
        for index = 1, math.min(count, limit) do
            local read_ok, item = pcall(function() return value[index] end)
            if not read_ok or item == nil then return nil, count, "container index unavailable: " .. index end
            result[index] = item
        end
        return result, count
    end
    local function candidate(name)
        local lower = name:lower()
        for _, word in ipairs({"play", "post", "event", "trigger", "switch", "state", "rtpc", "bank", "sound"}) do
            if lower:find(word, 1, true) then return true end
        end
        return false
    end
    local function describe_type(lines, td, label)
        local method_values, method_error = metadata(td, "get_methods")
        local field_values, field_error = metadata(td, "get_fields")
        local methods, method_count, method_list_error = descriptor_list(method_values, MAX_METHODS)
        local fields, field_count, field_list_error = descriptor_list(field_values, MAX_FIELDS)
        if methods == nil then
            lines[#lines + 1] = "METHODS_UNAVAILABLE\t" .. label .. "\t" .. tostring(method_error or method_list_error)
            methods = {}
        end
        for index = 1, math.min(#methods, MAX_METHODS) do
            local method = methods[index]
            local name = text(metadata(method, "get_name") or "<unavailable>")
            local count = metadata(method, "get_num_params")
            local types, type_count = descriptor_list(metadata(method, "get_param_types"), MAX_PARAMS)
            local names = descriptor_list(metadata(method, "get_param_names"), MAX_PARAMS)
            local params, complete = {}, true
            if type(count) ~= "number" or count < 0 or count > MAX_PARAMS or types == nil or type_count ~= count then
                complete = false
            else
                for pi = 1, count do
                    local parameter_type = type_name(types[pi])
                    complete = complete and parameter_type ~= "<unavailable>"
                    params[#params + 1] = parameter_type .. " " .. text(type(names) == "table" and names[pi] or "<unnamed>")
                end
            end
            local static = metadata(method, "is_static")
            local return_type = type_name(metadata(method, "get_return_type"))
            complete = complete and type(static) == "boolean" and return_type ~= "<unavailable>"
            local is_candidate = candidate(name)
            if is_candidate then P.candidate_count = P.candidate_count + 1 end
            lines[#lines + 1] = table.concat({"METHOD", label, name,
                "static=" .. (static == nil and "unknown" or tostring(static)),
                "return=" .. return_type,
                "param_count=" .. tostring(count or "unknown"), "params=" .. table.concat(params, ";"),
                "signature_complete=" .. tostring(complete), "candidate=" .. tostring(is_candidate),
                "hook_verified=false"}, "\t")
        end
        if method_count and method_count > MAX_METHODS then lines[#lines + 1] = "METHOD_LIMIT\t" .. label .. "\t" .. method_count end
        if fields == nil then
            lines[#lines + 1] = "FIELDS_UNAVAILABLE\t" .. label .. "\t" .. tostring(field_error or field_list_error)
            fields = {}
        end
        for index = 1, math.min(#fields, MAX_FIELDS) do
            local field = fields[index]
            lines[#lines + 1] = table.concat({"FIELD", label,
                text(metadata(field, "get_name") or "<unavailable>"), type_name(metadata(field, "get_type")),
                "value=not_read"}, "\t")
        end
        if field_count and field_count > MAX_FIELDS then lines[#lines + 1] = "FIELD_LIMIT\t" .. label .. "\t" .. field_count end
    end
    function P.inspect()
        if P.cache ~= nil then return P.cache end
        P.type_count, P.candidate_count = 0, 0
        local lines, visited = {"schema=mhgz_rise_audio_capabilities_v1", "mode=metadata_only",
            "recorder_version=" .. SCRIPT_VERSION, "probe_installs_audio_hooks=0", "runtime_listeners=see_audio_hooks_report",
            "No Wwise methods, object fields or setters were invoked; candidates are not proven hookable.",
            "Limits: types=" .. #targets .. "; parents=2; methods/type=192; fields/type=64; parameters/method=16"}, {}
        for _, name in ipairs(targets) do
            local ok, td = pcall(sdk.find_type_definition, name)
            if not ok or td == nil then
                lines[#lines + 1] = "TYPE\t" .. name .. "\t" .. (ok and "missing" or "lookup_error")
            else
                P.type_count = P.type_count + 1
                lines[#lines + 1] = "TYPE\t" .. name .. "\tavailable"
                for _ = 1, MAX_PARENTS do
                    if td == nil then break end
                    local label = type_name(td)
                    if label == "<unavailable>" or visited[label] then break end
                    visited[label] = true
                    describe_type(lines, td, label)
                    td = metadata(td, "get_parent_type")
                end
            end
        end
        lines[#lines + 1] = "SUMMARY\ttypes=" .. P.type_count .. "\tcandidates=" .. P.candidate_count .. "\tprobe_installs_audio_hooks=0"
        P.cache = table.concat(lines, "\n") .. "\n"
        return P.cache
    end
    function P.write(path)
        local body = P.inspect()
        local handle, open_error = io.open(path, "w")
        if handle == nil then return false, tostring(open_error) end
        local ok, write_error = pcall(function()
            local written, err = handle:write(body)
            if written == nil then error(err or "audio probe write failed") end
            handle:flush()
        end)
        local closed, close_error = pcall(function() handle:close() end)
        if not ok or not closed then return false, tostring(write_error or close_error) end
        P.last_path = path
        P.status = "[AUDIO PROBE] Saved " .. P.type_count .. " type descriptions"
        return true
    end
    function P.auto_export()
        if P.auto_attempted then return end
        P.auto_attempted = true
        local path = OUTPUT_DIRECTORY .. "/mhrise_audio_probe_" .. os.date("%Y%m%d_%H%M%S") .. "_boot.txt"
        local ok, result = pcall(P.write, path)
        if not ok or not result then
            P.status = "[AUDIO PROBE ERROR] Auto export failed; F9 can retry"
        end
    end
    return P
end)()
-- END AUDIO CAPABILITY PROBE.

-- BEGIN AUDIO CONTEXT PREFLIGHT: descriptors only; not condition/hit capture.
-- Do not hook reflected setters merely because a signature exists. The old
-- postEvent wrappers produced zero callbacks; numeric native dispatch must be
-- verified first. Keep this report separate from both audio CSV schemas.
local audio_context_preflight = (function()
    local P = { attempted = false, status = "[AUDIO CONTEXT] Waiting for metadata", last_path = nil }
    local specs = {
        {"via.wwise.WwiseManager", "manager"},
        {"via.wwise.WwiseSwitchElement", "all"},
        {"via.wwise.WwiseEffectiveSwitchParam", "all"},
        {"via.wwise.WwiseEffectiveStateParam", "all"},
        {"via.wwise.WwiseStateParam", "all"},
        {"via.wwise.WwiseGameParameterParam", "all"},
        {"via.wwise.WwiseSetGameParameterParam", "all"},
        {"via.wwise.WwiseSavableSetStateParam", "all"},
        {"snow.enemy.EnemyCharacterBase", "hit"},
        {"snow.hit.EnemyCalcDamageInfo", "all"},
        {"snow.hit.DamageFlowInfoBase", "all"},
        {"snow.DamageReceiver.HitInfo", "all"},
        {"snow.hit.EnemyCalcDamageInfo.AfterCalcInfo", "all"},
        {"snow.hit.AttackHitInfo", "all"},
        {"snow.player.PlayerQuestBase", "hit"},
    }
    local manager_methods = {
        setSwitch=true, setSwitchByName=true, setState=true, setStateWithSave=true, resetState=true,
        executeSetGameParameter=true, addGameParameter=true, deleteGameParameter=true,
        getGameObjectByGameObjectId=true, getSaveDataStateList=true, getSaveDataStateListCount=true,
        getWwiseContainer=true,
    }
    local function clean(value) return tostring(value):gsub("[\r\n\t]", " ") end
    local function meta(object, method)
        if not object then return nil, "missing_descriptor" end
        local ok, value = pcall(function() return object[method](object) end)
        if ok then return value end
        return nil, clean(value)
    end
    local function typename(td) return meta(td, "get_full_name") or "unknown" end
    local function vector(value, limit)
        if type(value) ~= "table" and type(value) ~= "userdata" then return {}, "unavailable_container" end
        local ok, count = pcall(function() return #value end)
        if not ok or type(count) ~= "number" or count < 0 or count ~= math.floor(count) then
            return {}, "unavailable_length"
        end
        local result = {}
        for i = 1, math.min(count, limit) do
            local read_ok, item = pcall(function() return value[i] end)
            if not read_ok or item == nil then return result, "unavailable_index_" .. i end
            result[i] = item
        end
        return result, count > limit and ("truncated_" .. count) or "complete"
    end
    local function selected(name, mode)
        if mode == "all" then return true end
        if mode == "manager" then return manager_methods[name] == true end
        local lower = name:lower()
        return lower:find("hit", 1, true) ~= nil or lower:find("damage", 1, true) ~= nil
    end
    function P.inspect()
        local lines = {"schema=mhgz_rise_audio_context_preflight_v1", "recorder_version=" .. SCRIPT_VERSION,
            "mode=metadata_only;hooks_installed=0;live_values_read=0;game_methods_invoked=0",
            "conditions_capture=see_native_context_status;emitter_resolution=not_implemented;damage_capture=not_implemented",
            "initial_conditions=unknown;final_media=unknown;descriptor_address_is_not_runtime_call_evidence",
            "limits=types:15;methods/type:2048;fields/type:256;params/method:16;parents:0"}
        for _, spec in ipairs(specs) do
            local name, mode = spec[1], spec[2]
            local ok, td = pcall(sdk.find_type_definition, name)
            lines[#lines+1] = "TYPE\t" .. name .. "\t" .. (ok and td and "available" or "unavailable")
            if ok and td then
                local methods, method_status = vector(meta(td, "get_methods"), 2048)
                local fields, field_status = vector(meta(td, "get_fields"), 256)
                lines[#lines+1] = "ENUM\t" .. name .. "\tmethods=" .. method_status .. "\tfields=" .. field_status
                for index, method in ipairs(methods) do
                    local method_name = clean(meta(method, "get_name") or "unknown")
                    if selected(method_name, mode) then
                        local params, param_status = vector(meta(method, "get_param_types"), 16)
                        local count, static = meta(method, "get_num_params"), meta(method, "is_static")
                        local owner = typename(meta(method, "get_declaring_type"))
                        local result = typename(meta(method, "get_return_type"))
                        local types, complete = {}, param_status == "complete" and type(count) == "number" and
                            count >= 0 and count <= 16 and #params == count and type(static) == "boolean" and
                            result ~= "unknown" and owner == name
                        for _, param in ipairs(params) do
                            local value = typename(param); types[#types+1] = value
                            complete = complete and value ~= "unknown"
                        end
                        -- Descriptor function pointer only: never convert to float or invoke it.
                        local address, address_error = meta(method, "get_function")
                        lines[#lines+1] = table.concat({"METHOD", name, method_name, "index=" .. index,
                            "owner=" .. owner, "static=" .. tostring(static), "return=" .. result,
                            "params=" .. table.concat(types, ";"), "signature_complete=" .. tostring(complete),
                            "function=" .. clean(address or "unknown"),
                            "address_error=" .. clean(address_error or ""), "hook_verified=false"}, "\t")
                    end
                end
                for _, field in ipairs(fields) do
                    local field_name = clean(meta(field, "get_name") or "unknown")
                    if selected(field_name, mode) then
                        local offset, offset_error = meta(field, "get_offset_from_base")
                        lines[#lines+1] = table.concat({"FIELD", name, field_name,
                            typename(meta(field, "get_type")), "offset=" .. clean(offset or "unknown"),
                            "value=not_read", "offset_error=" .. clean(offset_error or "")}, "\t")
                    end
                end
            end
        end
        return table.concat(lines, "\n") .. "\n"
    end
    function P.auto_export()
        if P.attempted then return end
        P.attempted = true
        local path = OUTPUT_DIRECTORY .. "/mhrise_audio_context_" .. os.date("%Y%m%d_%H%M%S") .. "_boot.txt"
        local handle
        local ok, err = pcall(function()
            local body = P.inspect()
            handle = assert(io.open(path, "wb"))
            assert(handle:write(body)); assert(handle:flush())
        end)
        if handle then
            local closed, result, close_error = pcall(function() return handle:close() end)
            if not closed or not result then ok, err = false, close_error or result or "close_failed" end
        end
        if ok then
            P.last_path, P.status = path, "[AUDIO CONTEXT] Metadata saved; numeric capture capability is in native status"
        else
            P.status = "[AUDIO CONTEXT ERROR] " .. clean(err)
        end
    end
    return P
end)()
-- END AUDIO CONTEXT PREFLIGHT.

-- BEGIN AUDIO OBSERVATION: exact scalar returns only; no argument/object access.
local audio_capture = (function()
    local A = { attempted = false, hook_count = 0, recording = false, file = nil,
        status = "[AUDIO] Listeners not initialized", rows = 0, dropped = 0,
        errors = 0, observed = 0, error = nil, install_lines = {}, diagnostics = {}, listeners = {} }
    local capacity, pending, queue = 2048, 0, {}
    for i = 1, capacity do queue[i] = {} end
    -- These three signatures were exported by the actual 2026-10-18 boot probe.
    -- postEvent's UInt32 semantic is deliberately UNKNOWN, not PlayingID/EventID.
    -- Never hook the unfiltered get_EventId getter: unrelated getters may share
    -- its native implementation. Old 1.16 event_parameter_read rows are untrusted.
    local specs = {
        {"via.wwise.WwiseManager", "postEvent", {"via.GameObject", "via.wwise.RequestInfo"}, "post_event_return"},
        {"via.wwise.WwiseManager", "postEvent", {"via.GameObject", "via.wwise.RequestInfo", "System.UInt32"}, "post_event_return"},
        {"via.wwise.WwiseManager", "postEvent", {"via.GameObject", "via.wwise.RequestInfo", "System.UInt32", "System.UInt32"}, "post_event_return"},
    }
    local method_cache = {}
    local function clean(value) return tostring(value):gsub("[\r\n\t]", " ") end
    local function type_name(td) return td and td:get_full_name() or "<unavailable>" end
    local function read_methods(owner)
        if method_cache[owner] then return method_cache[owner] end
        local entry = { candidates = {} }
        method_cache[owner] = entry -- Cache failures too; no repeated discovery.
        local ok, err = pcall(function()
            local td = sdk.find_type_definition(owner)
            if not td then entry.error = "type_unavailable"; return end
            local methods = td:get_methods()
            if type(methods) ~= "table" and type(methods) ~= "userdata" then
                entry.error = "method_list_unavailable:" .. type(methods); return
            end
            local count = #methods
            if type(count) ~= "number" or count < 0 or count % 1 ~= 0 or count > 512 then
                entry.error = "method_list_limit_or_invalid:" .. clean(count); return
            end
            entry.count = count
            for i = 1, count do
                local method = methods[i]
                local readable, name = pcall(function() return method:get_name() end)
                if not readable or type(name) ~= "string" then
                    -- An unreadable descriptor might hide another exact match.
                    -- Fail closed instead of selecting from an incomplete list.
                    entry.error = "method_descriptor_unreadable:index=" .. i; return
                end
                if name == "postEvent" then
                    if #entry.candidates >= 32 then entry.error = "post_candidate_limit"; return end
                    entry.candidates[#entry.candidates + 1] = { method = method, index = i }
                end
            end
        end)
        if not ok then entry.error = "enumeration_error:" .. clean(err) end
        A.diagnostics[#A.diagnostics + 1] = "ENUMERATE\t" .. owner .. "\tcount=" ..
            tostring(entry.count or "unknown") .. "\tpost_candidates=" .. #entry.candidates ..
            "\terror=" .. tostring(entry.error or "none")
        return entry
    end
    local function unsigned_return(value)
        local n = sdk.to_int64(value)
        -- Modulo is compatible with both LuaJIT and Lua 5.4. No pointer dereference.
        if type(n) ~= "number" or n ~= n or n == math.huge or n == -math.huge then
            error("UInt32 return unavailable")
        end
        return n % 4294967296
    end
    local function observe(retval, listener)
        A.observed = A.observed + 1
        listener.observed = listener.observed + 1
        if not A.recording then return end
        if pending >= capacity then A.dropped = A.dropped + 1; return end
        local value = unsigned_return(retval)
        local entry = queue[pending + 1]
        entry.value, entry.kind, entry.label = value, "post_event_return", listener.label
        entry.time, entry.frame, entry.sample = state.capture_time, state.frame_callback_count, state.sample_count
        entry.clock = os.clock() - A.clock_start
        pending = pending + 1
    end
    local function validate(method, spec)
        if method:get_name() ~= spec[2] then return false, "method_name_mismatch" end
        local owner = type_name(method:get_declaring_type())
        if owner ~= spec[1] then return false, "declaring_type:" .. owner end
        local static = method:is_static()
        if static ~= true then return false, "static_flag:" .. tostring(static) end
        local result = type_name(method:get_return_type())
        if result ~= "System.UInt32" then return false, "return_type:" .. result end
        local count = method:get_num_params()
        if count ~= #spec[3] then return false, "parameter_count:" .. tostring(count) end
        local types = method:get_param_types()
        if type(types) ~= "table" and type(types) ~= "userdata" then return false, "parameter_list_unavailable" end
        if #types ~= #spec[3] then return false, "parameter_list_count:" .. tostring(#types) end
        for i, name in ipairs(spec[3]) do
            local actual = type_name(types[i])
            if actual ~= name then return false, "parameter_type[" .. i .. "]:" .. actual end
        end
        return true, "exact_match"
    end
    local function select_method(spec, label)
        local entry = read_methods(spec[1])
        if entry.error then return nil, entry.error end
        local matches = {}
        for _, candidate in ipairs(entry.candidates) do
            local ok, match, why = pcall(validate, candidate.method, spec)
            A.diagnostics[#A.diagnostics + 1] = "MATCH\t" .. label .. "\tindex=" .. candidate.index ..
                "\t" .. (ok and clean(why) or ("metadata_error:" .. clean(match)))
            if not ok then return nil, "candidate_metadata_error:index=" .. candidate.index end
            if match then matches[#matches + 1] = candidate end
        end
        if #matches ~= 1 then return nil, "exact_match_count:" .. #matches end
        return matches[1]
    end
    function A.install()
        if A.attempted then return end
        A.attempted = true
        local addresses = {}
        for _, spec in ipairs(specs) do
            local label = spec[1] .. "." .. spec[2] .. "(" .. table.concat(spec[3], ",") .. ")"
            local ok, result = pcall(function()
                local selected, reason = select_method(spec, label)
                if not selected then return reason end
                local method = selected.method
                local address = method:get_function()
                if address == nil or sdk.to_int64(address) == 0 then return "function_unavailable" end
                local key = tostring(address)
                if addresses[key] then return "shared_function_skipped;address=" .. key end
                local listener = { label = label, observed = 0, errors = 0, address = key }
                sdk.hook(method,
                    function(_) return sdk.PreHookResult.CALL_ORIGINAL end,
                    function(retval)
                        local captured = pcall(observe, retval, listener)
                        if not captured then A.errors = A.errors + 1; listener.errors = listener.errors + 1 end
                        return retval -- Always preserve the exact original pointer/value.
                    end)
                addresses[key] = true
                A.listeners[#A.listeners + 1] = listener
                A.hook_count = A.hook_count + 1
                return "installed_via_enumerated_descriptor;index=" .. selected.index .. ";address=" .. key
            end)
            A.install_lines[#A.install_lines + 1] = label .. "\t" .. (ok and result or ("error: " .. tostring(result)))
        end
        A.status = "[AUDIO] " .. A.hook_count .. "/3 post listeners; observed=" .. A.observed
    end
    function A.report(path)
        A.install()
        local f, err = io.open(path, "w")
        if not f then return false, err end
        local ok, result = pcall(function()
            assert(f:write("schema=mhgz_audio_listeners_v2\nrecorder_version=" .. SCRIPT_VERSION ..
                "\ninstalled=" .. A.hook_count .. "\nobserved_callbacks=" .. A.observed ..
                "\npost_return_semantics=unknown\nsource_scope=all_world_unattributed\n" ..
                "unfiltered_event_getter=disabled\nevent_id_capture=not_implemented\n" ..
                table.concat(A.install_lines, "\n") .. "\n" .. table.concat(A.diagnostics, "\n") .. "\n"))
            for _, listener in ipairs(A.listeners) do
                assert(f:write("ACTIVITY\t" .. listener.label .. "\tobserved=" .. listener.observed ..
                    "\terrors=" .. listener.errors .. "\taddress=" .. listener.address .. "\n"))
            end
            assert(f:flush())
        end)
        local closed, close_result = pcall(function() return f:close() end)
        return ok and closed and close_result ~= nil and close_result ~= false, result
    end
    function A.update()
        if not A.boot_attempted then
            A.boot_attempted, A.boot_clock = true, os.clock()
            A.boot_base = OUTPUT_DIRECTORY .. "/mhrise_audio_hooks_" .. os.date("%Y%m%d_%H%M%S")
            local ok, result, err = pcall(A.report, A.boot_base .. "_boot.txt")
            if not ok or not result then A.report_error = clean(err or result) end
        end
        -- At most two live snapshots: first activity, or a 2s zero-activity
        -- snapshot followed by first activity. No repeated writes on failure.
        if not A.live_attempted and (A.observed > 0 or os.clock() - A.boot_clock >= 2) then
            A.live_attempted, A.live_observed = true, A.observed
            local ok, result, err = pcall(A.report, A.boot_base .. "_live.txt")
            if not ok or not result then A.report_error = clean(err or result) end
        elseif A.live_attempted and A.live_observed == 0 and A.observed > 0 then
            A.live_observed = A.observed
            local ok, result, err = pcall(A.report, A.boot_base .. "_live.txt")
            if not ok or not result then A.report_error = clean(err or result) end
        end
        if A.report_error then
            A.status = "[AUDIO ERROR] Report failed; installed=" .. A.hook_count
        elseif not A.error then
            A.status = "[AUDIO] " .. A.hook_count .. "/3 post listeners; callbacks=" .. A.observed ..
                "; rows=" .. A.rows .. "; dropped=" .. A.dropped .. "; errors=" .. A.errors
        end
    end
    function A.start(path)
        A.install()
        A.rows, A.dropped, A.errors, A.error, pending = 0, 0, 0, nil, 0
        local f, err = io.open(path, "w")
        if not f then return false, err end
        local ok, result = pcall(function()
            assert(f:write("event_index,capture_time_s,frame_index,last_sample_index,clock_elapsed_s,kind,event_id,post_return_id,hook,source_scope\n"))
        end)
        if not ok then pcall(function() f:close() end); return false, result end
        A.file, A.clock_start, A.recording = f, os.clock(), true
        return true
    end
    function A.drain()
        if not A.file or A.error or pending == 0 then return end
        local ok, err = pcall(function()
            for i = 1, pending do
                local e = queue[i]
                assert(A.file:write(table.concat({A.rows + 1, csv_number(e.time), e.frame, e.sample,
                    csv_number(e.clock), e.kind,
                    e.kind == "event_parameter_read" and string.format("%.0f", e.value) or "",
                    e.kind == "post_event_return" and string.format("%.0f", e.value) or "",
                    csv_text(e.label), "all_world_unattributed"}, ",") .. "\n"))
                A.rows = A.rows + 1
            end
            assert(A.file:flush())
        end)
        pending = 0
        if not ok then
            A.error, A.recording = tostring(err), false
            A.status = "[AUDIO ERROR] File write failed; trajectory continues"
        else
            A.status = "[AUDIO] " .. A.hook_count .. "/3 post listeners; rows=" .. A.rows ..
                "; dropped=" .. A.dropped .. "; errors=" .. A.errors
        end
    end
    function A.stop()
        A.recording = false
        A.drain()
        if A.file then
            local ok, result, err = pcall(function() return A.file:close() end)
            if not ok or result == nil or result == false then
                A.error = tostring(err or result); A.status = "[AUDIO ERROR] Close failed"
            end
        end
        A.file = nil
    end
    return A
end)()
-- END AUDIO OBSERVATION.

-- BEGIN NATIVE AUDIO CLIENT
local native_audio = (function()
    local N = { status = "[AUDIO NATIVE] Waiting for bridge", sequence = 0, pending = nil,
        commands = {}, rows = 0, ready = false, recording = false, last_poll = -1 }
    local nonce = (os.date("%Y%m%d_%H%M%S") .. "_" .. tostring({})):gsub("[^%w_-]", "_")
    local command_path = OUTPUT_DIRECTORY .. "/audio_native_bridge_command.txt"
    local status_path = OUTPUT_DIRECTORY .. "/audio_native_bridge_status.txt"
    local function request(verb, basename, clock)
        if #N.commands >= 8 then N.status = "[AUDIO NATIVE ERROR] Command queue full"; return false end
        N.commands[#N.commands + 1] = {verb = verb, basename = basename or "none", clock = clock or 0}
        return true
    end
    function N.start(basename, clock) return request("START", basename, clock) end
    function N.stop(basename, clock) return request("STOP", basename, clock) end
    function N.update()
        local now = os.clock()
        if not N.pending and #N.commands == 0 and now - N.last_poll < 0.1 then return end
        N.last_poll = now
        local file = io.open(status_path, "rb")
        if file then
            local body = file:read(2049); file:close()
            if body and #body <= 2048 and body:sub(-5) == "\nEND\n" then
                local values = {}
                for key, value in body:gmatch("([%w_]+)=([^\n]*)\n") do values[key] = value end
                if values.schema == "mhgz_rise_native_audio_v1" and values.nonce == nonce and
                    N.pending and tonumber(values.seq) == N.pending.sequence then
                    local completed = N.pending
                    N.pending = nil
                    N.ready, N.recording = values.ready == "1", values.recording == "1"
                    N.rows = tonumber(values.rows) or 0
                    N.context_ready = values.context_ready == "1"
                    N.context_rows = tonumber(values.context_rows) or 0
                    N.context_observed = tonumber(values.context_observed) or 0
                    N.objects_ready = values.objects_ready == "1"
                    N.trigger_ready = values.trigger_ready == "1"
                    N.status = N.ready and ("[AUDIO NATIVE] " .. (N.recording and "REC " or "READY ") ..
                        N.rows .. " rows; observed=" .. (values.observed or "?") .. "; drops=" .. (values.dropped or "?")) or
                        ("[AUDIO NATIVE ERROR] " .. (values.error or "not ready"))
                    if values.error and values.error ~= "" then N.status = "[AUDIO NATIVE ERROR] " .. values.error end
                    if N.ready then
                        N.status = N.status .. (N.trigger_ready and ("; triggers=" .. (values.trigger_observed or "0") ..
                            "; hit contexts=" .. (values.trigger_hit_contexts or "0")) or "; trigger contexts unavailable")
                        N.status = N.status .. (N.objects_ready and ("; objects=" .. (values.objects_rows or "0") ..
                            " rows/" .. (values.objects_observed or "0") .. " observed; drops=" .. (values.objects_dropped or "?")) or
                            "; object keys unavailable")
                        N.status = N.status .. (N.context_ready and ("; context=" .. N.context_rows ..
                            " rows/" .. N.context_observed .. " observed; drops=" .. (values.context_dropped or "?")) or
                            "; context unavailable (old DLL or not loaded)")
                        if values.context_error and values.context_error ~= "" then
                            N.status = N.status .. "; CONTEXT ERROR: " .. values.context_error
                        end
                    end
                    if completed.verb == "START" and N.recording and state.recording then
                        write_event("native_audio_anchor", "qpc=" .. (values.anchor_qpc or "?") ..
                            ";frequency=" .. (values.qpc_frequency or "?") .. ";capture_anchor_estimate_s=" ..
                            (values.capture_anchor or "?") .. ";ack_received_capture_s=" .. tostring(state.capture_time) ..
                            ";scope=all_world_unattributed;timing=last_sample_at_IPC_write_plus_render_delay")
                        write_event("native_audio_context_capability", "ready=" .. tostring(N.context_ready) ..
                            ";rows=" .. N.context_rows .. ";observed=" .. N.context_observed ..
                            ";provenance=numeric_API_requests_only;prehook=unknown;emitter_owner=unknown;final_media=unknown")
                    end
                end
            end
        end
        if N.pending and os.time() - N.pending.sent_at >= 4 then
            N.pending, N.commands = nil, {}
            N.ready = false
            N.status = "[AUDIO NATIVE ERROR] No matching DLL acknowledgement; trajectory continues"
        end
        if not N.pending and #N.commands > 0 then
            local value = table.remove(N.commands, 1)
            N.sequence = N.sequence + 1
            local body = table.concat({"MHGZ_AUDIO_NATIVE_V1", nonce, tostring(N.sequence), value.verb,
                value.basename, string.format("%.9f", value.verb == "START" and state.capture_time or value.clock), "END", ""}, "\n")
            local output = io.open(command_path, "wb")
            if output then
                local ok, result = pcall(function() return output:write(body) end)
                output:close()
                if ok and result then N.pending = {sequence = N.sequence, verb = value.verb, sent_at = os.time()}
                else N.status = "[AUDIO NATIVE ERROR] Cannot write bridge command" end
            else N.status = "[AUDIO NATIVE ERROR] Cannot open bridge command" end
        end
        -- Heartbeat uses the same acknowledged epoch, never overwrites pending F9 commands.
        if not N.pending and #N.commands == 0 and (not N.last_ping or os.time() - N.last_ping >= 1) then
            N.last_ping = os.time(); request("PING")
        end
    end
    request("PING")
    return N
end)()
-- END NATIVE AUDIO CLIENT

-- BEGIN AUDIO COMBAT CAPTURE: independent damage callback, NOT inferred from sound.
local combat_capture = (function()
    local C = { installed = 0, observed = 0, hits = 0, sound_contexts = 0,
        rows = 0, errors = 0, dropped = 0, status = "[HITS] Waiting for metadata" }
    local specs = {
        {"snow.enemy.EnemyCharacterBase", "afterCalcDamage_DamageSide", "damage_side_callback"},
        {"snow.wwise.WwisePlayerMonitoredParameters", "hitTrigger", "hit_sound_context"},
    }
    local capacity, pending, queue, schemas, schema_count = 512, 0, {}, {}, 0
    local scalar_capacity = 96
    local context_stacks, context_count = {}, 0
    for i = 1, capacity do queue[i] = {} end
    local header = {"row", "kind", "capture_time_last_sample_s", "lua_clock_elapsed_s", "frame_callback", "sample_index",
        "thread_context_address", "receiver_address", "hit_info_address", "flow_type", "attacker_address",
        "attacker_name", "attacker_relation", "target_address", "target_name", "hit_x", "hit_y", "hit_z",
        "attack_shape_address", "attack_shape_type", "attack_shape_fields", "damage_shape_type", "damage_shape_fields",
        "part_info_type", "part_info_fields", "flow_fields", "sound_context_fields", "outcome", "read_errors",
        "damage_rs_type", "damage_rs_fields", "field_coverage", "sound_context_post_fields", "thread_id"}
    local numeric = { [3]=true, [4]=true, [5]=true, [6]=true, [16]=true, [17]=true, [18]=true }
    local primitive = { ["System.Boolean"]=true, ["System.Byte"]=true, ["System.SByte"]=true,
        ["System.Int16"]=true, ["System.UInt16"]=true, ["System.Int32"]=true, ["System.UInt32"]=true,
        ["System.Single"]=true, ["System.Double"]=true }
    -- Select by purpose, not the first N declared fields. In particular the
    -- actual _EffectMaterialType descriptor is near the END of the sound type.
    local priorities = {
        sound = {"_MaterialType", "_EffectMaterialType", "_Dmg", "_MySelfOrOther", "_MySelfOrOtherForBullet",
            "_IsMaster", "_IsServant", "_OverWriteMaterialType", "_OverWriteWater", "_OverWriteSap", "_OverWriteWaterShallow"},
        flow = {"<OwnerType>k__BackingField", "<PhysicalPartsVitalDamage>k__BackingField",
            "<ElementPartsVitalDamage>k__BackingField", "<HitResponse>k__BackingField", "<WeaponType>k__BackingField",
            "<DamageAttackerType>k__BackingField", "<ExtractiveType>k__BackingField",
            "<StaminaDamage>k__BackingField", "<StunDamage>k__BackingField"},
    }
    local field_patterns = {
        sound = {"Material", "Dmg", "Hit", "Trigger"},
        flow = {"Damage", "Hit", "Parts", "Meat", "Attack", "OwnerType", "WeaponType", "Extractive", "Critical"},
        attack = {"Attack", "Attr", "Hit", "Index", "Id", "ID", "Priority"},
        damage = {"Meat", "Hit", "Parts", "Attr", "Index", "Id", "ID", "Priority"},
        part = {"Parts", "Meat", "Hit", "Index", "Id", "ID", "Shape", "Data", "Group", "Joint"},
    }
    local function name(td) return td and td:get_full_name() or "unknown" end
    local function clean(v) return tostring(v):gsub("[\r\n\t]", " "):sub(1, 512) end
    local function list(value, limit)
        assert(type(value) == "table" or type(value) == "userdata", "metadata_container_unavailable")
        local count = #value
        assert(type(count) == "number" and count >= 0 and count % 1 == 0 and count <= limit, "metadata_limit_or_invalid")
        return count
    end
    local function address(value)
        if not value then return "unknown" end
        local ok, result = pcall(function() return tostring(value:get_address()) end)
        return ok and result or "unknown"
    end
    local function field(object, key)
        if not object then return nil end
        local td = object:get_type_definition()
        local descriptor = td:get_field(key)
        return descriptor and descriptor:get_data(object) or nil
    end
    -- Copy only scalar data while callback arguments are alive. No managed objects
    -- escape into the queue. Exact field names/types are retained, not renamed to
    -- guessed attack-segment/part/damage meanings. Int64/pointers stay elsewhere.
    local function scalars(object, role, depth, visited)
        role, depth, visited = role or "flow", depth or 0, visited or {}
        if not object then return "unknown", "unknown", "object_unavailable" end
        local td = object:get_type_definition()
        local type_key = name(td)
        local key = role .. ":" .. type_key
        if not schemas[key] then
            assert(schema_count < 32, "scalar_type_cache_limit")
            schema_count = schema_count + 1
            local schema = { fields = {}, nested = {}, diagnostics = {}, declared = {}, declared_count = 0, references = 0 }
            schemas[key] = schema
            local ok, err = pcall(function()
                local current, parent_count, candidates, seen = td, 0, {}, {}
                local rank = {}
                for i, n in ipairs(priorities[role] or {}) do rank[n] = i end
                while current and name(current) ~= "System.Object" and parent_count < 8 do
                    local fields = current:get_fields()
                    local count = list(fields, 512)
                    for i = 1, count do
                        local f = fields[i]
                        local ft = f:get_type()
                        local kind, fn = name(ft), f:get_name()
                        schema.declared_count = schema.declared_count + 1
                        if #schema.declared < 128 then schema.declared[#schema.declared + 1] = name(current) .. "." .. fn .. ":" .. kind end
                        local enum = ft and name(ft:get_parent_type()) == "System.Enum"
                        if not f:is_static() and not primitive[kind] and not enum then schema.references = schema.references + 1 end
                        local wanted = rank[fn] ~= nil
                        for _, pattern in ipairs(field_patterns[role] or {}) do if fn:find(pattern, 1, true) then wanted = true end end
                        if not f:is_static() and wanted and not seen[fn] then
                            seen[fn] = true
                            local label = name(current) .. "." .. fn .. ":" .. kind
                            if primitive[kind] or enum then
                                candidates[#candidates + 1] = {f, label, rank[fn] or 1000}
                            elseif (role == "part" or role == "damage" or role == "flow") and
                                (kind:find("snow.hit.", 1, true) == 1 or kind:find("snow.enemy.", 1, true) == 1) and
                                not kind:find("[]", 1, true) then
                                if #schema.nested < 4 then schema.nested[#schema.nested + 1] = {f, label}
                                else schema.diagnostics[#schema.diagnostics + 1] = "selected_nested_limit=" .. label end
                            end
                        end
                    end
                    current, parent_count = current:get_parent_type(), parent_count + 1
                end
                if current and name(current) ~= "System.Object" then schema.diagnostics[#schema.diagnostics + 1] = "metadata_parent_limit" end
                table.sort(candidates, function(a,b) return a[3] == b[3] and a[2] < b[2] or a[3] < b[3] end)
                for i = 1, math.min(#candidates, scalar_capacity) do schema.fields[i] = candidates[i] end
                if #candidates > scalar_capacity then schema.diagnostics[#schema.diagnostics + 1] = "selected_field_limit=" .. #candidates end
                if role == "sound" then
                    for _, required in ipairs({"_MaterialType", "_EffectMaterialType", "_Dmg"}) do
                        if not seen[required] then schema.diagnostics[#schema.diagnostics + 1] = "required_missing=" .. required end
                    end
                end
            end)
            if not ok then schema.diagnostics[#schema.diagnostics + 1] = "schema_error=" .. clean(err) end
        end
        local schema, values, coverage = schemas[key], {}, {}
        local identity = address(object)
        if visited[identity] then return type_key, "nested_cycle", "nested_cycle" end
        visited[identity] = true
        for _, entry in ipairs(schema.fields) do
            local ok, value = pcall(function() return entry[1]:get_data(object) end)
            local kind = type(value)
            local readable = ok and (kind == "boolean" or (kind == "number" and value == value and math.abs(value) ~= math.huge))
            values[#values + 1] = entry[2] .. "=" .. (readable and tostring(value) or "unknown")
            if not readable then coverage[#coverage + 1] = "field_unreadable=" .. entry[2] end
        end
        for _, entry in ipairs(schema.nested) do
            if depth == 2 then coverage[#coverage + 1] = "nested_depth_limit=" .. entry[2]
            else
                local ok, value = pcall(function() return entry[1]:get_data(object) end)
                if ok and value then
                    local good, _, result, nested_coverage = pcall(scalars, value, role, depth + 1, visited)
                    if good then
                        values[#values + 1] = entry[2] .. "=>{" .. result .. "}"
                        if nested_coverage ~= "selected_profile_ok" then coverage[#coverage + 1] = entry[2] .. ":" .. nested_coverage end
                    else coverage[#coverage + 1] = "nested_unreadable=" .. entry[2] end
                end
            end
        end
        visited[identity] = nil
        for _, d in ipairs(schema.diagnostics) do coverage[#coverage + 1] = d end
        local empty = schema.references > 0 and "container_only_unexpanded" or "no_selected_fields"
        if #values == 0 then coverage[#coverage + 1] = empty end
        return type_key, #values > 0 and table.concat(values, ";") or empty,
            #coverage > 0 and table.concat(coverage, ";") or "selected_profile_ok"
    end
    local function object_name(object)
        if not object then return "unknown" end
        local ok, value = pcall(function() return object:call("get_Name") end)
        return ok and value and clean(value) or "unknown"
    end
    local function snapshot(args, spec)
        C.observed = C.observed + 1
        if spec[3] == "damage_side_callback" then C.hits = C.hits + 1 else C.sound_contexts = C.sound_contexts + 1 end
        if not C.file then return end
        if pending == capacity then C.dropped = C.dropped + 1; return end
        local row = queue[pending + 1]
        for i = 1, #header do row[i] = nil end
        row[2], row[3], row[4], row[5], row[6] = spec[3], state.capture_time,
            os.clock() - C.clock_start, state.frame_callback_count, state.sample_count
        row[7] = tostring(args[1]) -- exact thread-context token, NOT a Windows thread ID
        row[28] = "callback_entered;resolved_damage_unknown;audio_not_hit_proof"
        local failures = {}
        local function read(label, fn)
            local ok, value = pcall(fn)
            if not ok then failures[#failures + 1] = label .. "=" .. clean(value); return nil end
            return value
        end
        local receiver = read("receiver", function() return sdk.to_managed_object(args[2]) end)
        local flow = read("flow", function() return sdk.to_managed_object(args[3]) end)
        local hit = read("hit", function()
            local value = sdk.to_managed_object(args[4])
            assert(value and name(value:get_type_definition()) == "snow.DamageReceiver.HitInfo", "hit_type_mismatch")
            return value
        end)
        row[8], row[9] = address(receiver), address(hit)
        local coverage = {}
        local function capture_fields(label, object, role, type_col, data_col)
            local t, v, quality = scalars(object, role)
            if type_col then row[type_col] = t end
            row[data_col] = v
            coverage[#coverage + 1] = label .. ":" .. quality
        end
        read("flow_fields", function() capture_fields("flow", flow, "flow", 10, 26) end)
        local attacker = read("attacker", function() return field(hit, "<AttackObject>k__BackingField") end)
        local target = read("target", function() return field(hit, "<DamageObject>k__BackingField") end)
        row[11], row[12], row[14], row[15] = address(attacker), object_name(attacker), address(target), object_name(target)
        row[13] = C.master_address and row[11] ~= "unknown" and row[11] == C.master_address and "master_gameobject_exact" or "unknown"
        read("position", function()
            local pos = field(hit, "<Position>k__BackingField")
            assert(pos, "position_unavailable")
            for i, axis in ipairs({"x", "y", "z"}) do
                local v = pos[axis]
                assert(type(v) == "number" and v == v and math.abs(v) ~= math.huge, "invalid_position")
                row[15 + i] = v
            end
        end)
        read("attack_shape", function()
            local shape = field(hit, "<AttackData>k__BackingField")
            row[19] = address(shape); capture_fields("attack", shape, "attack", 20, 21)
        end)
        read("damage_shape", function() capture_fields("damage", field(hit, "<DamageData>k__BackingField"), "damage", 22, 23) end)
        read("part_info", function() capture_fields("part", field(hit, "<HitDamageShapeInfo>k__BackingField"), "part", 24, 25) end)
        read("damage_rs", function() capture_fields("damage_rs", field(hit, "<DamageRSData>k__BackingField"), "part", 30, 31) end)
        if spec[3] == "hit_sound_context" then read("sound_context", function() capture_fields("sound_pre", receiver, "sound", nil, 27) end) end
        read("thread_id", function() row[34] = thread and thread.get_id and tostring(thread.get_id()) or "unknown" end)
        row[32] = table.concat(coverage, "|")
        if row[32]:find("limit",1,true) or row[32]:find("schema_error",1,true) or
            row[32]:find("required_missing",1,true) or row[32]:find("no_selected_fields",1,true) then
            C.incomplete = (C.incomplete or 0) + 1
        end
        row[29] = #failures > 0 and table.concat(failures, ";") or "none"
        if #failures > 0 then C.errors = C.errors + 1 end
        pending = pending + 1
        return row
    end
    local function post_failure(why)
        C.post_failures = (C.post_failures or 0) + 1
        C.post_error = clean(why)
    end
    local function context_key()
        local value = sdk.get_thread_context()
        assert(value ~= nil and tostring(value) ~= "0" and not tostring(value):match("^userdata: 0+$"), "post_thread_context_unavailable")
        return tostring(value) -- engine context token, NEVER a fabricated Windows ID
    end
    local function store_sound_frame(args, row)
        if C.post_disabled then return end
        local stored = false -- tombstone: nested non-captured calls must pop their own frame
        if row then
            local copied = {}
            for i = 1, #header do copied[i] = row[i] end
            stored = {receiver = sdk.to_managed_object(args[2]), row = copied, generation = C.generation}
        end
        if C.post_mode == "hook_storage" then
            thread.get_hook_storage().mhgz_sound_snapshot = stored
        elseif C.post_mode == "engine_context_stack" then
            local key = context_key()
            assert(key == tostring(args[1]), "post_thread_context_argument_mismatch")
            local stack = context_stacks[key]
            if not stack then
                if not stored then return end
                if context_count == 64 then post_failure("post_context_limit"); return end
                stack = {skipped = 0}; context_stacks[key] = stack; context_count = context_count + 1
            end
            if #stack == 32 or stack.skipped > 0 then
                stack.skipped = stack.skipped + 1; post_failure("post_stack_depth_limit"); return
            end
            stack[#stack + 1] = stored
        end
    end
    local function take_sound_frame()
        if C.post_disabled then return nil end
        if C.post_mode == "hook_storage" then return thread.get_hook_storage().mhgz_sound_snapshot end
        if C.post_mode ~= "engine_context_stack" then return nil end
        local key = context_key()
        local stack = context_stacks[key]
        if not stack then return nil end
        if stack.skipped > 0 then stack.skipped = stack.skipped - 1; return nil end
        local stored = stack[#stack]; stack[#stack] = nil
        if #stack == 0 then context_stacks[key] = nil; context_count = context_count - 1 end
        return stored
    end
    local function sound_post()
        local stored = take_sound_frame()
        if not stored or not C.file or C.generation ~= stored.generation then return end
        -- A re-entrant callback can append rows, never overwrite this call's slot.
        if pending == capacity then C.dropped = C.dropped + 1; return end
        local row = queue[pending + 1]
        for i = 1, #header do row[i] = stored.row[i] end
        row[2] = "hit_sound_context_post"
        row[3], row[4], row[5], row[6] = state.capture_time, os.clock() - C.clock_start, state.frame_callback_count, state.sample_count
        local _, value, quality = scalars(stored.receiver, "sound")
        row[33] = value
        row[32] = row[32] .. "|sound_post:" .. quality
        if quality ~= "selected_profile_ok" then C.incomplete = (C.incomplete or 0) + 1 end
        pending = pending + 1
        C.sound_posts = (C.sound_posts or 0) + 1
    end
    function C.install()
        if C.attempted then return end
        C.attempted, C.install_lines = true, {}
        C.post_mode = thread and type(thread.get_hook_storage) == "function" and "hook_storage" or
            type(sdk.get_thread_context) == "function" and "engine_context_stack" or "unavailable"
        if C.post_mode == "unavailable" then C.post_error = "post_storage_capability_unavailable" end
        local addresses = {}
        for _, spec in ipairs(specs) do
            local ok, why = pcall(function()
                local td = assert(sdk.find_type_definition(spec[1]), "type_unavailable")
                local methods, matches = td:get_methods(), {}
                for i = 1, list(methods, 4096) do
                    local method = methods[i]
                    if method:get_name() == spec[2] then
                        local params = method:get_param_types()
                        if name(method:get_declaring_type()) == spec[1] and method:is_static() == false and
                            name(method:get_return_type()) == "System.Void" and method:get_num_params() == 2 and
                            list(params, 16) == 2 and name(params[1]) == "snow.hit.DamageFlowInfoBase" and
                            name(params[2]) == "snow.DamageReceiver.HitInfo" then matches[#matches + 1] = method end
                    end
                end
                assert(#matches == 1, "exact_match_count=" .. #matches)
                local method = matches[1]
                local ptr = method:get_function()
                assert(ptr and sdk.to_int64(ptr) ~= 0, "function_unavailable")
                local key = tostring(ptr)
                assert(not addresses[key], "shared_function_rejected")
                sdk.hook(method, function(args)
                    local captured, row = pcall(snapshot, args, spec)
                    if not captured then C.errors = C.errors + 1 end
                    if spec[3] == "hit_sound_context" then
                        local saved, why = pcall(store_sound_frame, args, captured and row or nil)
                        if not saved then
                            post_failure(why); C.post_disabled = true
                            context_stacks, context_count = {}, 0 -- fail closed; never pop another invocation
                        end
                    end
                    return sdk.PreHookResult.CALL_ORIGINAL
                end, function(retval)
                    if spec[3] == "hit_sound_context" then
                        local ok, why = pcall(sound_post)
                        if not ok then
                            C.errors = C.errors + 1; post_failure(why); C.post_disabled = true
                            context_stacks, context_count = {}, 0
                        end
                    end
                    return retval
                end)
                addresses[key], C.installed = true, C.installed + 1
                return "installed;address=" .. key .. ";actual_callbacks_required"
            end)
            C.install_lines[#C.install_lines + 1] = spec[1] .. "." .. spec[2] .. "=" .. (ok and why or clean(why))
        end
    end
    function C.drain()
        if not C.file then pending = 0; return end
        local ok, err = pcall(function()
            for i = 1, pending do
                local row, cells = queue[i], {}
                row[1] = C.rows + 1
                for col = 1, #header do cells[col] = numeric[col] and csv_number(row[col]) or csv_text(row[col] or "unknown") end
                assert(C.file:write(table.concat(cells, ",") .. "\n"))
                C.rows = C.rows + 1
            end
            assert(C.file:flush())
        end)
        pending = 0
        if not ok then
            C.file_error = clean(err); pcall(function() C.file:close() end); C.file = nil
        end
    end
    function C.start(basename)
        C.install()
        C.generation = (C.generation or 0) + 1
        C.rows, C.errors, C.dropped, C.file_error, pending, C.clock_start, C.incomplete = 0, 0, 0, nil, 0, os.clock(), 0
        C.sound_posts = 0
        local path = OUTPUT_DIRECTORY .. "/" .. basename .. "_hits.csv"
        local existing = io.open(path, "rb")
        if existing then existing:close(); C.file_error = "hits_exists_no_overwrite"; return false end
        local f, err = io.open(path, "wb")
        if not f then C.file_error = clean(err); return false end
        local ok = pcall(function() assert(f:write(table.concat(header, ",") .. "\n")); assert(f:flush()) end)
        if not ok then pcall(function() f:close() end); C.file_error = "hits_header_failed"; return false end
        C.file = f
        return true
    end
    function C.stop()
        C.drain()
        if C.file then
            local ok, result = pcall(function() return C.file:close() end)
            if not ok or not result then C.file_error = "hits_close_failed" end
            C.file = nil
        end
    end
    function C.update()
        C.install()
        if not C.last_master_clock or os.clock() - C.last_master_clock > 0.25 then
            C.last_master_clock = os.clock()
            local ok, _, go = pcall(get_master_player_and_game_object)
            C.master_address = ok and go and address(go) or nil
        end
        C.status = "[HITS] " .. C.installed .. "/2 listeners; damage callbacks=" .. C.hits ..
            "; sound contexts=" .. C.sound_contexts .. "; rows=" .. C.rows .. "; dropped=" .. C.dropped .. "; errors=" .. C.errors ..
            "; coverage warnings=" .. (C.incomplete or 0) .. "; sound posts=" .. (C.sound_posts or 0) ..
            "; post mode=" .. tostring(C.post_mode) .. "; post failures=" .. (C.post_failures or 0)
        if C.post_error then C.status = C.status .. "; POST_ERROR=" .. C.post_error end
        if C.file_error then C.status = C.status .. "; ERROR=" .. C.file_error end
        -- Descriptor-only selected/declaration coverage. No extra getter calls,
        -- no F8 requirement, and no live-object reference is retained here.
        if C.reported_schema_count ~= schema_count then
            C.reported_schema_count = schema_count
            local f = io.open(OUTPUT_DIRECTORY .. "/combat_field_schema.txt", "wb")
            if f then
                pcall(function()
                    assert(f:write("schema=mhgz_combat_field_profiles_v1\nversion=" .. SCRIPT_VERSION .. "\n"))
                    for key, schema in pairs(schemas) do
                        assert(f:write("TYPE\t" .. key .. "\tdeclared=" .. schema.declared_count ..
                            "\tselected=" .. #schema.fields .. "\tnested=" .. #schema.nested .. "\t" .. table.concat(schema.diagnostics, ";") .. "\n"))
                        for _, entry in ipairs(schema.fields) do assert(f:write("SELECTED\t" .. entry[2] .. "\n")) end
                        for _, entry in ipairs(schema.nested) do assert(f:write("NESTED\t" .. entry[2] .. "\n")) end
                        for _, label in ipairs(schema.declared) do assert(f:write("DECLARED\t" .. label .. "\n")) end
                        if schema.declared_count > #schema.declared then assert(f:write("DECLARATIONS_TRUNCATED\t" .. key .. "\n")) end
                    end
                    assert(f:write("END\n"))
                end)
                pcall(function() f:close() end)
            end
        end
        -- Small no-F9 health report; no file IO inside hit/audio hooks.
        if not C.last_report or os.time() ~= C.last_report then
            C.last_report = os.time()
            local f = io.open(OUTPUT_DIRECTORY .. "/combat_capture_live.txt", "wb")
            if f then
                pcall(function()
                    assert(f:write("schema=mhgz_combat_capture_v1\nversion=" .. SCRIPT_VERSION .. "\n" .. C.status ..
                        "\nmaster_gameobject_address=" .. (C.master_address or "unknown") ..
                        "\nsemantics=damage_side_callback_is_not_final_damage;hit_sound_context_is_not_independent_hit\n" ..
                        "time=last_completed_sample;native_qpc_sync_unknown\n" .. table.concat(C.install_lines, "\n") .. "\nEND\n"))
                end)
                pcall(function() f:close() end)
            end
        end
    end
    return C
end)()
-- END AUDIO COMBAT CAPTURE.

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
    native_audio.stop(state.native_audio_basename, state.capture_time)
    audio_capture.stop()
    combat_capture.stop()
    write_event("hit_capture_health", combat_capture.status .. ";rows=" .. combat_capture.rows ..
        ";file_error=" .. tostring(combat_capture.file_error))
    write_event("audio_capture_health", "listeners=" .. audio_capture.hook_count .. ";rows=" .. audio_capture.rows ..
        ";dropped=" .. audio_capture.dropped .. ";errors=" .. audio_capture.errors ..
        ";file_error=" .. tostring(audio_capture.error) .. ";source_scope=all_world_unattributed")
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
    local audio_probe_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_audio_probe.txt"
    local audio_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_audio.csv"
    local audio_hooks_path = OUTPUT_DIRECTORY .. "/" .. basename .. "_audio_hooks.txt"

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
    state.native_audio_basename = basename
    native_audio.start(basename, 0)
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
    local combat_ok, combat_result = pcall(combat_capture.start, basename)
    if not combat_ok or not combat_result then combat_capture.file_error = "hit_sidecar_start_failed" end
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
    for _, column in ipairs({ "stamina_available", "stamina_raw", "stamina_max_raw",
        "stamina_source", "stamina_error" }) do
        table.insert(sample_header, column)
    end
    state.stamina_read_status = nil
    samples_file:write(table.concat(sample_header, ",") .. "\n")
    state.sample_column_count = #sample_header
    events_file:write("event_index,capture_time_s,event,vertical_phase,details\n")
    write_event("capture_started", (trigger or "unknown trigger") .. "; raw hunter + primary camera world Transform capture")
    write_event("gamepad_input_schema", state.gamepad_backend .. "; buttons=" .. table.concat(gamepad_button_names, ","))
    write_event("animation_reader_schema", "v1.13.0; file=" .. animation_path ..
        ";frames=engine_native_unscaled;legacy_layers_are_not_attack_frame_truth")
    write_event("stamina_reader_schema", "v1.14.0; units=raw_unscaled; no implicit /30 or /150; match same-frame action/input")
    local audio_ok, audio_result, audio_error = pcall(audio_probe.write, audio_probe_path)
    if audio_ok and audio_result then
        write_event("audio_probe_metadata", "schema=v1; file=" .. audio_probe_path .. ";types=" ..
            audio_probe.type_count .. ";candidates=" .. audio_probe.candidate_count .. ";probe_only=true")
    else
        audio_probe.status = "[AUDIO PROBE ERROR] Metadata export failed; trajectory continues"
        write_event("audio_probe_error", tostring(audio_ok and audio_error or audio_result))
    end
    local started, result, err = pcall(audio_capture.start, audio_path)
    if not started or not result then
        audio_capture.status = "[AUDIO ERROR] Cannot open audio CSV; trajectory continues"
        write_event("audio_capture_error", tostring(err or result))
    else
        write_event("audio_capture_schema", "v1; file=" .. audio_path .. ";listeners=" .. audio_capture.hook_count ..
            ";unfiltered_getter=disabled;event_id_capture=not_implemented;post_return_semantics=unknown;time=last_completed_sample;source_scope=all_world_unattributed")
    end
    local report_ok, report_result, report_error = pcall(audio_capture.report, audio_hooks_path)
    if not report_ok or not report_result then write_event("audio_listener_report_error", tostring(report_error or report_result)) end
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

    local stamina = read_hunter_stamina(position.player)
    local stamina_status = stamina.available and "ok" or stamina.error
    if state.stamina_read_status ~= stamina_status then
        state.stamina_read_status = stamina_status
        write_event("stamina_read_status", stamina_status)
        show_toast(stamina.available and "[REC STAMINA OK] Raw values captured"
            or "[REC STAMINA WARNING] Values unavailable; see events CSV", 3.0,
            stamina.available and 0xFF00FF00 or 0xFF00FFFF)
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
    table.insert(row, csv_bool_number(stamina.available))
    table.insert(row, csv_number(stamina.current))
    table.insert(row, csv_number(stamina.maximum))
    table.insert(row, csv_text(stamina.source))
    table.insert(row, csv_text(stamina.error))
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
        draw.filled_rect(20, 20, 730, 86, 0xB0000000)
        draw.outline_rect(20, 20, 730, 86, 0xFF0000FF)
        draw.text(
            "[REC] TRAJECTORY CAPTURE  |  " .. capture_key .. ": STOP  |  Samples: " .. tostring(state.sample_count),
            32,
            34,
            0xFF0000FF
        )
        draw.text(state.health_message, 32, 52, state.health_state == "stalled" and 0xFF00AAFF or 0xFFFFFFFF)
        draw.text(animation_capture.status, 32, 69,
            animation_capture.last_status == "readable" and 0xFF00FF00 or 0xFF00AAFF)
        draw.text(native_audio.status, 32, 86, 0xFF00AAFF)
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
        draw.filled_rect(20, 112, 730, 48, 0xC0000000)
        draw.outline_rect(20, 112, 730, 48, state.toast_color)
        draw.text(state.toast_text, 32, 123, state.toast_color)
    end
end

local function draw_quick_control_widget()
    -- REFramework permits standalone ImGui windows from on_frame when wrapped
    -- in begin_window/end_window. Keep this small control visible even while
    -- the main REFramework menu is closed, so recording never depends on a key.
    imgui.set_next_window_pos({ 20, 166 }, 4)
    imgui.set_next_window_size({ 540, 245 }, 4)
    local contents_visible = imgui.begin_window("MHGZ Recorder Control", nil, nil)
    if contents_visible then
        imgui.text("Recorder v" .. SCRIPT_VERSION)
        imgui.text(native_audio.status)
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
    -- Descriptor-only discovery does not need a hunter, input or a new recording.
    audio_probe.auto_export()
    audio_context_preflight.auto_export()
    local combat_updated = pcall(combat_capture.update)
    if not combat_updated then combat_capture.status = "[HITS ERROR] Update failed; trajectory continues" end
    local native_updated = pcall(native_audio.update)
    if not native_updated then native_audio.status = "[AUDIO NATIVE ERROR] IPC update failed; trajectory continues" end
    local audio_updated = pcall(audio_capture.update)
    if not audio_updated then audio_capture.status = "[AUDIO ERROR] Health update failed; trajectory continues" end

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
        audio_capture.drain()
        combat_capture.drain()
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
    imgui.text(audio_probe.status)
    imgui.text(audio_context_preflight.status)
    imgui.text(audio_capture.status)
    imgui.text(native_audio.status)
    imgui.text(combat_capture.status)
    if audio_probe.last_path ~= nil then imgui.text("Audio interface report: " .. audio_probe.last_path) end
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
