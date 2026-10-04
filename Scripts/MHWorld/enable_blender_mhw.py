"""Enable the installed MOD3/LMT add-on and persist Blender user preferences."""

import addon_utils
import bpy


def main():
    for name in ("mhworld_tools_suite", "io_scene_fbx"):
        module = addon_utils.enable(name, default_set=True, persistent=True)
        if module is None or not addon_utils.check(name)[1]:
            raise RuntimeError(f"Could not enable required add-on: {name}")
    assert bpy.ops.mhworld_import.mhworld_mod3.get_rna_type()
    assert bpy.ops.mhworld_import.mhworld_lmt.get_rna_type()
    assert bpy.ops.export_scene.fbx.get_rna_type()
    result = bpy.ops.wm.save_userpref()
    if result != {"FINISHED"}:
        raise RuntimeError(f"Could not save add-on preferences: {result}")
    print("MHGZ_MHW_ADDONS_ENABLED=mhworld_tools_suite,io_scene_fbx")


if __name__ == "__main__":
    main()
