"""Create a reusable UE 5.6 IK Retarget setup for MHW dive 380/381.

Only creates new assets under the MHW review folder. Never edits gameplay
assets or the source review animations.
"""

import json
import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
SETUP_DIR = ROOT + "/Retargeting"
SOURCE_MESH_PATH = ROOT + "/SK_MHW_IG_AerialDive_ReviewRig"
TARGET_MESH_PATH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
SOURCE_IKRIG_NAME = "IKR_MHW_IG_AerialDive_Source"
TARGET_IKRIG_NAME = "IKR_MHW_IG_AerialDive_DemoIG"
RETARGETER_NAME = "IKRT_MHW_IG_AerialDive_ToDemoIG"

CHAINS = (
    ("Root", "bone_root", "bone_root", "Root", "Root"),
    ("Spine", "bone_001", "bone_004", "Spine_00", "Head_00"),
    ("Arm_L", "bone_005", "bone_008", "L_Arm_00", "L_Arm_03"),
    ("Arm_R", "bone_009", "bone_012", "R_Arm_00", "R_Arm_03"),
    ("Leg_L", "bone_014", "bone_017", "L_Leg_00", "L_Leg_03"),
    ("Leg_R", "bone_018", "bone_021", "R_Leg_00", "R_Leg_03"),
    ("Weapon_L", "bone_030", "bone_030", "L_Weapon_00", "L_Weapon_00"),
    ("Weapon_R", "bone_047", "bone_047", "R_Weapon_00", "R_Weapon_00"),
    # The two skeletons have five digit chains on each hand, but their
    # numeric/source and named/target bones are not one-to-one by index.
    # On the source right hand, bone_057 is a shared branch parent which has
    # no matching target finger bone; start the two target chains at its
    # children instead. The left hand has a matching shared proximal bone.
    ("Finger_L_00", "bone_031", "bone_033", "L_Finger_00", "L_Finger_02_end"),
    ("Finger_L_01", "bone_034", "bone_036", "L_Finger_03", "L_Finger_05_end"),
    ("Finger_L_02", "bone_037", "bone_039", "L_Finger_06", "L_Finger_08_end"),
    ("Finger_L_03", "bone_040", "bone_043", "L_Finger_09", "L_Finger_12_end"),
    ("Finger_L_04", "bone_044", "bone_046", "L_Finger_13", "L_Finger_15_end"),
    ("Finger_R_00", "bone_048", "bone_050", "R_Finger_00", "R_Finger_02_end"),
    ("Finger_R_01", "bone_051", "bone_053", "R_Finger_03", "R_Finger_05_end"),
    ("Finger_R_02", "bone_054", "bone_056", "R_Finger_06", "R_Finger_08_end"),
    ("Finger_R_03", "bone_058", "bone_060", "R_Finger_09", "R_Finger_11_end"),
    ("Finger_R_04", "bone_061", "bone_063", "R_Finger_12", "R_Finger_14_end"),
)


def asset_path(name):
    return SETUP_DIR + "/" + name


def load_required(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise FileNotFoundError(path)
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Unable to save " + asset.get_path_name())


def get_or_create_ikrig(name, mesh, root_bone, side):
    path = asset_path(name)
    ikrig = unreal.EditorAssetLibrary.load_asset(path)
    if ikrig is None:
        ikrig = unreal.IKRigDefinitionFactory.create_new_ik_rig_asset(SETUP_DIR, name)
        if ikrig is None:
            raise RuntimeError("Failed to create IK Rig " + path)
    controller = unreal.IKRigController.get_controller(ikrig)
    if controller.get_skeletal_mesh() != mesh:
        if not controller.set_skeletal_mesh(mesh):
            raise RuntimeError("Could not assign mesh to " + path)
    if not controller.set_retarget_root(unreal.Name(root_bone)):
        raise RuntimeError("Could not set retarget pelvis/root " + root_bone + " on " + path)

    existing = {str(chain.chain_name): chain for chain in controller.get_retarget_chains()}
    for chain_name, source_start, source_end, target_start, target_end in CHAINS:
        start, end = ((source_start, source_end) if side == "source"
                      else (target_start, target_end))
        if chain_name not in existing:
            controller.add_retarget_chain(unreal.Name(chain_name), unreal.Name(start),
                                          unreal.Name(end), unreal.Name("None"))
        if str(controller.get_retarget_chain_start_bone(unreal.Name(chain_name))) != start:
            raise RuntimeError("Unexpected chain start: {} {}".format(chain_name, start))
        if str(controller.get_retarget_chain_end_bone(unreal.Name(chain_name))) != end:
            raise RuntimeError("Unexpected chain end: {} {}".format(chain_name, end))
    save(ikrig)
    return ikrig


def main():
    unreal.EditorAssetLibrary.make_directory(SETUP_DIR)
    source_mesh = load_required(SOURCE_MESH_PATH)
    target_mesh = load_required(TARGET_MESH_PATH)
    target_skeleton = target_mesh.get_editor_property("skeleton")
    if target_skeleton is None:
        raise RuntimeError("Target demo mesh has no Skeleton")

    source_ikrig = get_or_create_ikrig(
        SOURCE_IKRIG_NAME, source_mesh, "bone_013", "source")
    target_ikrig = get_or_create_ikrig(
        TARGET_IKRIG_NAME, target_mesh, "Waist_00", "target")

    retargeter_path = asset_path(RETARGETER_NAME)
    retargeter = unreal.EditorAssetLibrary.load_asset(retargeter_path)
    retargeter_created = False
    if retargeter is None:
        retargeter = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            RETARGETER_NAME, SETUP_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
        if retargeter is None:
            raise RuntimeError("Failed to create IK Retargeter " + retargeter_path)
        retargeter_created = True
    retarget_controller = unreal.IKRetargeterController.get_controller(retargeter)
    retarget_controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_ikrig)
    retarget_controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_ikrig)
    retarget_controller.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
    retarget_controller.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
    # The existing review Retargeter already has its default ops. Calling this
    # unconditionally adds another set on reruns, so only initialize a newly
    # created Retargeter here.
    if retargeter_created:
        retarget_controller.add_default_ops()
    retarget_controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)

    mapping = {}
    for chain_name, *_ in CHAINS:
        mapped = str(retarget_controller.get_source_chain(unreal.Name(chain_name)))
        mapping[chain_name] = mapped
        if mapped != chain_name:
            raise RuntimeError("Chain mapping failed for {}: {}".format(chain_name, mapped))
    save(retargeter)

    report = {
        "status": "IK_RETARGET_SETUP_CREATED",
        "source_mesh": source_mesh.get_path_name(),
        "source_skeleton": source_mesh.get_editor_property("skeleton").get_path_name(),
        "target_mesh": target_mesh.get_path_name(),
        "target_skeleton": target_skeleton.get_path_name(),
        "source_ikrig": source_ikrig.get_path_name(),
        "target_ikrig": target_ikrig.get_path_name(),
        "retargeter": retargeter.get_path_name(),
        "source_pelvis": "bone_013",
        "target_pelvis": "Waist_00",
        "chain_mapping": mapping,
        "finger_chain_count_per_hand": 5,
        "finger_chain_names": [row[0] for row in CHAINS if row[0].startswith("Finger_")],
    }
    unreal.log("[MHW AerialDive Retarget Setup] " + json.dumps(report, ensure_ascii=False))
    print("MHW_AERIAL_DIVE_RETARGET_SETUP=" + json.dumps(report, ensure_ascii=False))


main()
