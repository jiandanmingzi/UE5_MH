"""Run the actual Lua audio metadata block; no game, real hook or file write."""
import importlib
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua"
TEXT = SOURCE.read_text(encoding="utf-8-sig")
BLOCK = TEXT.split("-- BEGIN AUDIO CAPABILITY PROBE:", 1)[1].split("\n", 1)[1].split(
    "-- END AUDIO CAPABILITY PROBE.", 1)[0]
try:
    from lupa.lua54 import LuaRuntime
except ImportError:
    LuaRuntime = None

HARNESS = r'''
SCRIPT_VERSION='1.15.1'
OUTPUT_DIRECTORY='test_recorder'
files={}
lookups=0
function td(name)
    return {get_full_name=function() return name end}
end
function method(name,count)
    return {
        get_name=function() return name end,
        get_num_params=function() return count end,
        get_param_types=function() return {td('System.UInt32'),td('via.wwise.WwiseEventParam')} end,
        get_param_names=function() return {'eventId','parameters'} end,
        get_return_type=function() return td('System.UInt32') end,
        is_static=function() return false end,
        call=function() error('must never invoke audio method') end,
    }
end
methods={method('postEvent',2)}
fields={{get_name=function() return 'EventId' end,
    get_type=function() return td('System.UInt32') end,
    get_data=function() error('must never read a live object field') end}}
manager=td('via.wwise.WwiseManager')
manager.get_methods=function() return methods end
manager.get_fields=function() return fields end
manager.get_parent_type=function() return nil end
sdk={find_type_definition=function(name)
    lookups=lookups+1
    if name=='via.wwise.WwiseManager' then return manager end
    return nil
end,
hook=function() error('probe must not install native hooks') end,
call_native_func=function() error('probe must not invoke game code') end,
to_managed_object=function() error('probe must not dereference native arguments') end}
io={open=function(path,mode)
    assert(mode=='w')
    local f={data='',closed=false}
    function f:write(data) self.data=self.data..data; return self end
    function f:flush() end
    function f:close() self.closed=true end
    files[path]=f
    return f
end}
'''


@unittest.skipIf(LuaRuntime is None, "Use the existing isolated lupa dependency")
class AudioProbeTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute("native_io=io")
        self.lua.execute(HARNESS)
        self.probe = self.lua.execute(BLOCK + "\nreturn audio_probe")

    def test_signature_and_field_metadata_without_game_calls(self):
        result = self.probe.inspect()
        self.assertIn("postEvent\tstatic=false\treturn=System.UInt32", result)
        self.assertIn("params=System.UInt32 eventId;via.wwise.WwiseEventParam parameters", result)
        self.assertIn("signature_complete=true", result)
        self.assertIn("value=not_read", result)
        self.assertIn("hook_verified=false", result)
        self.assertEqual(self.probe.type_count, 1)
        self.assertEqual(self.probe.candidate_count, 1)

    def test_missing_types_are_not_event_capture_success(self):
        self.lua.execute("sdk.find_type_definition=function() return nil end")
        result = self.probe.inspect()
        self.assertEqual(self.probe.type_count, 0)
        self.assertEqual(self.probe.candidate_count, 0)
        self.assertIn("probe_installs_audio_hooks=0", result)
        self.assertIn("runtime_listeners=see_audio_hooks_report", result)
        self.assertIn("via.wwise.WwiseManager\tmissing", result)

    def test_metadata_lookup_is_cached_not_polled_per_sample(self):
        self.probe.write("first.txt")
        count = self.lua.globals().lookups
        self.probe.write("second.txt")
        self.assertEqual(count, 13)
        self.assertEqual(self.lua.globals().lookups, count)
        self.assertTrue(self.lua.globals().files["first.txt"].closed)
        self.assertEqual(self.lua.globals().files["first.txt"].data, self.lua.globals().files["second.txt"].data)

    def test_missing_parameter_metadata_stays_incomplete(self):
        self.lua.execute("methods[1].get_param_types=function() return nil end")
        result = self.probe.inspect()
        self.assertIn("signature_complete=false", result)
        self.assertNotIn("System.UInt32 eventId", result)

    def test_broken_descriptor_is_reported_without_invoking_anything(self):
        self.lua.execute("manager.get_methods=function() error('not supported') end")
        self.assertIn("METHODS_UNAVAILABLE", self.probe.inspect())

    def test_missing_calling_metadata_is_not_marked_complete(self):
        self.lua.execute("methods[1].is_static=function() return nil end;"
                         "methods[1].get_return_type=function() return nil end")
        result = self.probe.inspect()
        self.assertIn("static=unknown", result)
        self.assertIn("signature_complete=false", result)

    def test_real_userdata_vectors_for_methods_fields_and_parameters(self):
        root = SOURCE.parents[2] / "Saved/Maintenance/AudioProbe1151Tests"
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            self.lua.globals().fixture_root = Path(directory).as_posix()
            self.lua.execute(r'''
local serial=0
local function vector(values)
    serial=serial+1
    local file=assert(native_io.open(fixture_root..'/'..serial..'.tmp','w'))
    file:close()
    debug.setmetatable(file,{__len=function() return #values end,
        __index=function(_,index) return values[index] end})
    assert(type(file)=='userdata')
    return file
end
local parameters=vector(methods[1]:get_param_types())
local names=vector(methods[1]:get_param_names())
methods[1].get_param_types=function() return parameters end
methods[1].get_param_names=function() return names end
methods=vector(methods)
fields=vector(fields)
''')
            result = self.probe.inspect()
            self.assertIn("params=System.UInt32 eventId;via.wwise.WwiseEventParam parameters", result)
            self.assertIn("signature_complete=true", result)
            self.assertIn("FIELD\tvia.wwise.WwiseManager\tEventId", result)
            self.assertNotIn("METHODS_UNAVAILABLE\tvia.wwise.WwiseManager", result)

    def test_bad_descriptor_container_is_not_an_empty_list(self):
        self.lua.execute("manager.get_methods=function() return '' end")
        self.assertIn("unsupported container: string", self.probe.inspect())

    def test_enumeration_caps_and_parameter_limit(self):
        self.lua.execute("methods={};fields={};for i=1,250 do methods[i]=method('play'..i,17);fields[i]={} end")
        result = self.probe.inspect()
        self.assertEqual(self.probe.candidate_count, 192)
        self.assertIn("METHOD_LIMIT", result)
        self.assertIn("FIELD_LIMIT", result)
        self.assertNotIn("signature_complete=true", result)

    def test_report_open_failure_is_explicit(self):
        self.lua.execute("io.open=function() return nil,'denied' end")
        ok, error = self.probe.write("blocked.txt")
        self.assertFalse(ok)
        self.assertEqual(error, "denied")
        self.assertIsNone(self.probe.last_path)

    def test_failed_write_still_closes_handle(self):
        self.lua.execute("local previous=io.open;io.open=function(path,mode) local f=previous(path,mode);"
                         "f.write=function() return nil,'disk full' end;return f end")
        ok, error = self.probe.write("broken.txt")
        self.assertFalse(ok)
        self.assertIn("disk full", error)
        self.assertTrue(self.lua.globals().files["broken.txt"].closed)

    def test_startup_report_does_not_need_f9_and_runs_once(self):
        self.probe.auto_export()
        self.probe.auto_export()
        files = self.lua.globals().files
        self.assertEqual(len(list(files.keys())), 1)
        self.assertTrue(self.probe.last_path.endswith("_boot.txt"))
        self.assertEqual(self.lua.globals().lookups, 13)

    def test_startup_failure_does_not_repeat_each_frame(self):
        self.lua.execute("io.open=function() error('denied') end")
        self.probe.auto_export()
        self.probe.auto_export()
        self.assertIn("ERROR", self.probe.status)
        self.assertEqual(self.lua.globals().lookups, 13)

    def test_entire_recorder_compiles_in_lua54_and_luajit(self):
        for name in ("lupa.lua54", "lupa.luajit21"):
            runtime = importlib.import_module(name).LuaRuntime()
            self.assertTrue(runtime.eval(
                "function(s) local f,e=(loadstring or load)(s);assert(f,e);return true end")(TEXT))


if __name__ == "__main__":
    unittest.main()
