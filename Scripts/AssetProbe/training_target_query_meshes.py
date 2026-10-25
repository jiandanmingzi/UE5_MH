"""Precise solid hurtbox extension of configure_demo_training_targets.py.

The old tool only checked AABB coverage. This extension generates circumscribed
UCX geometry and migrates three configs, without touching map/body/visual assets.
Invoke through that tool: Blender --build-query-meshes; UE -MHGZPreciseTrainingQueries
(freeze/verify), then once -MHGZPreciseTrainingQueriesApply (write).
"""
import hashlib
import json
import math
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Saved/Maintenance/TrainingTargetQueries20261025"
MESH_DIR = "/Game/Monster/TrainingDummy/Meshes"
COLORS = ("White", "Orange", "Red")
CONFIGS = ["/Game/Monster/TrainingDummy/Data/DA_TrainingDummy" + x
           for x in ("", "_Large", "_Hemisphere")]
NAMES = ["SM_TD_Query_Cylinder"] + ["SM_TD_Query_Hemisphere_" + c for c in COLORS]
SIDES, SLICES, RADIUS, MARGIN = 32, 8, 400.0, 2.0


def geometry():
    """Each tuple is a separate solid convex hull, in band-centred cm space."""
    yield NAMES[0], [[(1.0, -1.0), (1.0, 1.0)]]
    for band, name in enumerate(NAMES[1:]):
        z0, z1 = RADIUS * band / 3, RADIUS * (band + 1) / 3
        lo, hi = math.asin(z0 / RADIUS), math.asin(z1 / RADIUS)
        hulls = []
        for row in range(SLICES):
            a, b = lo + (hi-lo)*row/SLICES, lo + (hi-lo)*(row+1)/SLICES
            mid = (a+b)/2
            lower, upper = RADIUS*math.sin(a)-MARGIN, RADIUS*math.sin(b)+MARGIN
            # Intersect three circumscribed tangent cones, not only the middle
            # plane: near the pole the middle plane alone flares at its bottom.
            planes = [((RADIUS+MARGIN)/math.cos(t), -math.tan(t))
                      for t in (a, mid, b) if math.cos(t) > 1e-6]
            knots = [lower, upper]
            for i, (c, slope) in enumerate(planes):
                for d, other_slope in planes[i+1:]:
                    z = (d-c)/(slope-other_slope)
                    if lower < z < upper:
                        knots.append(z)
            hulls.append([(min(c+slope*z for c,slope in planes), z-(z0+z1)/2)
                          for z in sorted(set(knots))])
        yield name, hulls


def hull_mesh(hull):
    vertices = [(r / math.cos(math.pi/SIDES) * math.cos(2*math.pi*j/SIDES),
                 r / math.cos(math.pi/SIDES) * math.sin(2*math.pi*j/SIDES), z)
                for r, z in hull for j in range(SIDES)]
    last = (len(hull)-1)*SIDES
    faces = [tuple(reversed(range(SIDES))), tuple(range(last, last+SIDES))]
    faces += [(row*SIDES+j, row*SIDES+(j+1)%SIDES, (row+1)*SIDES+(j+1)%SIDES, (row+1)*SIDES+j)
              for row in range(len(hull)-1) for j in range(SIDES)]
    return vertices, faces


def build_meshes():
    import bpy
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 0.01
    dest = OUT / "Source"
    dest.mkdir(parents=True, exist_ok=True)
    report = []
    for name, hulls in geometry():
        bpy.ops.object.select_all(action="DESELECT")
        all_vertices, all_faces, objects = [], [], []
        for index, hull in enumerate(hulls):
            vertices, faces = hull_mesh(hull)
            all_faces.extend(tuple(i+len(all_vertices) for i in f) for f in faces)
            all_vertices.extend(vertices)
            mesh = bpy.data.meshes.new(f"UCX_{name}_{index:02}")
            mesh.from_pydata(vertices, [], faces)
            mesh.update()
            obj = bpy.data.objects.new(mesh.name, mesh)
            bpy.context.collection.objects.link(obj)
            objects.append(obj)
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(all_vertices, [], all_faces)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        objects.append(obj)
        for o in objects:
            o.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = dest / (name + ".fbx")
        bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", mesh_smooth_type="FACE",
            axis_forward="-Y", axis_up="Z", bake_anim=False, add_leaf_bones=False)
        report.append(dict(name=name, hulls=len(hulls), file=str(path)))
    (OUT / "mesh-source.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("PRECISE_QUERY_FBX_OK", len(report))


def editor_main():
    import unreal as ue
    sys.path.insert(0, str(ROOT / "Scripts"))
    from ue_asset_utils import as_sequence
    OUT.mkdir(parents=True, exist_ok=True)
    resuming = "-MHGZPreciseTrainingQueriesResume" in ue.SystemLibrary.get_command_line()
    writing = "-MHGZPreciseTrainingQueriesApply" in ue.SystemLibrary.get_command_line() or resuming
    refining = "-MHGZPreciseTrainingQueriesRefine" in ue.SystemLibrary.get_command_line()
    replacing = "-MHGZPreciseTrainingQueriesReplacePreservedCollision" in ue.SystemLibrary.get_command_line()
    source_path, applied_path = OUT / "source.json", OUT / "applied.json"
    packages = [MESH_DIR + "/" + n for n in NAMES]
    protected = ["/Game/Maps/L_DemoArena", "/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy",
                 "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat",
                 "/Game/Environment/DemoArena/Meshes/SM_Cylinder"]
    protected += [MESH_DIR + "/SM_TD_Hemisphere_" + c for c in COLORS]
    protected += ["/Game/Monster/TrainingDummy/Materials/MI_TD_" + c for c in COLORS]

    def disk(p):
        return ROOT / ("Content/" + p.removeprefix("/Game/") + (".umap" if p.endswith("L_DemoArena") else ".uasset"))

    def digest(p):
        return hashlib.sha256(disk(p).read_bytes()).hexdigest()

    def kept_zone(z):
        # All old fields survive verbatim, including flesh, identity and offsets.
        return {key: re.sub(r" \(0x[0-9A-Fa-f]+\)", "", str(z.get_editor_property(key))) for key in (
            "bone_name", "hitzone_tag", "extract_color_tag", "defense_multiplier",
            "physical_hitzone_multipliers", "element_hitzone_multipliers", "stagger_rate",
            "relative_location", "radius", "box_half_extent")}

    configs = [ue.load_asset(p) for p in CONFIGS]
    assert all(configs), "Missing training target configs"
    if not source_path.exists():
        assert not writing, "Freeze source before applying"
        source = dict(hashes={p: digest(p) for p in CONFIGS+protected},
                      zones={p: [kept_zone(z) for z in as_sequence(c.get_editor_property("hitzones"))]
                             for p, c in zip(CONFIGS, configs)})
        for p in CONFIGS:
            backup = OUT / "Backup" / disk(p).name
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk(p), backup)
        source_path.write_text(json.dumps(source, indent=2), encoding="utf-8")
        ue.log("PRECISE_QUERY_SOURCE_FROZEN")
        return
    source = json.loads(source_path.read_text(encoding="utf-8"))
    for p in protected:
        assert digest(p) == source["hashes"][p], ("Protected asset changed", p)
    mesh_api = ue.get_editor_subsystem(ue.StaticMeshEditorSubsystem) or ue.get_default_object(ue.StaticMeshEditorSubsystem)
    def convex_count(mesh):
        # UE5.6 GetSimpleCollisionCount omits ConvexElems entirely.
        return len(as_sequence(mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
                               .get_editor_property("convex_elems")))
    if refining or replacing:
        applied = json.loads(applied_path.read_text(encoding="utf-8"))
        assert applied["state"] == "saved"
        assert (not applied.get("collision_replaced")) if replacing else (not applied.get("curvature_refined"))
        assert all(digest(p) == sha for p,sha in applied["hashes"].items())
        if replacing:
            world = ue.EditorLoadingAndSavingUtils.load_map("/Game/Maps/L_DemoArena")
            ue.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")
        for name, p in zip(NAMES[1:], packages[1:]):
            backup = OUT / ("Backup/BeforeCollisionReplacement" if replacing else "Backup/BeforeCurvatureRefinement") / disk(p).name
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk(p), backup)
            if replacing:
                assert mesh_api.remove_collisions(ue.load_asset(p))
            import_mesh(name, ue, replace=True)
            mesh = ue.load_asset(p)
            assert convex_count(mesh) == SLICES
            mesh.get_editor_property("body_setup").set_editor_property(
                "collision_trace_flag", ue.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            mesh_api.enable_section_collision(mesh, True, 0, 0)
            assert ue.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
            applied["hashes"][p] = digest(p)
        applied["curvature_refined"] = True
        if replacing:
            applied["collision_replaced"] = True
        applied_path.write_text(json.dumps(applied, indent=2), encoding="utf-8")
    if writing:
        assert all(digest(p) == source["hashes"][p] for p in CONFIGS)
        assert all((OUT/"Source"/(n+".fbx")).exists() for n in NAMES)
        if resuming:
            partial = json.loads(applied_path.read_text(encoding="utf-8"))
            assert partial["state"] == "importing" and partial.get("partial_hashes")
            for p in packages:
                assert ue.EditorAssetLibrary.does_asset_exist(p) == (p in partial["partial_hashes"])
            assert all(digest(p) == sha for p, sha in partial["partial_hashes"].items())
        else:
            assert not applied_path.exists(), "Do not replay partial/applied transaction"
            assert all(not ue.EditorAssetLibrary.does_asset_exist(p) for p in packages)
            partial = dict(state="importing", partial_hashes={})
            applied_path.write_text(json.dumps(partial), encoding="utf-8")
        for name in NAMES:
            package = MESH_DIR + "/" + name
            if not ue.EditorAssetLibrary.does_asset_exist(package):
                import_mesh(name, ue)
            mesh = ue.load_asset(package)
            assert mesh and convex_count(mesh) == (1 if name == NAMES[0] else SLICES), (name, convex_count(mesh))
            mesh.get_editor_property("body_setup").set_editor_property(
                "collision_trace_flag", ue.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            mesh_api.enable_section_collision(mesh, True, 0, 0)
            assert ue.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
            partial["partial_hashes"][package] = digest(package)
            applied_path.write_text(json.dumps(partial, indent=2), encoding="utf-8")
        for index, config in enumerate(configs):
            zones = as_sequence(config.get_editor_property("hitzones"))
            ordered = sorted(zones, key=lambda z: z.get_editor_property("relative_location").z)
            for band, zone in enumerate(ordered):
                mesh = ue.load_asset(packages[0] if index < 2 else packages[band+1])
                scale = ue.Vector(77, 77, 58.25) if index == 0 else ue.Vector(152, 152, 114.5) if index == 1 else ue.Vector(1, 1, 1)
                zone.set_editor_property("shape", ue.MHGZHitzoneShape.MESH)
                zone.set_editor_property("query_mesh", mesh)
                zone.set_editor_property("query_mesh_scale", scale)
            config.set_editor_property("hitzones", zones)
            assert ue.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)
        applied_path.write_text(json.dumps(dict(state="saved", hashes={p: digest(p) for p in CONFIGS+packages}), indent=2), encoding="utf-8")

    assert applied_path.exists(), "Frozen only; explicit apply still pending"
    applied = json.loads(applied_path.read_text(encoding="utf-8"))
    assert applied["state"] == "saved"
    for p, sha in applied["hashes"].items():
        assert digest(p) == sha, ("Changed since apply", p)
    for p in protected:
        assert digest(p) == source["hashes"][p], p
    report = dict(success=True, mode="Apply" if writing else "ColdReadOnly", configs=[], meshes=[], protected_unchanged=len(protected))
    for index, (p, c) in enumerate(zip(CONFIGS, configs)):
        zones = as_sequence(c.get_editor_property("hitzones"))
        frozen = [{k: re.sub(r" \(0x[0-9A-Fa-f]+\)", "", v) for k,v in row.items()}
                  for row in source["zones"][p]]
        assert [kept_zone(z) for z in zones] == frozen, p
        ordered = sorted(zones, key=lambda z: z.get_editor_property("relative_location").z)
        for band, z in enumerate(ordered):
            assert z.get_editor_property("shape") == ue.MHGZHitzoneShape.MESH
            mesh_ref = z.get_editor_property("query_mesh")
            expected = packages[0] if index < 2 else packages[band+1]
            assert mesh_ref.get_path_name().split(".")[0] == expected, (str(mesh_ref), expected)
            actual = z.get_editor_property("query_mesh_scale")
            expected_scale = (77,77,58.25) if index == 0 else (152,152,114.5) if index == 1 else (1,1,1)
            assert all(abs(a-b)<.001 for a,b in zip((actual.x,actual.y,actual.z), expected_scale))
        report["configs"].append(dict(path=p, parts=len(zones)))
    for p in packages:
        mesh = ue.load_asset(p)
        count = convex_count(mesh)
        assert count == (1 if p == packages[0] else SLICES)
        assert mesh.get_editor_property("body_setup").get_editor_property("collision_trace_flag") == ue.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX
        report["meshes"].append(dict(path=p, convex_hulls=count))
    # The placed instances carry visual overrides; verify their generated runtime
    # queries at those exact transforms, without saving runtime components/map.
    ue.EditorLoadingAndSavingUtils.load_map("/Game/Maps/L_DemoArena")
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem).get_all_level_actors()
    report["placed_targets"] = []
    for label in ("Arena_TrainingDummy", "Arena_TrainingDummy_Hemisphere_R400"):
        actor = next(a for a in actors if a.get_actor_label() == label)
        actor.apply_config(actor.get_editor_property("dummy_config"))
        zones = sorted(actor.get_components_by_class(ue.MHGZMonsterHitzoneComponent), key=lambda z:z.get_world_location().z)
        assert len(zones) == 3
        parts = []
        for color, zone in zip(COLORS, zones):
            query = next(c for c in zone.get_children_components(False) if isinstance(c, ue.StaticMeshComponent))
            visual = next(c for c in actor.get_components_by_class(ue.StaticMeshComponent) if c.get_name() == color+"SegmentVisual")
            assert zone.get_collision_enabled() == ue.CollisionEnabled.NO_COLLISION
            assert query.get_collision_enabled() == ue.CollisionEnabled.QUERY_ONLY
            assert str(query.get_collision_profile_name()) == "MonsterHitzone" and not query.is_visible()
            assert str(zone.get_editor_property("extract_color_tag").get_editor_property("tag_name")).endswith(color)
            vc, ve, _ = ue.SystemLibrary.get_component_bounds(visual)
            qc, qe, _ = ue.SystemLibrary.get_component_bounds(query)
            for axis in ("x", "y", "z"):
                assert getattr(qc,axis)-getattr(qe,axis) <= getattr(vc,axis)-getattr(ve,axis)+.01
                assert getattr(qc,axis)+getattr(qe,axis) >= getattr(vc,axis)+getattr(ve,axis)-.01
            parts.append(dict(color=color, mesh=query.get_editor_property("static_mesh").get_path_name(),
                              query_origin=[qc.x,qc.y,qc.z], query_extent=[qe.x,qe.y,qe.z]))
        report["placed_targets"].append(dict(label=label, parts=parts))
    for p in protected:
        assert digest(p) == source["hashes"][p], p
    (OUT/("apply.json" if writing else "verified.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    ue.log("PRECISE_QUERY_OK " + json.dumps(report))


def import_mesh(name, ue, replace=False):
    task = ue.AssetImportTask()
    task.filename = str(OUT/"Source"/(name+".fbx"))
    task.destination_path, task.destination_name = MESH_DIR, name
    task.automated, task.replace_existing, task.save = True, replace, True
    task.replace_existing_settings = replace
    task.factory = ue.FbxFactory()
    options = ue.FbxImportUI()
    options.import_mesh, options.import_as_skeletal = True, False
    options.import_materials, options.import_textures = False, False
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = ue.FBXImportType.FBXIT_STATIC_MESH
    options.static_mesh_import_data.combine_meshes = False
    options.static_mesh_import_data.auto_generate_collision = False
    options.static_mesh_import_data.one_convex_hull_per_ucx = True
    task.options = options
    ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
