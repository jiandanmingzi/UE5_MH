"""Read-only Unreal Python probe for project and MHW review skeletons."""

import json
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT = PROJECT_DIR / "Saved" / "AssetProbe" / "character_skeleton_probe.json"
ASSETS = (
    "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/"
    "MHW_AerialDive_Candidates/SK_MHW_AerialDive_ReviewRig",
)


def safe_property(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as exc:
        return "<{}: {}>".format(type(exc).__name__, exc)


def describe_value(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    result = {"type": type(value).__name__, "text": str(value)}
    for name in ("name", "parent_index", "translation_retargeting_mode"):
        try:
            item = getattr(value, name)
            result[name] = str(item) if name.endswith("mode") else str(item)
        except Exception:
            pass
    return result


def describe_bone_tree(skeleton):
    tree = safe_property(skeleton, "bone_tree")
    try:
        count = len(tree)
    except Exception as exc:
        return {"error": "{}: {}".format(type(exc).__name__, exc)}
    rows = []
    for index, node in enumerate(tree):
        fields = {}
        for field in ("name", "bone_name", "parent_index", "parent_name",
                      "translation_retargeting_mode", "bone_tree_index"):
            try:
                value = node.get_editor_property(field)
                fields[field] = str(value)
            except Exception:
                try:
                    fields[field] = str(getattr(node, field))
                except Exception:
                    pass
        rows.append({"index": index, **fields, "node_type": type(node).__name__,
                     "tuple": str(node.to_tuple()) if index < 5 else "",
                     "export_text": node.export_text() if index < 5 else "",
                     "dir": [name for name in dir(node) if not name.startswith("_")] if index == 0 else []})
    return {"count": count, "bones": rows}


rows = []
for path in ASSETS:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        rows.append({"asset": path, "loaded": False})
        continue
    row = {
        "asset": path,
        "loaded": True,
        "class": asset.get_class().get_path_name(),
    }
    row["skinned_component_methods"] = [
        name for name in dir(unreal.SkeletalMeshComponent)
        if "bone" in name.lower() or "skeletal" in name.lower() or "skinned" in name.lower()
    ]
    try:
        component = unreal.new_object(unreal.SkeletalMeshComponent)
        component.set_editor_property("skeletal_mesh", asset)
        count = int(component.get_num_bones())
        row["component_bone_count"] = count
        row["component_bones"] = []
        for index in range(count):
            name = str(component.get_bone_name(index))
            try:
                parent = str(component.get_parent_bone(name))
            except Exception:
                parent = "<parent unavailable>"
            row["component_bones"].append({"index": index, "name": name, "parent": parent})
    except Exception as exc:
        row["component_bone_error"] = "{}: {}".format(type(exc).__name__, exc)
    skeleton = safe_property(asset, "skeleton")
    row["skeleton"] = str(skeleton)
    row["asset_reference_skeleton"] = describe_value(
        safe_property(asset, "reference_skeleton"))
    row["asset_reflected_properties"] = [name for name in dir(asset) if "bone" in name.lower() or "skeleton" in name.lower()]
    if not isinstance(skeleton, str) and skeleton is not None:
        row["skeleton_class"] = skeleton.get_class().get_path_name()
        row["skeleton_reflected_properties"] = [name for name in dir(skeleton) if "bone" in name.lower() or "skeleton" in name.lower()]
        row["skeleton_bone_tree"] = describe_bone_tree(skeleton)
        row["skeleton_reference_skeleton"] = describe_value(
            safe_property(skeleton, "reference_skeleton"))
    rows.append(row)

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
print("CHARACTER_SKELETON_PROBE=" + json.dumps(rows, ensure_ascii=False))
