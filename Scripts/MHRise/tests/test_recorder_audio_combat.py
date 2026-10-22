"""Execute the actual managed hit collector in isolated Lua, never in the game."""
import csv
import importlib
import io
from pathlib import Path
import unittest

TEXT = (Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua").read_text(encoding="utf-8-sig")
BLOCK = TEXT.split("-- BEGIN AUDIO COMBAT CAPTURE:", 1)[1].split("\n", 1)[1].split("-- END AUDIO COMBAT CAPTURE.", 1)[0]
HELPERS = "local function csv_text(value)" + TEXT.split("local function csv_text(value)", 1)[1].split("local function write_event", 1)[0]

HARNESS = r'''
OUTPUT_DIRECTORY='fixture';SCRIPT_VERSION='test'
state={capture_time=1.25,frame_callback_count=40,sample_count=30}
files={};hooks={};types={};clock=100;in_hook=false
os.clock=function() return clock end
function td(n,parent)
    local t={fields={},methods={},name=n}
    t.get_full_name=function() return n end
    t.get_parent_type=function() return parent end
    t.get_fields=function() return t.fields end
    t.get_methods=function() return t.methods end
    t.get_field=function(_,key) for _,f in ipairs(t.fields) do if f:get_name()==key then return f end end end
    types[n]=t;return t
end
function field(t,n,ft)
    local f={get_name=function() return n end,get_type=function() return ft end,
        is_static=function() return false end,get_data=function(_,o) return o.values[n] end}
    t.fields[#t.fields+1]=f;return f
end
function obj(t,addr,values)
    return {values=values or {},get_address=function() return addr end,get_type_definition=function() return t end,
        call=function(_,m) assert(m=='get_Name');return 'target,"quoted"' end}
end
function method(t,n)
    local m={get_name=function() return n end,get_declaring_type=function() return t end,
        is_static=function() return false end,get_return_type=function() return types['System.Void'] end,
        get_num_params=function() return 2 end,
        get_param_types=function() return {types['snow.hit.DamageFlowInfoBase'],types['snow.DamageReceiver.HitInfo']} end,
        get_function=function() return t.name end}
    t.methods[#t.methods+1]=m;return m
end
td('System.Void');td('System.Int32');td('System.Single');td('via.GameObject');td('via.vec3')
local enemy=td('snow.enemy.EnemyCharacterBase')
local sound=td('snow.wwise.WwisePlayerMonitoredParameters')
local flowtype=td('snow.hit.DamageFlowInfoBase')
local hit=td('snow.DamageReceiver.HitInfo')
local shape=td('Shape');field(shape,'AttackNo',types['System.Int32'])
field(flowtype,'Damage',types['System.Single'])
field(sound,'_MaterialType',types['System.Int32'])
field(hit,'<AttackObject>k__BackingField',types['via.GameObject'])
field(hit,'<DamageObject>k__BackingField',types['via.GameObject'])
field(hit,'<Position>k__BackingField',types['via.vec3'])
field(hit,'<AttackData>k__BackingField',shape)
field(hit,'<DamageData>k__BackingField',shape)
field(hit,'<HitDamageShapeInfo>k__BackingField',shape)
method(enemy,'afterCalcDamage_DamageSide');method(sound,'hitTrigger')
attacker=obj(types['via.GameObject'],'140123456789000')
target=obj(types['via.GameObject'],'140123456789888')
attack=obj(shape,'140000000000000',{AttackNo=3})
flow=obj(flowtype,'55',{Damage=45.5})
hitinfo=obj(hit,'77',{['<AttackObject>k__BackingField']=attacker,['<DamageObject>k__BackingField']=target,
    ['<Position>k__BackingField']={x=0,y=2.5,z=-4},['<AttackData>k__BackingField']=attack})
receiver=obj(enemy,'88');sound_receiver=obj(sound,'99',{_MaterialType=4})
args={'thread_token',receiver,flow,hitinfo}
function get_master_player_and_game_object() return receiver,attacker end
sdk={PreHookResult={CALL_ORIGINAL=123},find_type_definition=function(n) return types[n] end,
    to_int64=function(v) return 123 end,to_managed_object=function(v) return v end,
    hook=function(m,pre,post) hooks[#hooks+1]={method=m,pre=pre,post=post} end}
io={open=function(path,mode)
    assert(not in_hook,'file IO inside callback')
    if mode=='rb' then return files[path] and {close=function() return true end} end
    assert(mode=='wb')
    local f={data='',closed=false}
    function f:write(v) assert(not in_hook and not self.closed);self.data=self.data..v;return self end
    function f:flush() assert(not in_hook);return true end
    function f:close() self.closed=true;return true end
    files[path]=f;return f
end}
function hit_call(i,a)
    in_hook=true
    local value=hooks[i].pre(a or args)
    in_hook=false
    assert(value==123)
end
'''


class CombatCaptureTests(unittest.TestCase):
    def setUp(self):
        self.lua = importlib.import_module('lupa.lua54').LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(HARNESS)
        self.capture = self.lua.execute(HELPERS + BLOCK + '\nreturn combat_capture')

    def start(self):
        self.capture.update()
        self.assertTrue(self.capture.start('run'))

    def rows(self):
        return list(csv.DictReader(io.StringIO(self.lua.globals().files['fixture/run_hits.csv'].data)))

    def test_unique_exact_descriptors_and_only_two_hooks(self):
        self.capture.install(); self.capture.install()
        self.assertEqual(self.capture.installed, 2)
        self.assertEqual(len(self.lua.globals().hooks), 2)

    def test_reject_ambiguous_or_incomplete_signatures(self):
        self.lua.execute("local t=types['snow.enemy.EnemyCharacterBase'];method(t,'afterCalcDamage_DamageSide')")
        self.capture.install()
        self.assertEqual(self.capture.installed, 1)
        self.assertIn('exact_match_count=2', self.capture.install_lines[1])

    def test_reject_wrong_return_owner_or_parameter(self):
        for expression in (
            "m.get_return_type=function() return types['System.Int32'] end",
            "m.get_declaring_type=function() return types['Shape'] end",
            "m.get_param_types=function() return {types['Shape']} end",
        ):
            self.setUp()
            self.lua.execute("local m=types['snow.enemy.EnemyCharacterBase'].methods[1];"+expression)
            self.capture.install()
            self.assertEqual(self.capture.installed, 1)

    def test_reject_incomplete_enumeration_and_shared_native_address(self):
        self.lua.execute("local t=types['snow.enemy.EnemyCharacterBase'];for i=1,4096 do method(t,'other') end")
        self.capture.install()
        self.assertEqual(self.capture.installed, 1)
        self.assertIn('metadata_limit_or_invalid', self.capture.install_lines[1])
        self.setUp()
        self.lua.execute("for _,t in pairs(types) do for _,m in ipairs(t.methods) do m.get_function=function() return 'shared' end end end")
        self.capture.install()
        self.assertEqual(self.capture.installed, 1)
        self.assertIn('shared_function_rejected', self.capture.install_lines[2])

    def test_snapshot_while_alive_and_no_file_io_in_hook(self):
        self.start()
        self.lua.execute("hit_call(1);attack.values.AttackNo=99;hitinfo.values['<Position>k__BackingField'].y=999")
        self.capture.drain()
        row = self.rows()[0]
        self.assertIn('AttackNo:System.Int32=3', row['attack_shape_fields'])
        self.assertEqual(row['hit_y'], '2.50000000')
        self.assertEqual(row['hit_x'], '0.00000000')
        self.assertEqual(row['attacker_relation'], 'master_gameobject_exact')
        self.assertEqual(row['target_name'], 'target,"quoted"')
        self.assertEqual(row['attacker_address'], '140123456789000')
        self.assertEqual(row['read_errors'], 'none')
        self.assertIn('resolved_damage_unknown', row['outcome'])

    def test_sound_context_is_not_counted_as_independent_hit(self):
        self.start()
        self.lua.execute("hit_call(1);args[2]=sound_receiver;hit_call(2)")
        self.capture.drain()
        self.assertEqual([r['kind'] for r in self.rows()], ['damage_side_callback', 'hit_sound_context'])
        self.assertEqual(self.capture.hits, 1)
        self.assertEqual(self.capture.sound_contexts, 1)
        self.assertIn('_MaterialType:System.Int32=4', self.rows()[1]['sound_context_fields'])

    def test_bad_hit_type_does_not_replace_call_or_return_or_use_zero(self):
        self.start()
        self.lua.execute("args[4]=target;hit_call(1);local ret={};assert(rawequal(hooks[1].post(ret),ret))")
        self.capture.drain()
        self.assertIn('hit_type_mismatch', self.rows()[0]['read_errors'])
        self.assertEqual(self.rows()[0]['hit_x'], '')
        self.assertEqual(self.rows()[0]['target_address'], 'unknown')

    def test_outer_snapshot_failure_still_preserves_original(self):
        self.start()
        self.lua.execute("local a=setmetatable({}, {__index=function() error('unreadable arguments') end});hit_call(1,a);assert(hooks[1].post(nil)==nil)")
        self.assertEqual(self.capture.errors, 1)

    def test_unknown_master_and_closed_file_do_not_leak_old_hit(self):
        self.start()
        self.lua.execute("function get_master_player_and_game_object() return nil,nil end;clock=101")
        self.capture.update()
        self.lua.execute('hit_call(1)')
        self.capture.stop()
        self.assertEqual(self.rows()[0]['attacker_relation'], 'unknown')
        self.lua.execute('hit_call(1)')
        self.assertTrue(self.capture.start('next'))
        self.capture.drain()
        self.assertEqual(self.capture.rows, 0)

    def test_idle_no_rows_bounded_queue_and_explicit_loss(self):
        self.capture.install()
        self.lua.execute('hit_call(1)')
        self.assertEqual(self.capture.hits, 1)
        self.start()
        self.lua.execute('for i=1,514 do hit_call(1) end')
        self.capture.stop()
        self.assertEqual(self.capture.rows, 512)
        self.assertEqual(self.capture.dropped, 2)
        self.assertTrue(self.lua.globals().files['fixture/run_hits.csv'].closed)

    def test_file_collision_and_write_failure_isolated(self):
        self.start(); self.lua.execute('hit_call(1)'); self.capture.stop()
        previous = self.lua.globals().files['fixture/run_hits.csv'].data
        self.assertFalse(self.capture.start('run'))
        self.assertEqual(self.lua.globals().files['fixture/run_hits.csv'].data, previous)
        self.assertTrue(self.capture.start('next'))
        self.lua.execute("files['fixture/next_hits.csv'].write=function() return nil,'full' end;hit_call(1)")
        self.capture.drain()
        self.assertIsNone(self.capture.file)
        self.assertIsNotNone(self.capture.file_error)
        self.assertTrue(self.lua.globals().files['fixture/next_hits.csv'].closed)

    def test_luajit_userdata_metadata_vectors(self):
        lua = importlib.import_module('lupa.luajit21').LuaRuntime(unpack_returned_tuples=True)
        lua.execute(HARNESS)
        lua.execute(r'''
function vector(v) local o=newproxy(true);local m=getmetatable(o);m.__len=function() return #v end;m.__index=function(_,i) return v[i] end;return o end
for _,t in pairs(types) do local methods=t.methods;t.get_methods=function() return vector(methods) end end
''')
        capture = lua.execute(HELPERS + BLOCK + '\nreturn combat_capture')
        capture.install()
        self.assertEqual(capture.installed, 2)

    def test_sound_priority_fields_survive_unrelated_field_order(self):
        self.lua.execute("local t=types['snow.wwise.WwisePlayerMonitoredParameters'];for i=1,100 do field(t,'OtherHitDebug'..i,types['System.Int32']) end;"
                         "field(t,'_EffectMaterialType',types['System.Int32']);field(t,'_Dmg',types['System.Int32']);"
                         "sound_receiver.values._EffectMaterialType=9;sound_receiver.values._Dmg=3")
        self.start()
        self.lua.execute('args[2]=sound_receiver;hit_call(2)')
        self.capture.drain()
        row = self.rows()[0]
        self.assertIn('_EffectMaterialType:System.Int32=9',row['sound_context_fields'])
        self.assertIn('_Dmg:System.Int32=3',row['sound_context_fields'])
        self.assertIn('selected_field_limit=',row['field_coverage'])
        self.assertGreater(self.capture.incomplete,0)

    def test_sound_post_reads_updated_value_even_after_pre_row_drains(self):
        self.lua.execute("hook_storage={};thread={get_hook_storage=function() return hook_storage end,get_id=function() return 12345 end};"
                         "local t=types['snow.wwise.WwisePlayerMonitoredParameters'];field(t,'_EffectMaterialType',types['System.Int32']);field(t,'_Dmg',types['System.Int32']);"
                         "sound_receiver.values._EffectMaterialType=2;sound_receiver.values._Dmg=2")
        self.start()
        self.lua.execute('args[2]=sound_receiver;hit_call(2)')
        self.capture.drain()
        self.lua.execute('sound_receiver.values._Dmg=4;clock=102;assert(hooks[2].post(567)==567)')
        self.capture.drain()
        rows = self.rows()
        self.assertEqual(len(rows),2)
        self.assertEqual(rows[1]['kind'],'hit_sound_context_post')
        self.assertIn('_Dmg:System.Int32=2',rows[0]['sound_context_fields'])
        self.assertIn('_Dmg:System.Int32=4',rows[1]['sound_context_post_fields'])
        self.assertEqual(rows[1]['thread_id'],'12345')
        self.assertEqual(rows[0]['hit_info_address'],rows[1]['hit_info_address'])

    def test_ephemeral_hook_storage_supports_recursive_calls_and_new_session_rejection(self):
        self.lua.execute("storage_stack={{}};thread={get_hook_storage=function() return storage_stack[#storage_stack] end};"
                         "local t=types['snow.wwise.WwisePlayerMonitoredParameters'];field(t,'_EffectMaterialType',types['System.Int32']);field(t,'_Dmg',types['System.Int32']);"
                         "sound_receiver.values._EffectMaterialType=2;sound_receiver.values._Dmg=2")
        self.start()
        self.lua.execute('args[2]=sound_receiver;hit_call(2);storage_stack[2]={};hit_call(2);assert(hooks[2].post(1)==1);storage_stack[2]=nil;assert(hooks[2].post(2)==2)')
        self.capture.stop()
        self.assertEqual([r['kind'] for r in self.rows()],['hit_sound_context','hit_sound_context','hit_sound_context_post','hit_sound_context_post'])
        self.assertTrue(self.capture.start('next'))
        self.lua.execute('hooks[2].post(1)')
        self.capture.drain()
        self.assertEqual(self.capture.rows,0)

    def test_damage_rs_and_part_nested_fields_not_silently_discarded(self):
        self.lua.execute("local nested=td('snow.hit.PartDetail');field(nested,'PartsGroup',types['System.Int32']);"
                         "local rs=td('snow.hit.DamageRS');field(rs,'HitShape',nested);"
                         "field(types['snow.DamageReceiver.HitInfo'],'<DamageRSData>k__BackingField',rs);"
                         "hitinfo.values['<DamageRSData>k__BackingField']=obj(rs,'998',{HitShape=obj(nested,'999',{PartsGroup=7})})")
        self.start();self.lua.execute('hit_call(1)');self.capture.drain()
        self.assertIn('PartsGroup:System.Int32=7',self.rows()[0]['damage_rs_fields'])
        self.assertEqual(self.rows()[0]['damage_rs_type'],'snow.hit.DamageRS')

    def test_selected_base_field_beyond_four_parents_is_read(self):
        self.lua.execute("local p=td('PartBase');field(p,'PartsGroup',types['System.Int32']);"
                         "for i=1,6 do p=td('PartLayer'..i,p) end;attack=obj(p,'555',{PartsGroup=7})")
        self.start();self.lua.execute("hitinfo.values['<DamageData>k__BackingField']=attack;hit_call(1)");self.capture.drain()
        self.assertIn('PartsGroup:System.Int32=7',self.rows()[0]['damage_shape_fields'])

    def legacy_context(self):
        self.lua.execute("thread=nil; current_context='thread_token';sdk.get_thread_context=function() return current_context end;"
                         "args[2]=sound_receiver; local t=types['snow.wwise.WwisePlayerMonitoredParameters'];"
                         "field(t,'_EffectMaterialType',types['System.Int32']);field(t,'_Dmg',types['System.Int32']);"
                         "sound_receiver.values._EffectMaterialType=9;sound_receiver.values._Dmg=2")

    def test_legacy_host_without_thread_api_captures_post_after_pre_drain(self):
        self.legacy_context(); self.start()
        self.lua.execute('hit_call(2)'); self.capture.drain()
        self.lua.execute('sound_receiver.values._Dmg=4; assert(hooks[2].post(456)==456)'); self.capture.drain()
        self.assertEqual(self.capture.post_mode,'engine_context_stack')
        self.assertEqual([r['kind'] for r in self.rows()],['hit_sound_context','hit_sound_context_post'])
        self.assertIn('_Dmg:System.Int32=4',self.rows()[1]['sound_context_post_fields'])
        self.assertEqual(self.rows()[1]['thread_id'],'unknown')
        self.assertIsNone(self.capture.post_error)

    def test_legacy_recursive_and_interleaved_contexts_pair_their_own_frames(self):
        self.legacy_context(); self.start()
        self.lua.execute("hit_call(2); args[4]=obj(types['snow.DamageReceiver.HitInfo'],'78',hitinfo.values);hit_call(2);"
                         "current_context='other_context';args[1]=current_context;args[4]=obj(types['snow.DamageReceiver.HitInfo'],'79',hitinfo.values);hit_call(2);"
                         "assert(hooks[2].post(1)==1);current_context='thread_token';assert(hooks[2].post(2)==2);assert(hooks[2].post(3)==3)")
        self.capture.drain()
        self.assertEqual([r['hit_info_address'] for r in self.rows() if r['kind']=='hit_sound_context_post'],['79','78','77'])
        self.assertEqual(self.capture.sound_posts,3)

    def test_legacy_cross_capture_tombstone_and_epoch_rejection(self):
        self.legacy_context(); self.start(); self.lua.execute('hit_call(2)')
        self.capture.stop()
        self.lua.execute('hit_call(2); hooks[2].post(1)')
        self.assertTrue(self.capture.start('next'))
        self.lua.execute('hooks[2].post(2)'); self.capture.drain()
        self.assertEqual(self.capture.rows,0)
        self.assertEqual(self.capture.sound_posts,0)

    def test_missing_all_post_capabilities_is_visible_not_successful_silence(self):
        self.start(); self.capture.update()
        self.assertEqual(self.capture.post_mode,'unavailable')
        self.assertIn('POST_ERROR=post_storage_capability_unavailable',self.capture.status)
        self.lua.execute('args[2]=sound_receiver;hit_call(2);hooks[2].post(1)'); self.capture.drain()
        self.assertEqual(len(self.rows()),1)

    def test_legacy_context_mismatch_disables_pairing_without_altering_return(self):
        self.legacy_context(); self.start()
        self.lua.execute("current_context='different_context';hit_call(2);assert(hooks[2].post(8)==8)")
        self.capture.drain()
        self.assertTrue(self.capture.post_disabled)
        self.assertIn('post_thread_context_argument_mismatch',self.capture.post_error)
        self.assertEqual(len(self.rows()),1)

    def test_legacy_depth_limit_is_bounded_and_keeps_outer_pairing(self):
        self.legacy_context(); self.start()
        self.lua.execute('for i=1,35 do hit_call(2) end; for i=1,35 do hooks[2].post(i) end')
        self.capture.drain()
        self.assertEqual(self.capture.post_failures,3)
        self.assertEqual(self.capture.sound_posts,32)
        self.assertEqual(len(self.rows()),67)

    def test_real_flow_66_scalar_profile_keeps_stamina_and_stun(self):
        self.lua.execute("local t=types['snow.hit.DamageFlowInfoBase'];for i=1,63 do field(t,'OtherDamage'..i,types['System.Single']);flow.values['OtherDamage'..i]=i end;"
                         "field(t,'<StaminaDamage>k__BackingField',types['System.Single']);flow.values['<StaminaDamage>k__BackingField']=12;"
                         "field(t,'<StunDamage>k__BackingField',types['System.Single']);flow.values['<StunDamage>k__BackingField']=13")
        self.start(); self.lua.execute('hit_call(1)'); self.capture.drain()
        self.assertIn('<StaminaDamage>k__BackingField:System.Single=12',self.rows()[0]['flow_fields'])
        self.assertIn('<StunDamage>k__BackingField:System.Single=13',self.rows()[0]['flow_fields'])
        self.assertNotIn('selected_field_limit',self.rows()[0]['field_coverage'])

    def test_container_only_part_is_not_reported_as_missing_scalar(self):
        self.lua.execute("local t=td('snow.hit.damage_receiver.HitDamageShapeInfo');field(t,'<AttackCollider>k__BackingField',td('via.physics.Collider'));"
                         "field(t,'_DamageShapeDataList',td('System.Collections.Generic.List'));"
                         "hitinfo.values['<HitDamageShapeInfo>k__BackingField']=obj(t,'999')")
        self.start(); self.lua.execute('hit_call(1)'); self.capture.drain()
        self.assertEqual(self.rows()[0]['part_info_fields'],'container_only_unexpanded')
        self.assertIn('part:container_only_unexpanded',self.rows()[0]['field_coverage'])

    def test_whole_script_compiles_both_runtimes(self):
        for module in ('lupa.lua54', 'lupa.luajit21'):
            lua = importlib.import_module(module).LuaRuntime()
            self.assertTrue(lua.eval('function(s) local f,e=(loadstring or load)(s);assert(f,e);return true end')(TEXT))


if __name__ == '__main__':
    unittest.main()
