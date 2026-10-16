"""Focused launcher/type-adapter checks; no UE process or gameplay suite."""

import argparse
from pathlib import Path
import subprocess
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ue_asset_utils as values
import ue_remote
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "AssetOrganization"))
import run_m6_ui_remote


class Reflected:
    def __init__(self, **fields):
        self.fields = fields

    def get_editor_property(self, name):
        return self.fields[name]


class ReflectedArray:
    def __iter__(self):
        return iter((1, 2, 3))


class ReflectedMap:
    def items(self):
        return [("Slash", 45)]

    def __iter__(self):
        return iter(("Slash",))


class FakeClient:
    def __init__(self, nodes, result=None, error=None):
        self.remote_nodes = nodes
        self.result = result or {"success": True, "result": "None", "output": []}
        self.error = error
        self.stopped = False
        self.command = None
        self.connected = None

    def start(self):
        pass

    def stop(self):
        self.stopped = True

    def open_command_connection(self, node):
        self.connected = node

    def run_command(self, command, exec_mode):
        self.command = command
        if self.error:
            raise self.error
        return self.result


class MaintenanceToolsTests(unittest.TestCase):
    def test_latest_tujin_shared_entry_discards_only_unused_tail(self):
        import importlib.util
        path = Path(__file__).resolve().parents[1] / 'AssetOrganization/setup_montage_segment_blend.py'
        spec = importlib.util.spec_from_file_location('segment_blend_for_pure_test', path)
        subject = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {'unreal': types.SimpleNamespace()}):
            spec.loader.exec_module(subject)
        recipe = {'segments': [{} for _ in range(6)], 'sections': [
            {'name': 'Entry', 'time': 0, 'next': 'Charge'},
            {'name': 'Charge', 'time': .4, 'next': 'Core'},
            {'name': 'Core', 'time': 2.4, 'next': ''},
            {'name': 'Entry_From_Idle', 'time': 4.2, 'next': 'Charge'},
            {'name': 'Release_From_Idle', 'time': 4.2 + 55/120, 'next': 'Core'}],
            'notifies': [{'start': 3.5, 'end': 4.2, 'old_index': 6}]}
        recipe['segments'][4] = {
            'asset': '/Game/AS_Unsh_TuJinHuiXuan.AS_Unsh_TuJinHuiXuan',
            'source_start': 0, 'source_end': 55/60, 'play_rate': 1}
        recipe['segments'][5] = dict(recipe['segments'][4], source_start=55/60, source_end=67/60)
        updated = subject.current_tujin_idle_recipe(recipe)
        self.assertEqual(recipe['segments'][4]['source_start'], 0)
        self.assertEqual(len(recipe['segments']), 6)
        self.assertEqual(updated['segments'], recipe['segments'][:4])
        self.assertEqual(updated['sections'], recipe['sections'][:3])
        self.assertEqual(updated['notifies'], recipe['notifies'])
        recipe['segments'][4]['source_start'] = 14/60
        self.assertEqual(subject.current_tujin_idle_recipe(recipe), updated)
        recipe['notifies'][0]['end'] = 4.3
        with self.assertRaises(RuntimeError):
            subject.current_tujin_idle_recipe(recipe)

    def test_native_export_bom_decoding_is_lossless(self):
        text = 'Begin Object\n中文显示名\nEnd Object\n'
        self.assertEqual(values.decode_native_export(text.encode('utf-16')), text)
        self.assertEqual(values.decode_native_export(text.encode('utf-8-sig')), text)
        self.assertEqual(values.decode_native_export(text.replace('\n', '\r\n').encode('utf-16')), text)

    def test_attack_hold_removal_merges_only_equivalent_sections_and_clamps_dead_tail(self):
        snapshot = {'segments': [
            {'asset': 'Hold1', 'start': 0, 'duration': .05, 'source_start': 0, 'source_end': .05, 'play_rate': 1, 'loops': 1},
            {'asset': 'Hold2', 'start': .05, 'duration': .05, 'source_start': 0, 'source_end': .1, 'play_rate': 1, 'loops': 1},
            {'asset': 'Body', 'start': .1, 'duration': 1, 'source_start': 0, 'source_end': 2, 'play_rate': 1, 'loops': 1}],
            'length': 1.1,
            'sections': [{'SectionName': 'Special', 'NextSectionName': 'Core'},
                         {'SectionName': 'Core', 'LinkValue': '.05'}],
            'notifies': [{'start': .2, 'end': 1.2, 'row': {'NotifyName': 'DodgeAcceptWindow'}}]}
        recipe = values.montage_removal_recipe(snapshot, {'Hold1', 'Hold2'}, {'Special': 'Core'}, True)
        self.assertEqual(recipe['sections'], [{'name': 'Core', 'next': '', 'time': 0.0}])
        self.assertEqual(recipe['segments'][0]['source_end'], 2)
        self.assertAlmostEqual(recipe['notifies'][0]['start'], .1)
        self.assertAlmostEqual(recipe['notifies'][0]['end'], 1)
        snapshot['notifies'][0]['row']['NotifyName'] = 'AttackCollision'
        with self.assertRaises(ValueError):
            values.montage_removal_recipe(snapshot, {'Hold1', 'Hold2'}, {'Special': 'Core'}, True)
        snapshot['sections'][1]['LinkValue'] = '.2'
        with self.assertRaises(ValueError):
            values.montage_removal_recipe(snapshot, {'Hold1', 'Hold2'}, {'Special': 'Core'}, True)

    def test_bridge_removal_preserves_source_cuts_and_retimes_authored_windows(self):
        snapshot = {'segments': [
            {'asset': 'A', 'start': 0, 'duration': 0.2, 'source_start': 0, 'source_end': 0.4, 'play_rate': 1, 'loops': 1},
            {'asset': 'Blend', 'start': 0.2, 'duration': 0.05, 'source_start': 0, 'source_end': 0.05, 'play_rate': 1, 'loops': 1},
            {'asset': 'B', 'start': 0.25, 'duration': 0.3, 'source_start': 1, 'source_end': 1.6, 'play_rate': 1, 'loops': 1}],
            'sections': [{'SectionName': 'Entry'}, {'SectionName': 'Core', 'LinkValue': '0.25'}],
            'notifies': [{'start': 0.01, 'end': 0.55}, {'start': 0.3, 'end': 0.4}]}
        result = values.montage_removal_recipe(snapshot, {'Blend'})
        self.assertEqual([clip['asset'] for clip in result['segments']], ['A', 'B'])
        self.assertEqual(result['segments'][1]['source_start'], 1)
        self.assertAlmostEqual(result['sections'][1]['time'], 0.2)
        self.assertAlmostEqual(result['notifies'][0]['end'], 0.5)
        self.assertAlmostEqual(result['notifies'][1]['start'], 0.25)
        self.assertFalse(result['notifies'][1]['clone'])
        with self.assertRaises(ValueError):
            values.montage_removal_recipe(snapshot, {'Missing'})
        snapshot['notifies'][1]['start'] = 0.225
        with self.assertRaises(ValueError):
            values.montage_removal_recipe(snapshot, {'Blend'})

    def test_native_montage_parser_handles_nested_fields_and_quoted_commas(self):
        text = '   Notifies(0)=(NotifyName="A,B",EndLink=(LinkValue=1.2,Name="X"),Duration=0.1)\n'
        rows = values.export_records(text, 'Notifies')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['NotifyName'], '"A,B"')
        self.assertEqual(values.export_fields(rows[0]['EndLink'])['LinkValue'], '1.2')

    def session(self, client, probe=True):
        remote = types.SimpleNamespace(RemoteExecutionConfig=lambda: None,
                                       RemoteExecution=lambda config: client,
                                       MODE_EXEC_STATEMENT="ExecuteStatement")
        args = argparse.Namespace(project_root=ue_remote.PROJECT_ROOT, discovery_timeout=0.01,
                                  probe=probe, script=Path(__file__), script_arg=[])
        events = []
        result = ue_remote.run_session(args, remote, events.append)
        self.assertTrue(client.stopped)
        return result, events

    def node(self, name="correct", root=None):
        return {"node_id": name, "project_root": str(root or ue_remote.PROJECT_ROOT)}

    def test_unreal_array_adapter_and_error_dictionary(self):
        self.assertEqual(values.as_sequence(ReflectedArray()), [1, 2, 3])
        for invalid in ("array", {"read_error": "missing"}, ReflectedMap(), None):
            with self.assertRaises(TypeError):
                values.as_sequence(invalid)

    def test_maps_scalable_values_and_missing_property(self):
        self.assertEqual(values.map_values({"Slash": 45}), {"Slash": 45})
        self.assertEqual(values.map_values(ReflectedMap()), {"Slash": 45})
        with self.assertRaises(TypeError):
            values.map_values({"read_error": "missing"})
        self.assertEqual(values.scalable_float_value(Reflected(value=10, curve="None"))["value"], 10)
        error = values.read_field_or_error(Reflected(), "missing")
        self.assertTrue(values.is_read_error(error))
        self.assertEqual(values.scalable_float_value(error), error)

    def test_blueprint_cdo_rejects_uncompiled_asset(self):
        unreal = types.SimpleNamespace(get_default_object=lambda generated: generated)
        with self.assertRaises(RuntimeError):
            values.blueprint_cdo(types.SimpleNamespace(generated_class=lambda: None), unreal)
        with self.assertRaises(TypeError):
            values.blueprint_cdo(None, unreal)

    def test_legacy_ui_probe_forwards_paths_without_swallowing_values(self):
        with patch.object(run_m6_ui_remote.ue_remote, "main", return_value=0) as run:
            self.assertEqual(run_m6_ui_remote.main(["--probe", "--engine-root", "C:/UE Test", "--timeout", "2"]), 0)
        forwarded = run.call_args.args[0]
        self.assertTrue(forwarded[0].endswith("probe_m6_ui.py"))
        self.assertEqual(forwarded[forwarded.index("--engine-root") + 1], str(Path("C:/UE Test")))
        self.assertEqual(forwarded[forwarded.index("--discovery-timeout") + 1], "2.0")

    def test_matching_project_has_no_ui_bridge_requirement(self):
        client = FakeClient([self.node("other", "C:/other"), self.node()])
        result, _ = self.session(client)
        self.assertTrue(result["success"])
        self.assertEqual(client.connected, "correct")
        self.assertNotIn("build_e63_widget_blueprint", client.command)

    def test_wire_payload_compiles_in_ue_single_statement_mode(self):
        for probe in (True, False):
            client = FakeClient([self.node()])
            result, _ = self.session(client, probe=probe)
            self.assertTrue(result["success"])
            compile(client.command, "ue_single_statement", "single")

    def test_missing_editor_is_discovery_failure(self):
        client = FakeClient([self.node("other", "C:/other")])
        result, _ = self.session(client)
        self.assertEqual(result["error_type"], "EditorNotFound")
        self.assertIsNone(client.command)

    def test_duplicate_editors_never_execute(self):
        client = FakeClient([self.node("one"), self.node("two")])
        result, _ = self.session(client)
        self.assertFalse(result["success"])
        self.assertEqual(result["stage"], "discovery")
        self.assertIsNone(client.command)

    def test_remote_script_failure_is_not_success(self):
        result, _ = self.session(FakeClient([self.node()], {"success": False, "result": "Traceback"}))
        self.assertFalse(result["success"])
        self.assertEqual(result["error_type"], "RemoteScriptError")

    def test_lost_reply_reports_unknown_outcome(self):
        result, _ = self.session(FakeClient([self.node()], error=OSError("lost reply")))
        self.assertTrue(result["outcome_unknown"])
        self.assertEqual(result["stage"], "execution")

    def test_worker_timeout_does_not_imply_command_cancelled(self):
        error = subprocess.TimeoutExpired(["worker"], 1, output=b'{"stage":"execution"}\n')
        with patch.object(ue_remote.subprocess, "run", side_effect=error):
            result = ue_remote.run_worker(["worker"], 1)
        self.assertTrue(result["outcome_unknown"])
        self.assertFalse(result["success"])

    def test_script_arguments_and_process_state_are_restored(self):
        old_argv, old_path = sys.argv, sys.path[:]
        captured = {}
        script = Path("quoted path ' and space.py")
        def run_path(path, run_name):
            captured.update(path=path, argv=sys.argv[:], name=run_name)
            raise RuntimeError("script failed")
        with patch("runpy.run_path", side_effect=run_path):
            with self.assertRaisesRegex(RuntimeError, "script failed"):
                exec(ue_remote.script_command(script, ["--asset=/Game/A B"]), {})
        self.assertIs(sys.argv, old_argv)
        self.assertEqual(sys.path, old_path)
        self.assertEqual(captured["argv"][1], "--asset=/Game/A B")
        self.assertEqual(captured["name"], "__main__")

    def test_argparse_success_exit_is_not_a_remote_failure(self):
        with patch("runpy.run_path", side_effect=SystemExit(0)):
            exec(ue_remote.script_command(Path(__file__), []), {})
        with patch("runpy.run_path", side_effect=SystemExit(2)):
            with self.assertRaises(SystemExit):
                exec(ue_remote.script_command(Path(__file__), []), {})


if __name__ == "__main__":
    unittest.main()
