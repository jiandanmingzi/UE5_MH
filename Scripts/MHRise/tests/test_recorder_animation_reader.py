"""Execute the real Lua reader with read-only REFramework doubles (needs lupa).

No game process, native hook, installation write, or original capture is used.
"""
import csv
import importlib
import io
from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua"
TEXT = SOURCE.read_text(encoding="utf-8-sig")
try:
    from lupa.lua54 import LuaRuntime
except ImportError:
    LuaRuntime = None

HARNESS = r'''
state = { recording=true, sample_count=1, frame_callback_count=1 }
events, files, hooks = {}, {}, {}
local serial = 100
function object(kind, values)
    serial = serial + 1
    local obj = { values=values or {}, address=serial }
    local td = {}
    function td:get_full_name() return kind end
    function td:get_methods() return {} end
    function td:get_parent_type() return nil end
    function td:get_method(signature)
        if not signature:find("changeMotion",1,true) then return nil end
        return { get_address=function() return signature end }
    end
    function obj:get_address() return self.address end
    function obj:get_type_definition() return td end
    function obj:call(name, ...)
        assert(not name:find("set_",1,true) and not name:find("changeMotion",1,true), "game setter called")
        local value = self.values[name]
        if value == nil then error("missing getter: "..name) end
        if type(value)=="function" then return value(...) end
        return value
    end
    return obj
end
function node(id, frame, weight)
    return object("via.motion.MotionNode", {
        get_MotionBankID=100, get_MotionBankType=0, get_MotionID=id,
        get_MotionName="clip,"..id..'"', get_Frame=frame, get_EndFrame=276,
        get_Weight=weight, get_Speed=2 })
end
function scene(nodes, count_known, highest_index)
    local layer = object("via.motion.Layer", {
        get_Frame=12.5, get_EndFrame=276, get_Speed=2, get_Weight=1,
        get_HighestWeightMotionNode=function() return nodes[highest_index or 1] end,
        getMotionNode=function(i) assert(i < #nodes); return nodes[i+1] end })
    if count_known then layer.values.get_MotionNodeCount=#nodes end
    local motion=object("via.motion.Motion", {get_Layer={layer}, get_PlaySpeed=1})
    local go=object("via.GameObject", {get_Name="Hunter", ["getComponent(System.Type)"]=motion})
    current_go=go
    return layer,motion,go
end
function get_master_player_and_game_object() return {}, current_go end
function write_event(name, details) events[#events+1]={name=name, details=details} end
sdk = {
    typeof=function(name) assert(name=="via.motion.Motion"); return name end,
    hook=function(method, pre, post) hooks[#hooks+1]={pre=pre,post=post} end,
    to_managed_object=function(value) return value end,
    to_float=function(value) return value end, to_int64=function(value) return value end,
    PreHookResult={CALL_ORIGINAL=123} }
io = { open=function(path, mode)
    if mode=="r" then return nil end
    assert(mode=="w")
    local f = { data="", closed=false }
    function f:write(text) assert(not self.closed); self.data=self.data..text; return self end
    function f:flush() assert(not self.closed) end
    function f:close() self.closed=true end
    files[path]=f
    return f
end }
'''


@unittest.skipIf(LuaRuntime is None, "Install lupa in an isolated test environment")
class AnimationReaderTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(HARNESS)
        helpers = TEXT.split("local function csv_text(value)", 1)[1].split("local function write_event", 1)[0]
        reader = TEXT.split("-- BEGIN DIRECT MOTION READER", 1)[1].split("\n", 1)[1].split("-- END DIRECT MOTION READER.", 1)[0]
        self.reader = self.lua.execute("local function csv_text(value)" + helpers + reader +
                                       "\nreturn animation_capture")
        self.assertTrue(self.reader.start("animation.csv"))

    def sample(self):
        self.reader.sample(1, 0.1, 160)
        data = self.lua.globals().files["animation.csv"].data
        return list(csv.DictReader(io.StringIO(data)))

    def test_whole_script_compiles_in_both_lua_runtimes(self):
        for module_name in ("lupa.lua54", "lupa.luajit21"):
            runtime = importlib.import_module(module_name).LuaRuntime()
            result = runtime.eval("function(s) local f,e=(loadstring or load)(s); assert(f,e); return true end")(TEXT)
            self.assertTrue(result, module_name)

    def test_whole_script_initializes_without_a_game_scene(self):
        self.lua.execute(r'''
sdk.find_type_definition=function() return nil end
sdk.get_native_singleton=function() return nil end
sdk.get_managed_singleton=function() return nil end
re = {on_application_entry=function() end,on_frame=function() end,
      on_draw_ui=function() end,on_script_reset=function() end}
log = {info=function() end,error=function() end,warn=function() end}
''')
        self.lua.execute(TEXT)

    def test_actual_motion_node_not_behavior_placeholder(self):
        self.lua.execute("scene({node(160, 7.25, 1)}, true)")
        rows = self.sample()
        self.assertEqual(rows[0]["motion_id"], "160")
        self.assertEqual(rows[0]["frame"], "7.25")
        self.assertEqual(rows[0]["frame_source"], "node")
        self.assertEqual(rows[0]["motion_name"], 'clip,160"')
        self.assertEqual(self.reader.valid_samples, 1)

    def test_each_blend_participant_keeps_its_own_frame_and_weight(self):
        self.lua.execute("scene({node(160, 5.5, .4), node(118, 93.25, .6)}, true, 2)")
        rows = self.sample()
        self.assertEqual(len(rows), 2)
        self.assertEqual([r["frame"] for r in rows], ["5.5", "93.25"])
        self.assertEqual([r["node_weight"] for r in rows], ["0.4", "0.6"])
        self.assertEqual([r["highest_weight"] for r in rows], ["0", "1"])

    def test_missing_node_frames_do_not_borrow_one_layer_clock_for_two_clips(self):
        self.lua.execute("scene({node(160,nil,.4),node(118,nil,.6)},true,2)")
        rows = self.sample()
        self.assertTrue(all(r["frame"] == "" for r in rows))
        self.assertEqual(rows[0]["layer_frame"], "12.5")
        self.assertEqual(self.reader.valid_samples, 0)
        self.assertIn("WARNING", self.reader.status)

    def test_single_enumerated_clip_can_use_labelled_layer_clock(self):
        self.lua.execute("scene({node(160,nil,1)},true)")
        row = self.sample()[0]
        self.assertEqual(row["frame_source"], "single_node_layer")
        self.assertEqual(row["frame"], "12.5")
        self.assertEqual(row["node_frame"], "")

    def test_unknown_node_count_only_reads_documented_index0_and_highest(self):
        self.lua.execute("scene({node(160,2,.4),node(118,90,.6)},false,2)")
        rows = self.sample()
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(r["node_enumeration"] == "slot0_and_highest_only" for r in rows))

    def test_placeholder_and_missing_component_never_report_animation_success(self):
        self.lua.execute("local n=node(1,.5,1); n.values.get_MotionBankID=0; n.values.get_EndFrame=1; scene({n},true)")
        self.assertEqual(self.sample()[0]["read_status"], "incomplete_or_placeholder")
        self.assertEqual(self.reader.valid_samples, 0)
        self.lua.execute("current_go=nil")
        self.sample()
        self.assertIn("WARNING", self.reader.status)

    def test_change_request_hook_is_filtered_and_leaves_call_and_return_untouched(self):
        self.lua.execute("test_layer=scene({node(160,2,1)},true)")
        self.sample()
        hook = self.lua.globals().hooks[1]
        args = self.lua.table_from({2: self.lua.globals().test_layer, 3: 100, 4: 160, 5: 35.0})
        self.assertEqual(hook.pre(args), 123)
        self.assertEqual(hook.post(7654), 7654)
        self.sample()
        events = self.lua.globals().events
        requests = [events[i].details for i in range(1, len(events)+1)
                    if events[i].name == "animation_change_requested"]
        self.assertEqual(len(requests), 1)
        self.assertIn("requested_start_frame=35", requests[0])
        self.lua.execute("foreign=object('via.motion.Layer',{})")
        args[2] = self.lua.globals().foreign
        hook.pre(args)
        self.assertEqual(len(self.reader.requests), 0)

    def test_respawn_reacquires_objects_and_stop_closes_file(self):
        self.lua.execute("scene({node(160,2,1)},true)")
        self.sample()
        self.lua.execute("scene({node(118,45,1)},true)")
        self.assertEqual(self.sample()[-1]["motion_id"], "118")
        self.reader.stop()
        self.assertTrue(self.lua.globals().files["animation.csv"].closed)
        self.assertEqual(len(self.reader.layer_addresses), 0)

    def test_last_change_request_is_flushed_even_without_another_sample(self):
        self.lua.execute("test_layer=scene({node(160,2,1)},true)")
        self.sample()
        args = self.lua.table_from({2: self.lua.globals().test_layer, 3: 100, 4: 160, 5: 0.0})
        self.lua.globals().hooks[1].pre(args)
        self.reader.stop()
        events = self.lua.globals().events
        self.assertTrue(any(events[i].name == "animation_change_requested"
                            and "requested_start_frame=0" in events[i].details
                            for i in range(1, len(events)+1)))

    def test_actual_trajectory_builder_keeps_legacy_header_and_tail_alignment(self):
        helpers = TEXT.split("local function csv_text(value)", 1)[1].split("local function write_event", 1)[0]
        header = "local sample_header = {" + TEXT.split("local sample_header = {", 1)[1].split("samples_file:write", 1)[0]
        sampling = TEXT.split("local function record_sample()", 1)[1]
        row = "local row = {" + sampling.split("local row = {", 1)[1].split("assert(#row", 1)[0]
        code = "local function csv_text(value)" + helpers + r'''
local state = {sample_count=1,capture_time=.1,gamepad_snapshot={buttons={},pressed={},released={}}}
local position, camera = {x=1,y=2,z=3}, nil
local motion_layers={layers={},player_motion_old_id_ok=true,player_motion_old_id=160}
local function captured_motion_value(ok,value) if ok then return value end end
local gamepad_button_names={'A','Y','RT'}
local MOTION_LAYER_CAPTURE_COUNT=5
local function gamepad_button_column_name(name) return name end
''' + header + row + "\nreturn table.concat(sample_header,','), table.concat(row,',')"
        header_text, row_text = self.lua.execute(code)
        fields, values = next(csv.reader([header_text])), next(csv.reader([row_text]))
        self.assertEqual(len(fields), len(values))
        parsed = dict(zip(fields, values))
        self.assertEqual(float(parsed["player_motion_old_id"]), 160)
        self.assertEqual(parsed["input_gamepad_button_A_down"], "")
        self.assertEqual(fields[-35], "motion_l0_bank_id")

    def test_nil_hid_cells_and_false_flags_keep_fixed_csv_width(self):
        helpers = TEXT.split("local function csv_text(value)", 1)[1].split("local function write_event", 1)[0]
        code = "local function csv_text(value)" + helpers + r'''
local row = { csv_bool_number(nil), csv_bool_number(false), csv_bool_number(true) }
return #row, table.concat(row, ','), csv_text(false)
'''
        count, row, flag = self.lua.execute(code)
        self.assertEqual((count, row, flag), (3, ",0,1", '"false"'))


if __name__ == "__main__":
    unittest.main()
