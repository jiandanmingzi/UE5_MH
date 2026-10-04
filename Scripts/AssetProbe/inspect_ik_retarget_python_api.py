"""Print the exposed UE 5.6 Python API used to build a review retarget setup."""

import unreal


def members(value):
    return [name for name in dir(value)
            if any(token in name.lower() for token in
                   ("retarget", "chain", "pelvis", "skeleton", "mesh", "controller", "asset", "export"))]


rows = {}
for name in ("IKRigDefinition", "IKRigDefinitionFactory", "IKRigController",
             "IKRetargeter", "IKRetargetFactory", "IKRetargeterFactory",
             "IKRetargeterController", "IKRetargetBatchOperation", "AssetToolsHelpers",
             "NameDuplicationRule"):
    item = getattr(unreal, name, None)
    rows[name] = None if item is None else members(item)
    if name == "IKRigDefinitionFactory" and item:
        rows[name + ".static"] = [x for x in dir(item) if not x.startswith("_")]
    if name == "IKRetargetBatchOperation" and item:
        rows[name + ".static"] = [x for x in dir(item) if not x.startswith("_")]

for owner, method in (("IKRigDefinitionFactory", "create_new_ik_rig_asset"),
                      ("IKRetargetBatchOperation", "duplicate_and_retarget"),
                      ("IKRigController", "set_retarget_root"),
                      ("IKRetargeterController", "set_ik_rig"),
                      ("IKRetargeterController", "add_default_ops"),
                      ("IKRetargeterController", "auto_map_chains"),
                      ("IKRetargeterController", "set_source_chain")):
    value = getattr(getattr(unreal, owner, None), method, None)
    rows[owner + "." + method + ".doc"] = getattr(value, "__doc__", None) if value else None
rows["source_target_enum"] = [x for x in dir(unreal.RetargetSourceOrTarget) if x.isupper()] if hasattr(unreal, "RetargetSourceOrTarget") else None
rows["auto_map_enum"] = [x for x in dir(unreal.AutoMapChainType) if x.isupper()] if hasattr(unreal, "AutoMapChainType") else None
for name in ("AnimationBlueprintLibrary", "AnimationDataController", "AnimSequence", "IKRetargetFactory"):
    item = getattr(unreal, name, None)
    rows[name] = [x for x in dir(item) if any(t in x.lower() for t in
                 ("bone", "track", "controller", "factory", "create", "asset"))] if item else None
    if item and name == "IKRetargetFactory":
        try:
            factory = item()
            rows[name + ".instance"] = {"type": str(factory.get_class()),
                                           "props": [x for x in dir(factory) if not x.startswith("_")]}
        except Exception as exc:
            rows[name + ".instance_error"] = repr(exc)

print("IK_RETARGET_PYTHON_API=" + str(rows))
