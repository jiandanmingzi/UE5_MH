"""Run an Unreal Editor Python file through Epic's Remote Execution client."""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import time
from pathlib import Path


REMOTE_EXECUTION_MODULE = Path(
    r"C:\apps\UE5\UE_5.6\Engine\Plugins\Experimental\PythonScriptPlugin"
    r"\Content\Python\remote_execution.py"
)
PROJECT_ROOT = Path(__file__).resolve().parents[2]


def load_remote_execution():
    spec = importlib.util.spec_from_file_location(
        "ue_remote_execution", REMOTE_EXECUTION_MODULE
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load Epic Remote Execution client: {REMOTE_EXECUTION_MODULE}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def normalize_path(value: str) -> str:
    return os.path.normcase(os.path.normpath(value)).rstrip("\\/")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "script",
        nargs="?",
        default=str(PROJECT_ROOT / "Scripts/AssetOrganization/setup_m6_ui_widgets.py"),
        help="Python file to execute inside the matching Unreal Editor process.",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument(
        "--probe",
        action="store_true",
        help="Read in-memory widget assets and reflected enum names without modifying assets.",
    )
    args = parser.parse_args()

    script_path = Path(args.script).resolve()
    if not script_path.is_file():
        parser.error(f"Remote script does not exist: {script_path}")

    remote_execution = load_remote_execution()
    config = remote_execution.RemoteExecutionConfig()
    config.multicast_group_endpoint = ("239.0.0.1", 6766)
    config.multicast_bind_address = "127.0.0.1"
    client = remote_execution.RemoteExecution(config)
    expected_root = normalize_path(str(PROJECT_ROOT))

    try:
        client.start()
        deadline = time.monotonic() + args.timeout
        selected = None
        while time.monotonic() < deadline:
            nodes = client.remote_nodes
            matches = [
                node
                for node in nodes
                if normalize_path(node.get("project_root", "")) == expected_root
            ]
            if len(matches) == 1:
                selected = matches[0]
                break
            if len(matches) > 1:
                raise RuntimeError(
                    "More than one matching MHGZ editor is discoverable; refusing to choose."
                )
            time.sleep(0.25)

        if selected is None:
            raise RuntimeError(
                "No matching MHGZ editor answered Remote Execution discovery. "
                f"Discovered nodes: {client.remote_nodes!r}"
            )

        print(
            "Matched Unreal Editor: "
            f"{selected.get('project_name')} at {selected.get('project_root')} "
            f"(UE {selected.get('engine_version')})"
        )
        client.open_command_connection(selected["node_id"])

        probe = client.run_command(
            "str({'bridge': hasattr(unreal, 'MHGZUIAssetSetupLibrary'), "
            "'builder': hasattr(unreal.MHGZUIAssetSetupLibrary, 'build_e63_widget_blueprint') "
            "if hasattr(unreal, 'MHGZUIAssetSetupLibrary') else False})",
            exec_mode=remote_execution.MODE_EVAL_STATEMENT,
        )
        print(f"Bridge probe: {probe!r}")
        if not probe.get("success") or "'builder': True" not in str(probe.get("result")):
            raise RuntimeError("The compiled E6.3 editor bridge is not available in this editor.")

        if args.probe:
            inspect_command = (
                "str({'unreal_names': [n for n in dir(unreal) if "
                "'AttributeBar' in n or 'MHGZ' in n], "
                "'enum_members': [n for n in dir(unreal.MHGZAttributeBarKind) "
                "if not n.startswith('_')], "
                "'enum_object': str(unreal.load_object(None, "
                "'/Script/MHGZ.EMHGZAttributeBarKind')) "
                "if hasattr(unreal, 'load_object') else 'load_object unavailable', "
                "'asset_paths': {name: unreal.EditorAssetLibrary.does_asset_exist("
                "folder + '/' + name) for name, (folder, _) in "
                "{'WBP_HUD': ('/Game/UI/HUD', ''), "
                "'WBP_HealthBar': ('/Game/UI/HUD', ''), "
                "'WBP_StaminaBar': ('/Game/UI/HUD', ''), "
                "'WBP_Crosshair': ('/Game/UI/HUD', ''), "
                "'WBP_IG_ResourcePanel': ('/Game/UI/InsectGlaive', '')}.items()}, "
                "'health_value': str(unreal.MHGZAttributeBarKind.Health) "
                "if hasattr(unreal.MHGZAttributeBarKind, 'Health') else 'missing', "
                "'health_enum': str(unreal.MHGZAttributeBarKind.HEALTH) "
                "if hasattr(unreal.MHGZAttributeBarKind, 'HEALTH') else 'missing', "
                "'stamina_value': str(unreal.MHGZAttributeBarKind.Stamina) "
                "if hasattr(unreal.MHGZAttributeBarKind, 'Stamina') else 'missing', "
                "'stamina_enum': str(unreal.MHGZAttributeBarKind.STAMINA) "
                "if hasattr(unreal.MHGZAttributeBarKind, 'STAMINA') else 'missing', "
                "'hud_refs': {field: str(unreal.get_default_object("
                "unreal.EditorAssetLibrary.load_asset("
                "'/Game/Blueprints/HUD/BP_MHGZHUD').generated_class())"
                ".get_editor_property(field)) for field in ('main_hud_class', "
                "'health_bar_class', 'stamina_bar_class', 'crosshair_class')}, "
                "'resource_ref': str(unreal.EditorAssetLibrary.load_asset("
                "'/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG')"
                ".get_editor_property('resource_widget_class')), "
                "'game_mode_hud_ref': str(unreal.get_default_object("
                "unreal.EditorAssetLibrary.load_asset("
                "'/Game/Blueprints/GameModes/Demo/BP_Demo_GameMode').generated_class())"
                ".get_editor_property('hud_class'))})"
            )
            inspection = client.run_command(
                inspect_command, exec_mode=remote_execution.MODE_EVAL_STATEMENT
            )
            print(f"Read-only inspection: {inspection!r}")
            return 0 if inspection.get("success") else 1

        escaped_path = str(script_path).replace("\\", "\\\\").replace("'", "\\'")
        command = (
            "exec(compile(open('"
            + escaped_path
            + "', encoding='utf-8').read(), '"
            + escaped_path
            + "', 'exec'))"
        )
        result = client.run_command(
            command, exec_mode=remote_execution.MODE_EVAL_STATEMENT
        )
        print(f"Remote result: {result!r}")
        return 0 if result.get("success") else 1
    finally:
        client.stop()


if __name__ == "__main__":
    raise SystemExit(main())
