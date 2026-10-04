"""Set the shared B / RT+B dash range in DA_IG_Combat and verify the saved asset."""

import unreal


ASSET_PATH = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
TARGET_DISTANCE_CM = 840.0

asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if asset is None:
    raise RuntimeError("Could not load " + ASSET_PATH)

previous = float(asset.get_editor_property("kinsect_slash_max_distance"))
if not 0.0 < previous < 10000.0:
    raise RuntimeError("Unexpected previous Kinsect Slash range: {}".format(previous))

asset.set_editor_property("kinsect_slash_max_distance", TARGET_DISTANCE_CM)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError("Could not save " + ASSET_PATH)

reloaded = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
actual = float(reloaded.get_editor_property("kinsect_slash_max_distance"))
if abs(actual - TARGET_DISTANCE_CM) > 0.001:
    raise RuntimeError("Saved distance mismatch: {}".format(actual))

unreal.log("[KinsectSlashRange] {} -> {} cm (saved)".format(previous, actual))
