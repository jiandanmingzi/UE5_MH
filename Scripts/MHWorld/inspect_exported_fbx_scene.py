"""Inspect an FBX in a fresh Blender process without saving or modifying it."""

import argparse
import json
import sys

import bpy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fbx", required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Run in a fresh Blender --background --factory-startup process.")
    bpy.ops.import_scene.fbx(filepath=args.fbx, use_anim=False)
    report = {
        "fbx": args.fbx,
        "objects": [],
        "armatures": [],
    }
    for obj in bpy.context.scene.objects:
        item = {"name": obj.name, "type": obj.type}
        if obj.type == "MESH":
            item["vertices"] = len(obj.data.vertices)
            item["vertex_groups"] = len(obj.vertex_groups)
            weighted = {}
            for vertex in obj.data.vertices:
                for membership in vertex.groups:
                    if membership.weight > 0.0:
                        weighted[membership.group] = weighted.get(membership.group, 0) + 1
            item["weighted_groups"] = {
                group.name: weighted.get(group.index, 0)
                for group in obj.vertex_groups if weighted.get(group.index, 0) > 0
            }
            item["armature_modifiers"] = [
                modifier.object.name if modifier.object else None
                for modifier in obj.modifiers if modifier.type == "ARMATURE"
            ]
        if obj.type == "ARMATURE":
            bones = obj.data.bones
            report["armatures"].append({
                "name": obj.name,
                "bone_count": len(bones),
                "deform_bone_count": sum(1 for bone in bones if bone.use_deform),
                "roots": [bone.name for bone in bones if bone.parent is None],
                "bone_names": [bone.name for bone in bones],
            })
        report["objects"].append(item)
    print("MHW_FBX_SCENE=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
