"""Ask the open MHGZ editor to compile and apply its Live Coding patch."""

import unreal


world = unreal.EditorLevelLibrary.get_editor_world()
if world is None:
    raise RuntimeError("No editor world is available for the Live Coding request")

unreal.log("[E6.3LiveCoding] Requesting LiveCoding.CompileSync")
unreal.SystemLibrary.execute_console_command(world, "LiveCoding.CompileSync")
unreal.log("[E6.3LiveCoding] LiveCoding.CompileSync command returned")
