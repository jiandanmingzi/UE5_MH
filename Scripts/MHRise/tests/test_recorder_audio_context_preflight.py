"""Execute actual context-preflight Lua; never install a hook or invoke game code."""
import importlib
from pathlib import Path
import unittest

TEXT = (Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua").read_text(encoding="utf-8-sig")
BLOCK = TEXT.split("-- BEGIN AUDIO CONTEXT PREFLIGHT:", 1)[1].split("\n", 1)[1].split(
    "-- END AUDIO CONTEXT PREFLIGHT.", 1)[0]
from lupa.lua54 import LuaRuntime

HARNESS = r'''
SCRIPT_VERSION='1.17.1'; OUTPUT_DIRECTORY='fixture'; files={}; lookups=0
function td(name) return {get_full_name=function() return name end} end
function method(name)
    return {get_name=function() return name end,
        get_num_params=function() return 2 end,
        get_param_types=function() return {td('via.wwise.WwiseSwitchParam'),td('System.UInt64')} end,
        get_return_type=function() return td('System.Void') end,
        get_declaring_type=function() return td('via.wwise.WwiseManager') end,
        is_static=function() return true end,
        get_function=function() return '00007FF684620123' end,
        call=function() error('must never invoke a game method') end}
end
methods={method('setSwitch'),method('unrelatedSetter')}
fields={{get_name=function() return 'GroupId' end,
    get_type=function() return td('System.UInt32') end,
    get_offset_from_base=function() return 0 end,
    get_data=function() error('must not read live fields') end}}
manager=td('via.wwise.WwiseManager')
manager.get_methods=function() return methods end
manager.get_fields=function() return fields end
param=td('via.wwise.WwiseSwitchElement')
param.get_methods=function() return {} end
param.get_fields=function() return fields end
sdk={find_type_definition=function(name)
    lookups=lookups+1
    if name=='via.wwise.WwiseManager' then return manager end
    if name=='via.wwise.WwiseSwitchElement' then return param end
end,hook=function() error('must not install hooks') end,
call_native_func=function() error('must not call game code') end,
to_managed_object=function() error('must not read native arguments') end}
io={open=function(path,mode)
    assert(mode=='wb')
    local f={data='',closed=false}
    function f:write(value) self.data=self.data..value;return self end
    function f:flush() return self end
    function f:close() self.closed=true;return true end
    files[path]=f;return f
end}
'''


class ContextPreflightTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(HARNESS)
        self.probe = self.lua.execute(BLOCK + '\nreturn audio_context_preflight')

    def test_signature_address_and_zero_offset_are_metadata_only(self):
        report = self.probe.inspect()
        self.assertIn('setSwitch\tindex=1\towner=via.wwise.WwiseManager', report)
        self.assertIn('signature_complete=true\tfunction=00007FF684620123', report)
        self.assertIn('offset=0\tvalue=not_read', report)
        self.assertNotIn('unrelatedSetter', report)
        self.assertIn('hooks_installed=0;live_values_read=0;game_methods_invoked=0', report)
        self.assertIn('initial_conditions=unknown;final_media=unknown', report)
        self.assertIn('damage_capture=not_implemented', report)

    def test_missing_types_and_addresses_are_explicit(self):
        self.lua.execute('methods[1].get_function=function() error("unavailable") end')
        report = self.probe.inspect()
        self.assertIn('function=unknown\taddress_error=', report)
        self.assertIn('TYPE\tsnow.hit.AttackHitInfo\tunavailable', report)
        self.assertNotIn('hook_verified=true', report)

    def test_field_offset_failure_reports_its_reason_without_reading_value(self):
        self.lua.execute("fields[1].get_offset_from_base=function() error('offset unavailable') end")
        report = self.probe.inspect()
        self.assertIn('offset=unknown\tvalue=not_read\toffset_error=', report)
        self.assertIn('offset unavailable', report)

    def test_wrong_owner_or_missing_parameters_never_complete(self):
        self.lua.execute("methods[1].get_declaring_type=function() return td('Other') end")
        self.assertIn('signature_complete=false', self.probe.inspect())
        self.lua.execute('methods[1].get_param_types=function() return nil end')
        self.assertIn('signature_complete=false', self.probe.inspect())

    def test_actual_luajit_userdata_metadata_vectors(self):
        runtime = importlib.import_module('lupa.luajit21').LuaRuntime(unpack_returned_tuples=True)
        runtime.execute(HARNESS)
        runtime.execute(r'''
function vector(values)
    local v=newproxy(true)
    local mt=getmetatable(v)
    mt.__len=function() return #values end
    mt.__index=function(_,i) return values[i] end
    return v
end
local params=vector(methods[1]:get_param_types())
methods[1].get_param_types=function() return params end
methods=vector(methods); fields=vector(fields)
''')
        probe = runtime.execute(BLOCK + '\nreturn audio_context_preflight')
        report = probe.inspect()
        self.assertIn('signature_complete=true', report)
        self.assertIn('methods=complete\tfields=complete', report)
        self.assertIn('offset=0\tvalue=not_read', report)

    def test_enumeration_limit_and_container_error_are_explicit(self):
        self.lua.execute("methods={};for i=1,2050 do methods[i]=method('setState') end;"
                         "param.get_fields=function() return 'bad' end")
        report = self.probe.inspect()
        self.assertIn('methods=truncated_2050', report)
        self.assertNotIn('index=2049', report)
        self.assertIn('fields=unavailable_container', report)

    def test_hit_type_uses_hit_damage_filter_and_no_getters_called(self):
        self.lua.execute("local old=sdk.find_type_definition;sdk.find_type_definition=function(name) "
                         "if name=='snow.enemy.EnemyCharacterBase' then return {"
                         "get_methods=function() return {method('afterCalcDamage_DamageSide'),method('setActive')} end,"
                         "get_fields=function() return {} end} end;return old(name) end")
        report = self.probe.inspect()
        self.assertIn('afterCalcDamage_DamageSide', report)
        self.assertNotIn('setActive', report)

    def test_startup_without_f9_runs_once_and_closes_file(self):
        self.probe.auto_export(); self.probe.auto_export()
        self.assertEqual(self.lua.globals().lookups, 15)
        files = self.lua.globals().files
        self.assertEqual(len(list(files.keys())), 1)
        self.assertTrue(files[self.probe.last_path].closed)
        self.assertIn('native status', self.probe.status)

    def test_write_failure_closes_file_without_repeated_attempts(self):
        self.lua.execute("local old=io.open;io.open=function(path,mode) local f=old(path,mode);"
                         "f.write=function() return nil,'disk_full' end;return f end")
        self.probe.auto_export(); self.probe.auto_export()
        self.assertIn('ERROR', self.probe.status)
        self.assertIsNone(self.probe.last_path)
        self.assertEqual(self.lua.globals().lookups, 15)
        self.assertTrue(next(iter(self.lua.globals().files.values())).closed)

    def test_close_failure_does_not_report_saved(self):
        self.lua.execute("local old=io.open;io.open=function(path,mode) local f=old(path,mode);"
                         "f.close=function() return nil,'close_failed' end;return f end")
        self.probe.auto_export()
        self.assertIn('ERROR', self.probe.status)
        self.assertIsNone(self.probe.last_path)

    def test_whole_script_compiles_in_lua54_and_luajit(self):
        for module in ('lupa.lua54', 'lupa.luajit21'):
            runtime = importlib.import_module(module).LuaRuntime()
            self.assertTrue(runtime.eval('function(s) local f,e=(loadstring or load)(s);assert(f,e);return true end')(TEXT))


if __name__ == '__main__':
    unittest.main()
