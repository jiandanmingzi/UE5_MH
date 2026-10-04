"""Read-only UE Python API probe for isolated MHW review asset import."""

import json
import unreal


def names(obj, fragments):
    return sorted(name for name in dir(obj)
                  if any(fragment in name.lower() for fragment in fragments))


report = {
    "fbx_import_ui": names(unreal.FbxImportUI(), ("skeleton", "physics", "import_mesh")),
    "skeleton_factory_class": hasattr(unreal, "SkeletonFactory"),
    "skeletal_mesh_editor_subsystem": hasattr(unreal, "SkeletalMeshEditorSubsystem"),
    "asset_compiling_manager": hasattr(unreal, "AssetCompilingManager"),
    "animation_sequence_api": names(unreal.AnimSequence(), ("track", "bone", "preview")),
    "skeletal_mesh_api": names(unreal.SkeletalMesh(), ("skeleton", "physics")),
}
if report["skeleton_factory_class"]:
    factory = unreal.SkeletonFactory()
    report["skeleton_factory"] = names(factory, ("target", "skeletal", "mesh"))
if report["skeletal_mesh_editor_subsystem"]:
    report["skeletal_mesh_editor_subsystem_api"] = names(
        unreal.SkeletalMeshEditorSubsystem(), ("physics", "skeleton", "preview")
    )
unreal.log("MHW_UE_IMPORT_API=" + json.dumps(report, ensure_ascii=False))
print("MHW_UE_IMPORT_API=" + json.dumps(report, ensure_ascii=False))
