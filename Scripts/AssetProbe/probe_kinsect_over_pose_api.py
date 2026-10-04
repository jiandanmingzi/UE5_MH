import unreal

ASSET_PATH = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhan_Over"
sequence = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if sequence is None:
    raise RuntimeError("Could not load " + ASSET_PATH)

unreal.log("[KinsectOverPoseProbe] sequence=" + sequence.get_path_name())
unreal.log("[KinsectOverPoseProbe] play_length=" + str(sequence.get_play_length()))
unreal.log("[KinsectOverPoseProbe] rate_scale=" + str(sequence.get_editor_property("rate_scale")))

for label, obj in (("sequence", sequence),
                   ("skeleton", sequence.get_editor_property("skeleton"))):
    names = [name for name in dir(obj) if any(key in name.lower() for key in
             ("bone", "track", "pose", "frame", "transform", "animation"))]
    unreal.log("[KinsectOverPoseProbe] " + label + " methods=" + repr(names))

unreal.log("[KinsectOverPoseProbe] unreal animation symbols=" + repr([
    name for name in dir(unreal)
    if any(key in name.lower() for key in ("animpose", "animationblueprint", "animationlibrary"))
]))
unreal.log("[KinsectOverPoseProbe] library/object APIs=" + repr([
    name for name in dir(unreal)
    if name in ("load_object", "load_class", "find_object", "find_class", "load_module")
]))

for name in ("get_anim_pose_at_time", "get_anim_pose_at_frame"):
    method = getattr(sequence, name, None)
    unreal.log("[KinsectOverPoseProbe] " + name + " doc=" + repr(getattr(method, "__doc__", None)))

try:
    options = unreal.AnimPoseEvaluationOptions()
    unreal.log("[KinsectOverPoseProbe] options fields=" + repr([
        (key, repr(options.get_editor_property(key)))
        for key in ("evaluation_type", "should_retarget", "extract_root_motion",
                    "incorporate_root_motion_into_pose", "retrieve_additive_as_full_pose")
    ]))
    pose = sequence.get_anim_pose_at_time(0.0, options)
    unreal.log("[KinsectOverPoseProbe] pose result=" + repr(pose) + " type=" + repr(type(pose)))
    unreal.log("[KinsectOverPoseProbe] pose methods=" + repr([
        name for name in dir(pose)
        if any(key in name.lower() for key in ("bone", "pose", "valid", "transform"))
    ]))
    unreal.log("[KinsectOverPoseProbe] get_bone_names doc=" + repr(pose.get_bone_names.__doc__))
    unreal.log("[KinsectOverPoseProbe] get_bone_pose doc=" + repr(pose.get_bone_pose.__doc__))
    bones = pose.get_bone_names()
    unreal.log("[KinsectOverPoseProbe] pose bone count=" + str(len(bones)) + " names=" + repr(bones))
    for bone in bones[:8]:
        transform = pose.get_bone_pose(bone, unreal.AnimPoseSpaces.LOCAL)
        unreal.log("[KinsectOverPoseProbe] bone sample " + str(bone) + "=" + repr(transform))
    unreal.log("[KinsectOverPoseProbe] extension methods=" + repr([
        name for name in dir(unreal.AnimPoseExtensions)
        if any(key in name.lower() for key in ("bone", "pose", "valid", "transform"))
    ]))
except Exception as exc:
    unreal.log_error("[KinsectOverPoseProbe] pose sampling failed=" + repr(exc))
