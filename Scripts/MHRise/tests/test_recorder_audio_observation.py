"""Execute actual scalar-only Lua listeners; no game or native hooks required."""
import csv
import io
from pathlib import Path
import tempfile
import unittest

try:
    from lupa.lua54 import LuaRuntime
except ImportError:
    LuaRuntime = None

TEXT = (Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua").read_text(encoding="utf-8-sig")
BLOCK = TEXT.split("-- BEGIN AUDIO OBSERVATION:", 1)[1].split("\n", 1)[1].split("-- END AUDIO OBSERVATION.", 1)[0]
HELPERS = "local function csv_text(value)" + TEXT.split("local function csv_text(value)", 1)[1].split("local function write_event", 1)[0]
HARNESS = r'''
SCRIPT_VERSION='test'
OUTPUT_DIRECTORY='recorder_test'
state={capture_time=1.25,frame_callback_count=15,sample_count=12}
hooks,files,methods,enumerated_methods={},{},{},{}
clock=10
os.clock=function() return clock end
function td(name) return {get_full_name=function() return name end} end
function make_method(owner,signature,types)
    local m={signature=signature}
    m.get_name=function() return signature:match('^([^%(]+)') end
    m.get_num_params=function() return #types end
    m.is_static=function() return owner=='via.wwise.WwiseManager' end
    m.get_return_type=function() return td('System.UInt32') end
    m.get_param_types=function()
        local p={};for i,t in ipairs(types) do p[i]=td(t) end;return p
    end
    m.get_declaring_type=function() return td(owner) end
    m.get_function=function() return m.address or signature end
    methods[signature]=m
    enumerated_methods[#enumerated_methods+1]=m
    return m
end
make_method('via.wwise.WwiseManager','postEvent(via.GameObject,via.wwise.RequestInfo)',{'via.GameObject','via.wwise.RequestInfo'})
make_method('via.wwise.WwiseManager','postEvent(via.GameObject,via.wwise.RequestInfo,System.UInt32)',{'via.GameObject','via.wwise.RequestInfo','System.UInt32'})
make_method('via.wwise.WwiseManager','postEvent(via.GameObject,via.wwise.RequestInfo,System.UInt32,System.UInt32)',{'via.GameObject','via.wwise.RequestInfo','System.UInt32','System.UInt32'})
make_method('via.wwise.WwiseEventParam','get_EventId()',{})
sdk={PreHookResult={CALL_ORIGINAL=123},
    find_type_definition=function(owner)
        assert(owner=='via.wwise.WwiseManager','must not look up event getter')
        return {
            get_methods=function() return enumerated_methods end,
            get_method=function() error('prototype lookup must not be used') end}
    end,
    hook=function(m,pre,post) hooks[#hooks+1]={method=m,pre=pre,post=post} end,
    to_int64=function(value)
        if value=='conversion_error' then error('bad return') end
        if type(value)=='table' then return value.number end
        if type(value)=='string' then return 100 end
        return value
    end,
    call_native_func=function() error('must not call audio code') end,
    to_managed_object=function() error('must not read native objects') end}
io={open=function(path,mode)
    assert(mode=='w')
    local f={data='',closed=false}
    function f:write(data) assert(not self.closed);self.data=self.data..data;return self end
    function f:flush() assert(not self.closed);return true end
    function f:close() self.closed=true;return true end
    files[path]=f;return f
end}
'''


@unittest.skipIf(LuaRuntime is None, "Use existing isolated lupa")
class AudioObservationTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute("native_io=io")
        self.lua.execute(HARNESS)
        self.capture = self.lua.execute(HELPERS + BLOCK + "\nreturn audio_capture")

    def start(self):
        self.assertTrue(self.capture.start("audio.csv"))

    def rows(self):
        return list(csv.DictReader(io.StringIO(self.lua.globals().files["audio.csv"].data)))

    def test_enumerated_descriptors_work_without_prototype_lookup(self):
        self.capture.install()
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 3)
        self.assertEqual(len(self.lua.globals().hooks), 3)
        self.assertTrue(all("postEvent" in h.method.signature for h in self.lua.globals().hooks.values()))

    def test_mismatched_return_type_not_hooked(self):
        self.lua.execute("enumerated_methods[1].get_return_type=function() return td('System.String') end")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 2)
        self.assertIn("return_type:System.String", "\n".join(self.capture.diagnostics.values()))

    def test_wrong_parameter_types_not_hooked(self):
        self.lua.execute("methods['postEvent(via.GameObject,via.wwise.RequestInfo)'].get_param_types=function() return {td('System.String'),td('System.String')} end")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 2)
        self.assertIn("parameter_type[1]:System.String", "\n".join(self.capture.diagnostics.values()))

    def test_shared_native_address_not_hooked_twice(self):
        self.lua.execute("for _,m in pairs(methods) do m.address='shared' end")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 1)

    def test_missing_and_unhookable_methods_are_reported_not_retried(self):
        self.lua.execute("table.remove(enumerated_methods,1);sdk.hook=function() error('not hookable') end")
        self.capture.install()
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 0)
        self.assertIn("exact_match_count:0", self.capture.install_lines[1])
        self.assertIn("not hookable", self.capture.install_lines[2])

    def test_pre_never_reads_or_modifies_arguments(self):
        self.capture.install()
        self.lua.execute("local args=setmetatable({}, {__index=function() error('argument read') end,__newindex=function() error('argument write') end});assert(hooks[1].pre(args)==123)")

    def test_exact_original_retval_even_when_conversion_fails(self):
        self.start()
        self.lua.execute("local original={number=7};assert(rawequal(hooks[1].post(original),original));assert(hooks[1].post('conversion_error')=='conversion_error')")
        self.assertEqual(self.capture.errors, 1)

    def test_no_io_or_native_calls_in_callback(self):
        self.start()
        self.lua.execute("files['audio.csv'].write=function() error('callback IO') end;hooks[1].post(7)")
        self.assertEqual(self.capture.errors, 0)

    def test_callback_while_idle_writes_no_rows(self):
        self.capture.install()
        self.lua.execute("hooks[1].post(7)")
        self.assertEqual(self.capture.observed, 1)
        self.assertEqual(self.capture.rows, 0)
        self.assertEqual(len(list(self.lua.globals().files.keys())), 0)

    def test_post_raw_return_never_relabelled_as_event_id(self):
        self.start()
        self.lua.execute("clock=10.01;hooks[1].post(900);hooks[2].post(12345)")
        self.capture.drain()
        rows = self.rows()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["post_return_id"], "900")
        self.assertEqual(rows[0]["event_id"], "")
        self.assertEqual(rows[1]["event_id"], "")
        self.assertEqual(rows[1]["post_return_id"], "12345")
        self.assertEqual(rows[1]["kind"], "post_event_return")
        self.assertEqual(rows[1]["frame_index"], "15")
        self.assertEqual(rows[1]["last_sample_index"], "12")
        self.assertEqual(float(rows[1]["capture_time_s"]), 1.25)
        self.assertEqual(rows[1]["source_scope"], "all_world_unattributed")

    def test_unsigned_conversion(self):
        self.start()
        self.lua.execute("hooks[1].post(-1)")
        self.capture.drain()
        self.assertEqual(self.rows()[0]["post_return_id"], "4294967295")

    def test_queue_has_hard_limit_and_reports_drops(self):
        self.start()
        self.lua.execute("for i=1,2050 do hooks[1].post(i) end")
        self.capture.stop()
        self.assertEqual(self.capture.rows, 2048)
        self.assertEqual(self.capture.dropped, 2)

    def test_stop_drains_closes_and_next_session_does_not_leak(self):
        self.start()
        self.lua.execute("hooks[1].post(7)")
        self.capture.stop()
        self.assertTrue(self.lua.globals().files["audio.csv"].closed)
        self.assertEqual(len(self.rows()), 1)
        self.start()
        self.capture.stop()
        self.assertEqual(self.rows(), [])

    def test_write_failure_stays_local_and_stops_enqueueing(self):
        self.start()
        self.lua.execute("hooks[1].post(7);files['audio.csv'].write=function() return nil,'disk full' end")
        self.capture.drain()
        self.assertFalse(self.capture.recording)
        self.assertIn("ERROR", self.capture.status)
        self.capture.stop()
        self.assertTrue(self.lua.globals().files["audio.csv"].closed)

    def test_failed_open_does_not_start_capture(self):
        self.lua.execute("io.open=function() return nil,'denied' end")
        ok, err = self.capture.start("audio.csv")
        self.assertFalse(ok)
        self.assertEqual(err, "denied")
        self.assertFalse(self.capture.recording)

    def test_listener_report_disclaims_unproven_semantics(self):
        self.assertTrue(self.capture.report("hooks.txt")[0])
        body = self.lua.globals().files["hooks.txt"].data
        self.assertIn("installed=3", body)
        self.assertIn("observed_callbacks=0", body)
        self.assertIn("post_return_semantics=unknown", body)
        self.assertIn("unfiltered_event_getter=disabled", body)
        self.assertIn("event_id_capture=not_implemented", body)
        self.assertIn("schema=mhgz_audio_listeners_v2", body)

    def test_getter_is_never_hooked_even_if_it_has_activity(self):
        self.capture.install()
        self.assertTrue(all(h.method.signature != "get_EventId()" for h in self.lua.globals().hooks.values()))

    def test_true_userdata_method_and_parameter_vectors(self):
        root = Path(__file__).resolve().parents[3] / "Saved/Maintenance/AudioObserve1161Tests"
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            self.lua.globals().fixture_root = Path(directory).as_posix()
            self.lua.execute(r'''
local serial=0
local function vector(values)
    serial=serial+1
    local u=assert(native_io.open(fixture_root..'/'..serial..'.tmp','w'))
    u:close()
    debug.setmetatable(u,{__len=function() return #values end,__index=function(_,i) return values[i] end})
    assert(type(u)=='userdata');return u
end
for _,m in ipairs(enumerated_methods) do
    local values=vector(m:get_param_types());m.get_param_types=function() return values end
end
enumerated_methods=vector(enumerated_methods)
''')
            self.capture.install()
            self.assertEqual(self.capture.hook_count, 3)

    def test_more_than_old_192_method_limit_still_finds_real_descriptors(self):
        self.lua.execute("for i=1,335 do table.insert(enumerated_methods,1,{get_name=function() return 'unrelated' end}) end")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 3)
        self.assertIn("index=336", self.capture.install_lines[1])

    def test_enumeration_is_bounded_and_fails_closed(self):
        self.lua.execute("enumerated_methods=setmetatable({}, {__len=function() return 513 end,__index=function() error('out of bounds') end})")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 0)
        self.assertIn("method_list_limit_or_invalid:513", self.capture.install_lines[1])

    def test_bad_method_list_is_explicit(self):
        self.lua.execute("enumerated_methods=''")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 0)
        self.assertIn("method_list_unavailable:string", self.capture.install_lines[1])

    def test_ambiguous_matches_not_arbitrarily_selected(self):
        self.lua.execute("enumerated_methods[#enumerated_methods+1]=enumerated_methods[1]")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 2)
        self.assertIn("exact_match_count:2", self.capture.install_lines[1])

    def test_unreadable_descriptor_fails_closed(self):
        self.lua.execute("enumerated_methods[4].get_name=function() error('broken descriptor') end")
        self.capture.install()
        self.assertEqual(self.capture.hook_count, 0)
        self.assertIn("method_descriptor_unreadable:index=4", self.capture.install_lines[1])

    def test_owner_and_static_flag_have_distinct_diagnostics(self):
        self.lua.execute("enumerated_methods[1].get_declaring_type=function() return td('Wrong.Owner') end;enumerated_methods[2].is_static=function() return false end")
        self.capture.install()
        text = "\n".join(self.capture.diagnostics.values())
        self.assertIn("declaring_type:Wrong.Owner", text)
        self.assertIn("static_flag:false", text)
        self.assertEqual(self.capture.hook_count, 1)

    def test_failed_discovery_cached_across_three_targets(self):
        self.lua.execute("discoveries=0;sdk.find_type_definition=function() discoveries=discoveries+1;return nil end")
        self.capture.install()
        self.capture.install()
        self.assertEqual(self.lua.globals().discoveries, 1)
        self.assertIn("type_unavailable", self.capture.install_lines[1])

    def test_live_report_without_f9_records_first_activity(self):
        self.capture.update()
        self.assertEqual(len(list(self.lua.globals().files.keys())), 1)
        self.lua.execute("hooks[1].post(99)")
        self.capture.update()
        live = [f for path, f in self.lua.globals().files.items() if path.endswith('_live.txt')]
        self.assertEqual(len(live), 1)
        self.assertIn("observed_callbacks=1", live[0].data)
        self.assertIn("ACTIVITY", live[0].data)
        self.assertEqual(self.capture.rows, 0)

    def test_zero_activity_live_report_can_be_followed_by_first_activity(self):
        self.capture.update()
        self.lua.execute("clock=12.1")
        self.capture.update()
        live_path = next(path for path in self.lua.globals().files.keys() if path.endswith('_live.txt'))
        self.assertIn("observed_callbacks=0", self.lua.globals().files[live_path].data)
        self.lua.execute("hooks[2].post(99)")
        self.capture.update()
        self.assertIn("observed_callbacks=1", self.lua.globals().files[live_path].data)
        self.lua.execute("hooks[2].post(100)")
        self.capture.update()
        self.assertIn("observed_callbacks=1", self.lua.globals().files[live_path].data)

    def test_failed_auto_report_not_retried_each_frame(self):
        self.lua.execute("opens=0;io.open=function() opens=opens+1;return nil,'denied' end")
        self.capture.update()
        self.capture.update()
        self.assertEqual(self.lua.globals().opens, 1)
        self.lua.execute("clock=13")
        self.capture.update()
        self.capture.update()
        self.assertEqual(self.lua.globals().opens, 2)
        self.assertIn("ERROR", self.capture.status)

    def test_write_failure_persists_in_overlay_and_is_not_retried(self):
        self.capture.update()
        self.start()
        self.lua.execute("hooks[1].post(7);writes=0;files['audio.csv'].write=function() writes=writes+1;return nil,'disk full' end")
        self.capture.drain()
        self.capture.update()
        self.capture.drain()
        self.assertEqual(self.lua.globals().writes, 1)
        self.assertIn("ERROR", self.capture.status)


if __name__ == "__main__":
    unittest.main()
