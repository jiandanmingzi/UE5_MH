"""Environment probe, executed by Blender in background mode without saving."""

import json
import sys

import addon_utils
import bpy


def main():
    action = bpy.data.actions.new("MHGZ_API_Probe")
    try:
        fbx_available = bool(bpy.ops.export_scene.fbx.get_rna_type())
    except Exception:
        fbx_available = False
    print("MHGZ_BLENDER_PROBE=" + json.dumps({
        "blender_version": bpy.app.version_string,
        "python_version": sys.version,
        "binary": bpy.app.binary_path,
        "user_scripts": bpy.utils.user_resource("SCRIPTS"),
        "user_config": bpy.utils.user_resource("CONFIG"),
        "legacy_action_fcurves": hasattr(action, "fcurves"),
        "layered_actions": hasattr(action, "slots") and hasattr(action, "layers"),
        "fbx_operator_available": fbx_available,
        "enabled_addons": sorted(bpy.context.preferences.addons.keys()),
        "available_mhw_addons": [m.__name__ for m in addon_utils.modules()
                                 if "mhw" in m.__name__.lower() or "mod3" in m.__name__.lower()],
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
