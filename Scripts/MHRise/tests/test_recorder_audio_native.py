"""Execute the recorder's actual native client, with in-memory file IPC only."""
from pathlib import Path
import tempfile
import unittest

from lupa.lua54 import LuaRuntime

TEXT = (Path(__file__).resolve().parents[1] / "MHGZ_AerialTrajectoryRecorder.lua").read_text(encoding="utf-8-sig")
BLOCK = TEXT.split("-- BEGIN NATIVE AUDIO CLIENT\n", 1)[1].split("-- END NATIVE AUDIO CLIENT", 1)[0]
HARNESS = r'''
OUTPUT_DIRECTORY='fixture'
state={recording=true,capture_time=1.25}
clock,seconds,files,events=10,100,{},{}
os.clock=function() return clock end
os.time=function() return seconds end
os.date=function() return 'epoch' end
function write_event(kind,body) events[#events+1]={kind,body} end
io={open=function(path,mode)
    if mode=='rb' then
        if not files[path] then return nil end
        return {read=function(self,count) return files[path]:sub(1,count) end,close=function() end}
    end
    files[path]=''
    return {write=function(self,body) files[path]=body;return self end,close=function() end}
end}
function step(delta) clock=clock+(delta or 0.11) end
function acknowledge(extra)
    local command=files['fixture/audio_native_bridge_command.txt']
    local nonce,seq=command:match('V1\n([^\n]+)\n([^\n]+)\n')
    files['fixture/audio_native_bridge_status.txt']='schema=mhgz_rise_native_audio_v1\nnonce='..nonce..
      '\nseq='..seq..'\nready=1\nrecording=1\nobserved=5\nrows=4\ndropped=0\nerror=\n'..(extra or '')..'END\n'
end
'''

class NativeClientTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(HARNESS)
        self.client = self.lua.execute(BLOCK + "\nreturn native_audio")

    def command(self):
        return self.lua.globals().files['fixture/audio_native_bridge_command.txt']

    def ack(self, extra=''):
        self.lua.globals().acknowledge(extra)
        self.lua.globals().step()
        self.client.update()

    def test_ping_must_be_acknowledged_before_f9_start(self):
        self.client.update()
        ping = self.command()
        self.client.start('capture01', 0)
        self.lua.globals().step()
        self.client.update()
        self.assertEqual(self.command(), ping)
        self.ack()
        self.assertIn('\nSTART\ncapture01\n1.250000000\nEND\n', self.command())

    def test_start_stop_order_and_no_heartbeat_overwrite(self):
        self.client.start('capture01', 0)
        self.client.stop('capture01', 2)
        self.client.update()
        self.ack()
        self.assertIn('\nSTART\n', self.command())
        start = self.command()
        self.lua.globals().seconds += 2
        self.lua.globals().step()
        self.client.update()
        self.assertEqual(self.command(), start)
        self.ack('anchor_qpc=123\nqpc_frequency=1000\ncapture_anchor=1.25\n')
        self.assertIn('\nSTOP\n', self.command())
        self.assertEqual(self.lua.globals().events[1][1], 'native_audio_anchor')

    def test_stale_and_partial_status_cannot_claim_ready(self):
        self.client.update()
        self.lua.globals().files['fixture/audio_native_bridge_status.txt'] = (
            'schema=mhgz_rise_native_audio_v1\nnonce=old\nseq=1\nready=1\nEND\n')
        self.lua.globals().step()
        self.client.update()
        self.assertFalse(self.client.ready)
        self.lua.globals().acknowledge('')
        self.lua.globals().files['fixture/audio_native_bridge_status.txt'] = (
            self.lua.globals().files['fixture/audio_native_bridge_status.txt'][:-3])
        self.lua.globals().step()
        self.client.update()
        self.assertFalse(self.client.ready)

    def test_missing_plugin_timeout_does_not_change_trajectory_state(self):
        self.client.update()
        self.lua.globals().seconds += 5
        self.lua.globals().step()
        self.client.update()
        self.assertFalse(self.client.ready)
        self.assertIn('No matching DLL acknowledgement', self.client.status)
        self.assertTrue(self.lua.globals().state.recording)

    def test_status_uses_actual_rows_not_listener_install_count(self):
        self.client.update()
        self.ack()
        self.assertTrue(self.client.ready)
        self.assertEqual(self.client.rows, 4)
        self.assertIn('observed=5', self.client.status)

    def test_condition_capability_and_error_do_not_masquerade_as_post_failure(self):
        self.client.update()
        self.ack('context_ready=1\ncontext_rows=12\ncontext_observed=45\ncontext_dropped=2\ncontext_error=disk_full\n')
        self.assertTrue(self.client.ready)
        self.assertTrue(self.client.context_ready)
        self.assertEqual(self.client.context_rows, 12)
        self.assertIn('context=12 rows/45 observed', self.client.status)
        self.assertIn('CONTEXT ERROR: disk_full', self.client.status)

    def test_old_dll_is_explicitly_context_unavailable(self):
        self.client.update(); self.ack()
        self.assertTrue(self.client.ready)
        self.assertFalse(self.client.context_ready)
        self.assertIn('context unavailable', self.client.status)

    def test_whole_script_compiles_in_both_supported_runtimes(self):
        for module in ('lupa.lua54', 'lupa.luajit21'):
            runtime = __import__(module, fromlist=['LuaRuntime']).LuaRuntime()
            self.assertTrue(runtime.eval('function(s) local f,e=load(s);return f~=nil,e end')(TEXT)[0])

    def test_real_windows_file_ipc_uses_lf_not_text_mode_crlf(self):
        fixture = Path(__file__).resolve().parents[3] / 'Saved/Audio/RiseIG/NativeBridge/LuaFixtures'
        fixture.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='mhgz_audio_ipc_', dir=fixture) as temp:
            runtime = LuaRuntime(unpack_returned_tuples=True)
            runtime.execute("state={recording=false,capture_time=0}")
            runtime.globals().OUTPUT_DIRECTORY = Path(temp).as_posix()
            client = runtime.execute(BLOCK + '\nreturn native_audio')
            client.update()
            data = (Path(temp) / 'audio_native_bridge_command.txt').read_bytes()
            self.assertNotIn(b'\r', data)
            self.assertTrue(data.endswith(b'\nEND\n'))

if __name__ == '__main__':
    unittest.main()
