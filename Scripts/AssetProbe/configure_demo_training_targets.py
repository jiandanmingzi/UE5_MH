"""Two training targets, asset-only. Do not replay an applied transaction.

Blender: blender -b --factory-startup --python <this file> -- --build-meshes
UE (editor closed): Python Commandlet, default freezes/backups or verifies.
One-time write: -MHGZTrainingTargetsApply. Use ue_remote.py in an open editor.
The cylinder's current visual/body/query dimensions double. A second ordinary
TrainingDummy instance gets three persistent hemisphere-band meshes, a separate
three-extract config, exact R=400cm geometry and mesh body collision. No C++.
"""
import hashlib
import json
import math
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Saved/Maintenance/TrainingTargets20261024"
MAP = "/Game/Maps/L_DemoArena"
BP = "/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy"
CONFIG = "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy"
LARGE = "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy_Large"
HEMI = "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy_Hemisphere"
MESH_DIR = "/Game/Monster/TrainingDummy/Meshes"
COLORS = ("White", "Orange", "Red")
RADIUS = 400.0


def mesh_name(color):
    return "SM_TD_Hemisphere_" + color


def build_meshes():
    import bpy
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 0.01
    dest = OUT / "Source"
    dest.mkdir(parents=True, exist_ok=True)
    report = []
    sides, rings = 128, 32
    for band, color in enumerate(COLORS):
        z0, z1 = RADIUS * band / 3, RADIUS * (band + 1) / 3
        verts, faces = [], []
        # Equal polar-angle steps retain smooth curvature near the crown.
        lo, hi = math.asin(z0 / RADIUS), math.asin(z1 / RADIUS)
        last_ring = rings - 1 if band == 2 else rings
        for row in range(last_ring + 1):
            angle = lo + (hi - lo) * row / rings
            rad, z = RADIUS * math.cos(angle), RADIUS * math.sin(angle)
            verts.extend((rad * math.cos(2 * math.pi * j / sides),
                          rad * math.sin(2 * math.pi * j / sides), z - z0) for j in range(sides))
        for row in range(last_ring):
            for j in range(sides):
                k = (j + 1) % sides
                faces.append((row*sides+j, row*sides+k, (row+1)*sides+k, (row+1)*sides+j))
        if band == 2:
            tip = len(verts)
            verts.append((0, 0, RADIUS-z0))
            for j in range(sides):
                faces.append((last_ring*sides+j, last_ring*sides+(j+1)%sides, tip))
        if band == 0:
            # Flat, closed bottom; the two internal seams have no collision caps.
            faces.append(tuple(reversed(range(sides))))
        mesh = bpy.data.meshes.new(mesh_name(color))
        mesh.from_pydata(verts, [], faces)
        mesh.update()
        obj = bpy.data.objects.new(mesh_name(color), mesh)
        bpy.context.collection.objects.link(obj)
        for polygon in mesh.polygons:
            polygon.use_smooth = len(polygon.vertices) <= 4
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = dest / (mesh_name(color) + ".fbx")
        bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", mesh_smooth_type="FACE",
            axis_forward="-Y", axis_up="Z", bake_anim=False, add_leaf_bones=False)
        assert all(abs(x*x+y*y+(z+z0)**2-RADIUS**2) < .01 for x,y,z in verts)
        report.append(dict(color=color, radius_cm=RADIUS, base_z=z0, top_z=z1,
                           vertices=len(verts), faces=len(faces), file=str(path)))
    (OUT / "mesh-source.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("TRAINING_HEMISPHERE_FBX_OK", len(report))


def editor_main():
    import unreal as ue
    sys.path.insert(0, str(ROOT / "Scripts"))
    from ue_asset_utils import as_sequence
    OUT.mkdir(parents=True, exist_ok=True)
    command_line = ue.SystemLibrary.get_command_line()
    resuming = "-MHGZTrainingTargetsResumeLayout" in command_line
    writing = "-MHGZTrainingTargetsApply" in command_line or resuming
    actors_api = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    # This editor subsystem is not instantiated by Python Commandlets. These
    # mesh-only utility methods do not depend on subsystem instance state.
    mesh_api = ue.get_editor_subsystem(ue.StaticMeshEditorSubsystem) or ue.get_default_object(ue.StaticMeshEditorSubsystem)

    def disk(p):
        return ROOT / ("Content/" + p.removeprefix("/Game/") + (".umap" if p == MAP else ".uasset"))

    def digest(p):
        return hashlib.sha256(disk(p).read_bytes()).hexdigest()

    def vec(v):
        return [float(v.x), float(v.y), float(v.z)]

    def component(actor, name):
        return next(c for c in actor.get_components_by_class(ue.ActorComponent) if c.get_name() == name)

    def bounds(c):
        origin, extent, radius = ue.SystemLibrary.get_component_bounds(c)
        return dict(origin=vec(origin), extent=vec(extent))

    def snapshot(actor):
        capsule = actor.get_component_by_class(ue.CapsuleComponent)
        return dict(name=actor.get_name(), label=actor.get_actor_label(), location=vec(actor.get_actor_location()),
            scale=vec(actor.get_actor_scale3d()), infinite_health=actor.get_editor_property("infinite_health"),
            config=actor.get_editor_property("dummy_config").get_path_name(),
            capsule=dict(radius=capsule.get_unscaled_capsule_radius(), half_height=capsule.get_unscaled_capsule_half_height(),
                         collision=str(capsule.get_collision_enabled())),
            segments={color: dict(mesh=component(actor, color+"SegmentVisual").get_editor_property("static_mesh").get_path_name(),
                location=vec(component(actor,color+"SegmentVisual").get_editor_property("relative_location")),
                scale=vec(component(actor,color+"SegmentVisual").get_editor_property("relative_scale3d")),
                bounds=bounds(component(actor,color+"SegmentVisual"))) for color in COLORS})

    world = ue.EditorLoadingAndSavingUtils.load_map(MAP)
    assert world
    actors = actors_api.get_all_level_actors()
    original = next(a for a in actors if a.get_actor_label() == "Arena_TrainingDummy")
    source_path, applied_path = OUT / "source.json", OUT / "applied.json"
    if not source_path.exists():
        assert not writing, "Freeze original map and values first"
        packages = [MAP, BP, CONFIG, "/Game/Environment/DemoArena/Meshes/SM_Cylinder"]
        packages += ["/Game/Monster/TrainingDummy/Materials/MI_TD_"+c for c in COLORS]
        hashes = {p: digest(p) for p in packages}
        backup = OUT / "Backup/L_DemoArena.umap"
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk(MAP), backup)
        assert hashlib.sha256(backup.read_bytes()).hexdigest() == hashes[MAP]
        source = dict(original=snapshot(original), hashes=hashes,
                      floor_top=max(a.get_actor_bounds(False, False)[0].z+a.get_actor_bounds(False, False)[1].z
                                    for a in actors if a.get_actor_label().startswith("Arena_Floor_")))
        source_path.write_text(json.dumps(source, ensure_ascii=False, indent=2), encoding="utf-8")
        # Record supported engine signatures before any import or mutation.
        docs = {name: getattr(ue.SystemLibrary,name).__doc__ for name in
                ("get_component_bounds", "line_trace_single_for_objects", "sphere_trace_single_for_objects")}
        (OUT / "api.txt").write_text(str(docs), encoding="utf-8")
        ue.log("TRAINING_TARGET_SOURCE_FROZEN " + json.dumps(source["original"], ensure_ascii=False))
        return

    source = json.loads(source_path.read_text(encoding="utf-8"))
    for p, sha in source["hashes"].items():
        if p != MAP:
            assert digest(p) == sha, ("Protected asset changed", p)
    if resuming:
        interrupted = json.loads(applied_path.read_text(encoding="utf-8"))
        assert interrupted["state"] == "creating", "Only resume the verified pre-map-save interruption"
        assert digest(MAP) == source["hashes"][MAP], "Unsaved layout only; map must still be original"
        assert all(ue.EditorAssetLibrary.does_asset_exist(p) for p in interrupted["packages"])
    if writing and not resuming:
        assert not applied_path.exists(), "Do not replay a saved/partial transaction"
        assert digest(MAP) == source["hashes"][MAP], "Map changed after source freeze"
        new_packages = [LARGE, HEMI] + [MESH_DIR+"/"+mesh_name(c) for c in COLORS]
        assert all(not ue.EditorAssetLibrary.does_asset_exist(p) for p in new_packages), "Do not overwrite existing assets"
        assert all((OUT/"Source"/(mesh_name(c)+".fbx")).exists() for c in COLORS)
        applied_path.write_text(json.dumps(dict(state="creating", packages=new_packages)), encoding="utf-8")

        for color in COLORS:
            task = ue.AssetImportTask()
            task.filename = str(OUT/"Source"/(mesh_name(color)+".fbx"))
            task.destination_path, task.destination_name = MESH_DIR, mesh_name(color)
            task.automated, task.replace_existing, task.save = True, False, True
            task.factory = ue.FbxFactory()
            options = ue.FbxImportUI()
            options.import_mesh, options.import_as_skeletal = True, False
            options.import_materials, options.import_textures = False, False
            options.automated_import_should_detect_type = False
            options.mesh_type_to_import = ue.FBXImportType.FBXIT_STATIC_MESH
            options.static_mesh_import_data.combine_meshes = False
            options.static_mesh_import_data.auto_generate_collision = False
            task.options = options
            ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            mesh = ue.load_asset(MESH_DIR+"/"+mesh_name(color))
            assert isinstance(mesh, ue.StaticMesh), task.imported_object_paths
            body = mesh.get_editor_property("body_setup")
            body.set_editor_property("collision_trace_flag", ue.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            mesh_api.enable_section_collision(mesh, True, 0, 0)
            assert ue.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

        large = ue.EditorAssetLibrary.duplicate_asset(CONFIG, LARGE)
        hemi = ue.EditorAssetLibrary.duplicate_asset(CONFIG, HEMI)
        assert large and hemi
        zones = as_sequence(large.get_editor_property("hitzones"))
        for z in zones:
            z.set_editor_property("relative_location", z.get_editor_property("relative_location")*2.0)
            z.set_editor_property("radius", z.get_editor_property("radius")*2.0)
            z.set_editor_property("box_half_extent", z.get_editor_property("box_half_extent")*2.0)
        large.set_editor_property("hitzones", zones)
        fire = large.get_editor_property("fire_ring")
        fire.set_editor_property("relative_location", fire.get_editor_property("relative_location")*2.0)
        large.set_editor_property("fire_ring", fire)
        zones = as_sequence(hemi.get_editor_property("hitzones"))
        ordered = sorted(zones, key=lambda z: z.get_editor_property("relative_location").z)
        for i, z in enumerate(ordered):
            z0 = RADIUS*i/3.0
            extent = math.sqrt(RADIUS*RADIUS-z0*z0)+2.0
            # Native ApplyConfig multiplies offsets/extents by 1.5 exactly once.
            z.set_editor_property("relative_location", ue.Vector(0, 0, (-RADIUS/2+(i+.5)*RADIUS/3)/1.5))
            z.set_editor_property("box_half_extent", ue.Vector(extent/1.5, extent/1.5, (RADIUS/6+2)/1.5))
            z.set_editor_property("shape", ue.MHGZHitzoneShape.BOX)
            z.set_editor_property("radius", extent/1.5)
        hemi.set_editor_property("hitzones", zones)
        fire = hemi.get_editor_property("fire_ring")
        fire.set_editor_property("enabled", False)
        hemi.set_editor_property("fire_ring", fire)
        for data in (large,hemi):
            assert ue.EditorAssetLibrary.save_loaded_asset(data, only_if_is_dirty=False)

    if writing:
        new_packages = [LARGE, HEMI] + [MESH_DIR+"/"+mesh_name(c) for c in COLORS]
        large, hemi = ue.load_asset(LARGE), ue.load_asset(HEMI)
        capsule = original.get_component_by_class(ue.CapsuleComponent)
        capsule.set_capsule_size(source["original"]["capsule"]["radius"]*2,
                                 source["original"]["capsule"]["half_height"]*2, False)
        original.set_editor_property("dummy_config", large)
        for color in COLORS:
            c = component(original, color+"SegmentVisual")
            old = source["original"]["segments"][color]
            c.set_editor_property("relative_location", ue.Vector(*old["location"])*2.0)
            c.set_editor_property("relative_scale3d", ue.Vector(*old["scale"])*2.0)
            light = component(original, color+"ExtractLight")
            light.set_editor_property("relative_location", light.get_editor_property("relative_location")*2.0)
        oldpos = source["original"]["location"]
        original.set_actor_location(ue.Vector(oldpos[0],oldpos[1],source["floor_top"]+source["original"]["capsule"]["half_height"]*2),False,True)

        cls = ue.load_class(None, BP+".BP_TrainingDummy_C")
        new = actors_api.spawn_actor_from_class(cls, ue.Vector(oldpos[0],oldpos[1]+1400,source["floor_top"]+RADIUS/2), original.get_actor_rotation())
        assert new
        new.set_actor_label("Arena_TrainingDummy_Hemisphere_R400")
        new.set_editor_property("dummy_config", hemi)
        new.set_editor_property("auto_possess_ai", ue.AutoPossessAI.DISABLED)
        movement = new.get_component_by_class(ue.CharacterMovementComponent)
        movement.set_editor_property("auto_activate", False)
        movement.set_editor_property("default_land_movement_mode", ue.MovementMode.MOVE_NONE)
        movement.set_editor_property("default_water_movement_mode", ue.MovementMode.MOVE_NONE)
        movement.set_movement_mode(ue.MovementMode.MOVE_NONE)
        capsule = new.get_component_by_class(ue.CapsuleComponent)
        slope = capsule.get_walkable_slope_override()
        capsule.set_collision_enabled(ue.CollisionEnabled.NO_COLLISION)
        for i,color in enumerate(COLORS):
            c = component(new, color+"SegmentVisual")
            c.set_static_mesh(ue.load_asset(MESH_DIR+"/"+mesh_name(color)))
            c.set_editor_property("relative_location", ue.Vector(0,0,-RADIUS/2+i*RADIUS/3))
            c.set_editor_property("relative_scale3d", ue.Vector(1,1,1))
            c.set_collision_profile_name("MonsterBody")
            c.set_walkable_slope_override(slope)
            light = component(new, color+"ExtractLight")
            height = (i+.5)*RADIUS/3
            light.set_editor_property("relative_location", ue.Vector(math.sqrt(RADIUS**2-height**2)+12,0,-RADIUS/2+height))
        assert ue.EditorLoadingAndSavingUtils.save_map(world, MAP)
        applied_path.write_text(json.dumps(dict(state="saved", map_sha256=digest(MAP), packages=new_packages,
                                               hashes={p:digest(p) for p in new_packages}),indent=2),encoding="utf-8")

    # Cold-load verification is deliberately scoped to the two targets.
    applied = json.loads(applied_path.read_text(encoding="utf-8"))
    assert applied["state"] == "saved" and digest(MAP) == applied["map_sha256"]
    if "-MHGZTrainingTargetsRepairSectionCollision" in command_line:
        assert not applied.get("section_collision_repaired"), "Do not replay section repair"
        for color in COLORS:
            path=MESH_DIR+"/"+mesh_name(color)
            assert digest(path)==applied["hashes"][path]
            backup=OUT/"Backup"/(mesh_name(color)+".uasset")
            if backup.exists():
                assert hashlib.sha256(backup.read_bytes()).hexdigest()==digest(path)
            else:
                shutil.copy2(disk(path),backup)
            mesh=ue.load_asset(path)
            mesh_api.enable_section_collision(mesh,True,0,0)
            assert ue.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False)
            applied["hashes"][path]=digest(path)
        applied["section_collision_repaired"]=True
        applied_path.write_text(json.dumps(applied,indent=2),encoding="utf-8")
    actors = actors_api.get_all_level_actors()
    new = next(a for a in actors if a.get_actor_label() == "Arena_TrainingDummy_Hemisphere_R400")
    states = [snapshot(original),snapshot(new)]
    assert states[0]["config"].split('.')[0] == LARGE and states[1]["config"].split('.')[0] == HEMI
    assert all(a["infinite_health"] for a in states)
    assert abs(states[0]["capsule"]["radius"]-source["original"]["capsule"]["radius"]*2)<.01
    for color in COLORS:
        assert all(abs(a-b*2)<.01 for a,b in zip(states[0]["segments"][color]["scale"],source["original"]["segments"][color]["scale"]))
    lo=[min(v["bounds"]["origin"][i]-v["bounds"]["extent"][i] for v in states[1]["segments"].values()) for i in range(3)]
    hi=[max(v["bounds"]["origin"][i]+v["bounds"]["extent"][i] for v in states[1]["segments"].values()) for i in range(3)]
    assert abs((hi[0]-lo[0])/2-RADIUS)<.1 and abs((hi[1]-lo[1])/2-RADIUS)<.1, (lo,hi)
    assert abs(hi[2]-lo[2]-RADIUS)<.1 and abs(lo[2]-source["floor_top"])<.1, (lo,hi)
    assert new.get_component_by_class(ue.CapsuleComponent).get_collision_enabled()==ue.CollisionEnabled.NO_COLLISION
    for color in COLORS:
        c=component(new,color+"SegmentVisual")
        assert str(c.get_collision_profile_name())=="MonsterBody"
        assert c.get_editor_property("static_mesh").get_editor_property("body_setup").get_editor_property("collision_trace_flag")==ue.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        if "-MHGZTrainingTargetsProbeCollision" not in command_line:
            assert mesh_api.is_section_collision_enabled(c.get_editor_property("static_mesh"),0,0)
    movement = new.get_component_by_class(ue.CharacterMovementComponent)
    assert not movement.get_editor_property("auto_activate")
    assert movement.get_editor_property("default_land_movement_mode") == ue.MovementMode.MOVE_NONE
    assert movement.get_editor_property("default_water_movement_mode") == ue.MovementMode.MOVE_NONE
    assert new.get_editor_property("auto_possess_ai") == ue.AutoPossessAI.DISABLED
    if "-MHGZTrainingTargetsProbeCollision" in command_line:
        samples = [("floor", ue.Vector(0,0,100), ue.Vector(0,0,-100))]
        for actor in (original,new):
            p=actor.get_actor_location()
            samples.append((actor.get_actor_label(), p+ue.Vector(500,0,0), p))
        rows=[]
        for name,start,end in samples:
            for complex_trace in (False,True):
                hit=ue.SystemLibrary.line_trace_single_for_objects(world,start,end,
                    [ue.ObjectTypeQuery.OBJECT_TYPE_QUERY1,ue.ObjectTypeQuery.OBJECT_TYPE_QUERY2,
                     ue.ObjectTypeQuery.OBJECT_TYPE_QUERY3],
                    complex_trace,[],ue.DrawDebugTrace.NONE)
                rows.append(dict(sample=name,complex=complex_trace,
                                 result=str(hit) if hit else None))
        for color in COLORS:
            c=component(new,color+"SegmentVisual")
            rows.append(dict(component=c.get_name(),collision=str(c.get_collision_enabled()),
                             object_type=str(c.get_collision_object_type())))
        (OUT/"collision-probe.json").write_text(json.dumps(rows,indent=2),encoding="utf-8")
        return
    # Exercise the same generated part shapes as BeginPlay, in memory only.
    # Never save these runtime components into the map.
    zone_report = {}
    for actor in (original, new):
        actor.apply_config(actor.get_editor_property("dummy_config"))
        zones = sorted(actor.get_components_by_class(ue.MHGZMonsterHitzoneComponent),
                       key=lambda z: z.get_world_location().z)
        assert len(zones) == 3
        rows = []
        for color, zone in zip(COLORS, zones):
            query = next(c for c in zone.get_children_components(False) if isinstance(c, ue.BoxComponent))
            vb, qb = bounds(component(actor, color+"SegmentVisual")), bounds(query)
            assert str(query.get_collision_profile_name()) == "MonsterHitzone"
            assert query.get_collision_enabled() != ue.CollisionEnabled.NO_COLLISION
            assert str(zone.get_editor_property("extract_color_tag").get_editor_property("tag_name")).endswith(color)
            for axis in range(3):
                assert qb["origin"][axis]-qb["extent"][axis] <= vb["origin"][axis]-vb["extent"][axis]+.1
                assert qb["origin"][axis]+qb["extent"][axis] >= vb["origin"][axis]+vb["extent"][axis]-.1
            rows.append(dict(color=color, query_bounds=qb))
        zone_report[actor.get_actor_label()] = rows
    collision_report = []
    center = new.get_actor_location()
    for z in (RADIUS/6, RADIUS/2, RADIUS*5/6):
        start = ue.Vector(center.x+RADIUS+100, center.y, source["floor_top"]+z)
        end = ue.Vector(center.x, center.y, start.z)
        hit = ue.SystemLibrary.line_trace_single_for_objects(world, start, end,
            [ue.ObjectTypeQuery.OBJECT_TYPE_QUERY3], False, [], ue.DrawDebugTrace.NONE)
        assert hit is not None, ("Missing hemisphere body collision", z)
        details = hit.to_tuple()  # HitResult's registered native BreakHitResult.
        hit_component = details[10]
        assert hit_component.get_owner() == new, hit_component
        point = details[5]
        expected_x = center.x + math.sqrt(RADIUS**2-z**2)
        assert abs(point.x-expected_x)<1, (vec(point), expected_x)
        collision_report.append(dict(height=z, impact=vec(point), analytic_surface_x=expected_x))
    for p,sha in source["hashes"].items():
        if p!=MAP: assert digest(p)==sha,p
    report=dict(success=True, mode="Apply" if writing else "ColdReadOnly", targets=states,
                hemisphere_bounds=dict(min=lo,max=hi), hitzones=zone_report, body_traces=collision_report,
                protected_unchanged=len(source["hashes"])-1)
    (OUT/("apply.json" if writing else "verified.json")).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    ue.log("TRAINING_TARGETS_OK " + json.dumps(report,ensure_ascii=False))


if "--build-query-meshes" in sys.argv:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from training_target_query_meshes import build_meshes as build_query_meshes
    build_query_meshes()
elif "--build-meshes" in sys.argv:
    build_meshes()
else:
    import unreal as ue
    command_line = ue.SystemLibrary.get_command_line()
    if "-MHGZPreciseTrainingQueries" in command_line or (
        (ROOT / "Saved/Maintenance/TrainingTargetQueries20261025/applied.json").exists()
        and "-MHGZTrainingTargets" not in command_line):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from training_target_query_meshes import editor_main as precise_queries
        precise_queries()
    else:
        editor_main()
