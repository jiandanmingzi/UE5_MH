"""Blender FBX operator preset: selected rig/mesh, current action, full sampling."""

import bpy

op = bpy.context.active_operator
op.use_selection = True
op.object_types = {"ARMATURE", "MESH"}
op.global_scale = 1.0
op.apply_unit_scale = True
op.apply_scale_options = "FBX_SCALE_NONE"
op.axis_forward = "-Y"
op.axis_up = "Z"
op.bake_space_transform = False
op.add_leaf_bones = False
op.use_armature_deform_only = False
op.armature_nodetype = "NULL"
op.bake_anim = True
op.bake_anim_use_all_bones = True
op.bake_anim_use_nla_strips = False
op.bake_anim_use_all_actions = False
op.bake_anim_force_startend_keying = True
op.bake_anim_step = 1.0
op.bake_anim_simplify_factor = 0.0
