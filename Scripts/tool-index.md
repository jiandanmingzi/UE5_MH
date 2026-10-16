# 维护工具发现索引

更新：2026-10-13。此索引用于先找已有工具，避免重复实现；不授权执行全部工具。环境来自脚本依赖，简述来自文件头。具体参数、读写范围和前置条件仍以脚本为准；常用且已核验的调用见[README](README.md)。历史迁移/退役工具不可自动重跑。

工具新增、改名或退役时更新本表。普通分析脚本可能写`Saved`报告；`UE Python`不代表只读资产。

中文检索提示：摄像头→camera；准心/界面→ui、hud；猎虫/虫印/虫粉→kinsect、powder；曲线/位移→curve、trajectory、motion；急袭突刺→descending_thrust、aerial_dive；撑杆跳→vault；蓝图接线→anim_graph、AnimBlueprint、Graph。

## AssetOrganization

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [AssetOrganization/apply_damage_formula_units.py](AssetOrganization/apply_damage_formula_units.py) | UE Python | Apply the insect-glaive damage-unit migration to project assets. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/repair_locomotion_boundaries.py](AssetOrganization/repair_locomotion_boundaries.py) | UE Python | 四条Loop首尾闭合及两份专用PSS入口速度特征；复用C++桥接，保留时长/累计位移/曲线/通知。 | 默认只读验证；Apply仅一次，先备份，拒绝覆盖已有事务 |
| [AssetOrganization/audit_animation_assets.py](AssetOrganization/audit_animation_assets.py) | UE Python | Write a read-only Unreal animation inventory to Saved/AssetOrganization. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_asset_paths.py](AssetOrganization/audit_asset_paths.py) | UE Python | Read-only audit for specific Unreal package paths. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_damage_formula_units.py](AssetOrganization/audit_damage_formula_units.py) | UE Python | Read-only audit of insect-glaive damage and physical hitzone values. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_m6_ui_widgets.py](AssetOrganization/audit_m6_ui_widgets.py) | UE Python | Deprecated: UE 5.6 Python does not expose WidgetBlueprint.widget_tree. | 已弃用；改用probe_m6_ui.py |
| [AssetOrganization/organize_asset_roots.py](AssetOrganization/organize_asset_roots.py) | UE Python | Move complete Unreal asset roots with count and backup safeguards. | 按需；先核对参数/写入范围 |
| [AssetOrganization/organize_assets.py](AssetOrganization/organize_assets.py) | UE Python | Dry-run or apply an Unreal asset migration manifest. | 按需；先核对参数/写入范围 |
| [AssetOrganization/probe_m6_ui.py](AssetOrganization/probe_m6_ui.py) | UE Python | Read-only M6 UI asset/reference probe; does not inspect unexposed WidgetTree. | 按需；先核对参数/写入范围 |
| [AssetOrganization/run_m6_montage_pinyin_migration.py](AssetOrganization/run_m6_montage_pinyin_migration.py) | UE Python | Run the reviewed M6 Montage rename manifest through the shared UE asset mover. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/run_m6_ui_remote.py](AssetOrganization/run_m6_ui_remote.py) | 普通Python | Compatibility entry point for the M6 UI setup and read-only probe. | 按需；先核对参数/写入范围 |
| [AssetOrganization/run_weapon_move_asset_pinyin_migration.py](AssetOrganization/run_weapon_move_asset_pinyin_migration.py) | UE Python | Run the reviewed weapon-move asset rename manifest through AssetTools. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/setup_m6_ui_widgets.py](AssetOrganization/setup_m6_ui_widgets.py) | UE Python | Create and wire the E6.3 HUD Widget Blueprints in UE 5.6. | 按需；先核对参数/写入范围 |
| [AssetOrganization/setup_montage_segment_blend.py](AssetOrganization/setup_montage_segment_blend.py) | UE Python | 默认审计Slot惯性混合；--configure-montages配置0.05s元数据；--remove-synthetic-bridges移除5个桥接；--remove-attack-holds移除10个攻击保持并更新4份GA入口；对应verify只读回查 | 默认只读；显式Apply才备份保存；检查点阻止重复；不删除序列、不动翻滚保持/蓄力 |
| [AssetOrganization/apply_ground_attack_entries.py](AssetOrganization/apply_ground_attack_entries.py) | UE Python | 六组地面入口显式配方，原相位通知重定位及308副本；保留突进蓄力/未点名来源 | 已应用；默认/已应用时仅审计，勿删除检查点盲目重跑 |
| [AssetOrganization/trigger_mhgz_livecoding_compile.py](AssetOrganization/trigger_mhgz_livecoding_compile.py) | UE Python | Ask the open MHGZ editor to compile and apply its Live Coding patch. | 按需；先核对参数/写入范围 |
| [AssetOrganization/verify_demo_structure.py](AssetOrganization/verify_demo_structure.py) | 普通Python | Verify the Git-tracked Content scaffold required by the combat demo. | 按需；先核对参数/写入范围 |
| [AssetOrganization/verify_project_assets.py](AssetOrganization/verify_project_assets.py) | UE Python | Verify the current finalized Demo asset layout and compile project Blueprints. | 按需；先核对参数/写入范围 |

## AssetProbe

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [AssetProbe/adjust_demo_arena_sunlight.py](AssetProbe/adjust_demo_arena_sunlight.py) | UE Python | Lower only the Demo Arena's directional sun intensity, with guarded readback. | 按需；先核对参数/写入范围 |
| [AssetProbe/analyze_kinsect_over_pose.py](AssetProbe/analyze_kinsect_over_pose.py) | UE Python | Read-only pose-cycle analysis for AS_Unsh_CaoChongZhan_Over. | 按需；先核对参数/写入范围 |
| [AssetProbe/analyze_kinsect_piercer_telemetry.py](AssetProbe/analyze_kinsect_piercer_telemetry.py) | 普通Python | Read-only Kinsect Piercer PIE audit; output evidence, not an automatic sign-off. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_descending_thrust_m1_poses.py](AssetProbe/audit_descending_thrust_m1_poses.py) | UE Python | Read-only M1 audit of current dive assets and existing falling-loop options. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_descending_thrust_runtime_loop_seam.py](AssetProbe/audit_descending_thrust_runtime_loop_seam.py) | UE Python | Read-only pose audit for the runtime loop observed in MHW ActionNo 14. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_anim_graph.py](AssetProbe/audit_kinsect_anim_graph.py) | UE Python | Read-only dump of ABP_MH_Character AnimGraph node/pin wiring. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_piercer_combo_entries.py](AssetProbe/audit_kinsect_piercer_combo_entries.py) | UE Python | Read-only audit of copied TuCi combo routes and target montage entry selection. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_slash_collision_windows.py](AssetProbe/audit_kinsect_slash_collision_windows.py) | UE Python | Read-only inspection of the saved Kinsect Slash montage windows and config. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_slash_movement_profile.py](AssetProbe/audit_kinsect_slash_movement_profile.py) | UE Python | Read saved Kinsect Slash defaults after a cold build; never save assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_m6_baseline.py](AssetProbe/audit_m6_baseline.py) | UE Python | Read-only M6.1 baseline audit for weapon wiring and Insect Glaive assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_finger_retarget.py](AssetProbe/audit_mhw_aerial_dive_finger_retarget.py) | UE Python | Audit source and retargeted finger poses for MHW aerial-dive review clips. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_root_motion.py](AssetProbe/audit_mhw_aerial_dive_root_motion.py) | UE Python | Read source/target hierarchy motion without changing Unreal content. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_weapon_pose.py](AssetProbe/audit_mhw_aerial_dive_weapon_pose.py) | UE Python | Compare MHW LMT381's final weapon/grip pose with native MHR idle poses. | 按需；先核对参数/写入范围 |
| [AssetProbe/configure_idle_vault_and_thrust_routes.py](AssetProbe/configure_idle_vault_and_thrust_routes.py) | UE Python | Update Idle Insect Glaive pole-vault and thrust Combo routes. | 按需；先核对参数/写入范围 |
| [AssetProbe/configure_training_dummy_hit_coverage.py](AssetProbe/configure_training_dummy_hit_coverage.py) | UE Python | Migrate only DA_TrainingDummy query geometry. Default is read-only. | 按需；先核对参数/写入范围 |
| [AssetProbe/create_mhw_aerial_dive_retarget_setup.py](AssetProbe/create_mhw_aerial_dive_retarget_setup.py) | UE Python | Create a reusable UE 5.6 IK Retarget setup for MHW dive 380/381. | 按需；先核对参数/写入范围 |
| [AssetProbe/create_mhw_aerial_dive_scale_variants.py](AssetProbe/create_mhw_aerial_dive_scale_variants.py) | UE Python | RETIRED: source-scale variants are no longer part of this review. | 已退役；入口拒绝执行 |
| [AssetProbe/ensure_kinsect_piercer_combo_entry_mappings.py](AssetProbe/ensure_kinsect_piercer_combo_entry_mappings.py) | UE Python | Mirror explicit Montage entry mappings for copied Kinsect Piercer Combo routes. | 按需；先核对参数/写入范围 |
| [AssetProbe/fix_a7_dance_vault.py](AssetProbe/fix_a7_dance_vault.py) | UE Python | A7: retune the dance vault to the MHRise ground truth. | 按需；先核对参数/写入范围 |
| [AssetProbe/fix_a9_root_lock.py](AssetProbe/fix_a9_root_lock.py) | UE Python | A9: lock the JumpOver root tracks so their pose lead cannot snap back. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_camera_defaults.py](AssetProbe/inspect_camera_defaults.py) | UE Python | Read the live Blueprint CDO camera values without editing assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_character_skeletons.py](AssetProbe/inspect_character_skeletons.py) | UE Python | Read-only Unreal Python probe for project and MHW review skeletons. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_demo_arena.py](AssetProbe/inspect_demo_arena.py) | UE Python | Read actor placement, bounds, collision, materials, and lights in L_DemoArena. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_ik_retarget_python_api.py](AssetProbe/inspect_ik_retarget_python_api.py) | UE Python | Print the exposed UE 5.6 Python API used to build a review retarget setup. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_kinsect_piercer_root_motion.py](AssetProbe/inspect_kinsect_piercer_root_motion.py) | UE Python | Read-only source-root and AnimBP audit, run with Unreal Python commandlet. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_mhw_aerial_dive_retarget_outputs.py](AssetProbe/inspect_mhw_aerial_dive_retarget_outputs.py) | UE Python | Inspect retargeted MHW dive assets and UE Python animation editing APIs. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_mhw_root_helpers.py](AssetProbe/inspect_mhw_root_helpers.py) | UE Python | Read-only compare Root helper bones in native MHR and MHW retarget bakes. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_kinsect_over_pose_api.py](AssetProbe/probe_kinsect_over_pose_api.py) | UE Python | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_kinsect_slash_dummy.py](AssetProbe/probe_kinsect_slash_dummy.py) | UE Python | Read-only audit of the live Kinsect Slash and training-dummy asset defaults. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_m5_assets.py](AssetProbe/probe_m5_assets.py) | UE Python | Read-only asset probe for the M5 aerial/back-vault audit. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_mhw_aerial_dive_retarget.py](AssetProbe/probe_mhw_aerial_dive_retarget.py) | UE Python | Read-only UE source/target hierarchy and animation-scale diagnostic. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_vault_seam_blend.py](AssetProbe/probe_vault_seam_blend.py) | UE Python | **采样器**：把撑杆跳接缝两侧的姿势倒成网格，交给离线脚本分析。 | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_vault_seam_pose.py](AssetProbe/probe_vault_seam_pose.py) | UE Python | 量撑杆跳蒙太奇 Jump→JumpOver 接缝处的**姿势落差**。 | 按需；先核对参数/写入范围 |
| [AssetProbe/read_asset_import_ids.py](AssetProbe/read_asset_import_ids.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [AssetProbe/repair_mhw_aerial_dive_root_motion.py](AssetProbe/repair_mhw_aerial_dive_root_motion.py) | UE Python | Repair body and helper tracks of the MHW->MHR review-only FK bake. | 按需；先核对参数/写入范围 |
| [AssetProbe/resize_demo_arena.py](AssetProbe/resize_demo_arena.py) | UE Python | Expand L_DemoArena and turn its boundary walls into invisible blockers. | 按需；先核对参数/写入范围 |
| [AssetProbe/retarget_mhw_aerial_dive_sequences.py](AssetProbe/retarget_mhw_aerial_dive_sequences.py) | UE Python | Create finger-chain-fixed MHW LMT 380/381 review retargets in UE5.6. | 按需；先核对参数/写入范围 |
| [AssetProbe/set_kinsect_slash_range.py](AssetProbe/set_kinsect_slash_range.py) | UE Python | Set the shared B / RT+B dash range in DA_IG_Combat and verify the saved asset. | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_kinsect_piercer_setup.ps1](AssetProbe/verify_kinsect_piercer_setup.ps1) | PowerShell | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_mhw_aerial_dive_fingerfixed_disk.py](AssetProbe/verify_mhw_aerial_dive_fingerfixed_disk.py) | 普通Python | Reload and verify the saved finger-fixed MHW dive review animations. | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_mhw_aerial_dive_root_motion.py](AssetProbe/verify_mhw_aerial_dive_root_motion.py) | UE Python | Read-only RAW/compressed verification of the saved review bake. | 按需；先核对参数/写入范围 |

## MHRise

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHRise/analyze_kinsect_slash_outcomes.py](MHRise/analyze_kinsect_slash_outcomes.py) | 普通Python | Inspect Kinsect Slash hit/miss captures without changing runtime assets. | 按需；先核对参数/写入范围 |
| [MHRise/audit_action_motion_captures.py](MHRise/audit_action_motion_captures.py) | 普通Python | Audit MHRise recorder CSVs for M6 action-ID episodes and frame telemetry. | 按需；先核对参数/写入范围 |
| [MHRise/build_fall_physics.py](MHRise/build_fall_physics.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_piercer_curves.py](MHRise/build_kinsect_piercer_curves.py) | 普通Python | Export an auditable intermediate trajectory dataset for MHRise motion 201. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_piercer_full_curves.py](MHRise/build_kinsect_piercer_full_curves.py) | 普通Python | Rebuild the Enhanced Kinsect Piercer (强化操虫穿刺) 200/201/203 chain curves. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_slash_curves.py](MHRise/build_kinsect_slash_curves.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_truth_table.py](MHRise/build_truth_table.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_vault_curves.py](MHRise/build_vault_curves.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/discover_action_ids_by_input.py](MHRise/discover_action_ids_by_input.py) | 普通Python | Discover MHRise motion IDs associated with controller input transitions. | 按需；先核对参数/写入范围 |
| [MHRise/MHGZ_AerialTrajectoryRecorder.lua](MHRise/MHGZ_AerialTrajectoryRecorder.lua) | 游戏Lua宿主 | F9轨迹/输入与独立animation.csv实际Motion节点、原生帧及可用切换请求；1.13.0修复空值错列 | 只读游戏状态；写配套录制文件；原生读取待短录验证，不把旧行为层当攻击帧 |
| [MHRise/recorder_path.py](MHRise/recorder_path.py) | 普通Python | Locate the installed MHRise recorder data across development machines. | 公共模块；导入复用 |

## MHRise/tests

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHRise/tests/test_recorder_animation_reader.py](MHRise/tests/test_recorder_animation_reader.py) | 普通Python + lupa | 执行真实Lua读取器的受限模拟、CSV行构造及Lua5.4/LuaJIT编译 | 定向工具测试；不启动游戏，不代替游戏内API验收 |
| [MHRise/tests/test_action_motion_capture_audit.py](MHRise/tests/test_action_motion_capture_audit.py) | 普通Python | Synthetic tests for MHRise action-ID episode and frame-field auditing. | 定向测试；按需 |
| [MHRise/tests/test_discover_action_ids_by_input.py](MHRise/tests/test_discover_action_ids_by_input.py) | 普通Python | Synthetic tests for controller-input to motion-ID discovery. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_piercer_curves.py](MHRise/tests/test_kinsect_piercer_curves.py) | 普通Python | Pure-function tests for the Enhanced Kinsect Piercer curve extractor. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_piercer_full_curves.py](MHRise/tests/test_kinsect_piercer_full_curves.py) | 普通Python | Tests for full 200 → 201 → 203 curve-chain extraction and canonical curves. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_slash_outcomes.py](MHRise/tests/test_kinsect_slash_outcomes.py) | 普通Python | Offline extraction tests; no Unreal Editor or live game required. | 定向测试；按需 |

## MHWorld

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHWorld/analyze_aerial_dive_trajectory.py](MHWorld/analyze_aerial_dive_trajectory.py) | 普通Python | Read-only numerical audit of MHW aerial dive's runtime 49532 / LMT380 phase. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_aerial_dive_inertia_source.py](MHWorld/audit_aerial_dive_inertia_source.py) | 普通Python | Compare pre-action identity and measured RT-edge speed against dive inertia. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_aerial_dive_model.py](MHWorld/audit_aerial_dive_model.py) | 普通Python | Numerical inspection of the first-phase exports; no game/asset writes. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_descending_thrust_m1_timing.py](MHWorld/audit_descending_thrust_m1_timing.py) | 普通Python | Build a derived timing audit for MHW runtime actions 49532/49533. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_common_fall_lmt.py](MHWorld/audit_mhw_common_fall_lmt.py) | 普通Python | Inventory MHW LMT actions for possible aerial-fall loop sources. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_common_fall_pose_cycles.py](MHWorld/audit_mhw_common_fall_pose_cycles.py) | Blender Python | Rank repeated-pose windows in an MHW LMT. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_fullbody_ue_assets.py](MHWorld/audit_mhw_fullbody_ue_assets.py) | UE Python | Read-only validation of the normalized full-body MHW review assets. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_rig_coverage.py](MHWorld/audit_mhw_rig_coverage.py) | Blender Python | Read-only audit of MHW armor-part MOD3 bones against WP10 LMT tracks. | 按需；先核对参数/写入范围 |
| [MHWorld/build_aerial_dive_curves.py](MHWorld/build_aerial_dive_curves.py) | 普通Python | Export observed LMT380 curves without averaging different planar inertias together. | 按需；先核对参数/写入范围 |
| [MHWorld/capture_mhw_xinput.py](MHWorld/capture_mhw_xinput.py) | 普通Python | Record raw XInput controller state and publish an atomic live sample. | 按需；先核对参数/写入范围 |
| [MHWorld/enable_blender_mhw.py](MHWorld/enable_blender_mhw.py) | Blender Python | Enable the installed MOD3/LMT add-on and persist Blender user preferences. | 按需；先核对参数/写入范围 |
| [MHWorld/export_aerial_dive_candidates.py](MHWorld/export_aerial_dive_candidates.py) | Blender Python | Export review FBXs from selected MHW WP10 LMT entries. | 按需；先核对参数/写入范围 |
| [MHWorld/export_mhw_fullbody_candidates.py](MHWorld/export_mhw_fullbody_candidates.py) | Blender Python | Build a review-only full-body MHW WP10 candidate rig from armor MOD3 parts. | 按需；先核对参数/写入范围 |
| [MHWorld/fit_aerial_dive_trajectory.py](MHWorld/fit_aerial_dive_trajectory.py) | 普通Python | Fit observations, excluding phase-entry carryover and contact-distorted samples. | 按需；先核对参数/写入范围 |
| [MHWorld/import_aerial_dive_candidates.py](MHWorld/import_aerial_dive_candidates.py) | UE Python | Import the exported MHW aerial-dive review FBXs as isolated UE assets. | 按需；先核对参数/写入范围 |
| [MHWorld/import_mhw_fullbody_candidates.py](MHWorld/import_mhw_fullbody_candidates.py) | UE Python | Import the review-only MHW full-body rig and candidate actions into UE. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/import_mhw_fullbody_on_existing_skeleton.py](MHWorld/import_mhw_fullbody_on_existing_skeleton.py) | UE Python | Try a review-only full-body mesh import against the known MHW body Skeleton. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/inspect_exported_fbx_scene.py](MHWorld/inspect_exported_fbx_scene.py) | Blender Python | Inspect an FBX in a fresh Blender process without saving or modifying it. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_asset_candidates.py](MHWorld/inspect_mhw_asset_candidates.py) | 普通Python | Read-only inventory check for candidate MHW hunter MOD3/LMT files. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_lmt_action_motion.py](MHWorld/inspect_mhw_lmt_action_motion.py) | 普通Python | Report which MOD3 bone IDs actually change in selected MHW LMT entries. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_skeleton.py](MHWorld/inspect_mhw_skeleton.py) | 普通Python | Read-only dump of the MHW reference MOD3 skeleton hierarchy. | 按需；先核对参数/写入范围 |
| [MHWorld/mhw_animation_current_action.py](MHWorld/mhw_animation_current_action.py) | Blender Python | Blender FBX operator preset: selected rig/mesh, current action, full sampling. | 按需；先核对参数/写入范围 |
| [MHWorld/prepare_mhw_oodle.py](MHWorld/prepare_mhw_oodle.py) | 普通Python | Unpack the Oodle dependency from the official URL used by MHWNoChunk. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_aerial_dive_weapon_scale.py](MHWorld/probe_aerial_dive_weapon_scale.py) | Blender Python | Read-only comparison of raw LMT and exported review rig scale tracks. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_blender_mhw.py](MHWorld/probe_blender_mhw.py) | Blender Python | Environment probe, executed by Blender in background mode without saving. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_mhw_ue_import_api.py](MHWorld/probe_mhw_ue_import_api.py) | UE Python | Read-only UE Python API probe for isolated MHW review asset import. | 按需；先核对参数/写入范围 |
| [MHWorld/reimport_mhw_aerial_dive_review.py](MHWorld/reimport_mhw_aerial_dive_review.py) | UE Python | Rebuild the isolated MHW Aerial Dive review import with canonical names. | 按需；先核对参数/写入范围 |
| [MHWorld/render_mhw_common_lmt_pose_sheet.py](MHWorld/render_mhw_common_lmt_pose_sheet.py) | 普通Python | Render read-only skeletal contact sheets from MHW LMT pose samples. | 按需；先核对参数/写入范围 |
| [MHWorld/repair_mhw_fullbody_review_assets.py](MHWorld/repair_mhw_fullbody_review_assets.py) | UE Python | Repair only the isolated MHW full-body review mesh and preview assets. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/test_capture_mhw_xinput.py](MHWorld/test_capture_mhw_xinput.py) | 普通Python | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [MHWorld/verify_blender_mhw.py](MHWorld/verify_blender_mhw.py) | Blender Python | Validate the staged MHW add-on and a synthetic skeleton/animation FBX round trip. | 按需；先核对参数/写入范围 |

## tests

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [tests/test_maintenance_tools.py](tests/test_maintenance_tools.py) | 普通Python | Focused launcher/type-adapter checks; no UE process or gameplay suite. | 定向测试；按需 |

## 公共入口与录制器

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHGZ_MHW_AerialTrajectoryRecorder.lua](MHGZ_MHW_AerialTrajectoryRecorder.lua) | 游戏Lua宿主 | MHGZ Monster Hunter: World runtime recorder | 按需；先核对参数/写入范围 |
| [ue_asset_utils.py](ue_asset_utils.py) | 普通Python | Small, UE-independent adapters for reflected values; errors stay explicit. | 公共模块；导入复用 |
| [ue_remote.py](ue_remote.py) | 普通Python | Execute a Python file in the matching open UE editor using Epic's client. | 按需；先核对参数/写入范围 |

## 原生编辑器反射桥接

2026-10-15：下表`MHGZAnimationSeamLibrary`新增定向ABP事务标志修复、两MM根运动/最终Pose History配置，以及拒绝覆盖的Loop候选库/相位Schema创建入口。均不自动保存；合同与已完成实例见[Scripts README](README.md#assetorganization)及[维护记录§7](../docs/editor/locomotion-maintenance-20261015.md#7-按顺序实施的当前结果)，不重复执行既有检查点。

| 入口 | 环境/源码 | 用途与约束 |
|---|---|---|
| `unreal.MHGZAnimationSeamLibrary` | UE Python调用；[声明](../Source/MHGZToolsEditor/Public/MHGZAnimationSeamLibrary.h)、[实现](../Source/MHGZToolsEditor/Private/MHGZAnimationSeamLibrary.cpp) | 常驻1帧中间姿态/推进根位移；单Slot插段、替换、裁切、刷新总长；同步通知/Section。显式索引、备份、输出不存在及无PIE/脏资产检查由调用者执行；不自动保存，不可盲目重跑。不是Commandlet，不搬玩法父类 |

## 既有编辑器Commandlet

通过对应引擎的`UnrealEditor-Cmd.exe <project> -run=<入口>`调用。下列多数属于历史阶段修复，运行前读取对应`.h/.cpp`与任务条件；工具存在不表示当前需要再次Apply。已有AnimBP审计、修复和表现资产桥接优先复用。

| 入口 | 源码 |
|---|---|
| `MHGZAerialHandoffSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZAerialHandoffSetupCommandlet.h)；历史写资产工具，不作为迁移验证命令 |
| `MHGZActionVisualPitchGraph` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZActionVisualPitchGraphCommandlet.h) |
| `MHGZAirDodgeMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZAirDodgeMontageSetupCommandlet.h) |
| `MHGZDodgeBlendInSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZDodgeBlendInSetupCommandlet.h) |
| `MHGZE4ActionExitAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionExitAuditCommandlet.h) |
| `MHGZE4ActionIdleAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionIdleAssetSetupCommandlet.h) |
| `MHGZE4ActionIdleRouting` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionIdleRoutingCommandlet.h) |
| `MHGZE4AnimBlueprintAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4AnimBlueprintAuditCommandlet.h)；Editor模块，原短入口不变；只读蓝图连线审计 |
| `MHGZE4ExitAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitAssetSetupCommandlet.h) |
| `MHGZE4ExitCallbackPolish` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitCallbackPolishCommandlet.h) |
| `MHGZE4ExitTransition` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitTransitionCommandlet.h) |
| `MHGZE4HandoffNotifySetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4HandoffNotifySetupCommandlet.h) |
| `MHGZE4PreSearchRouting` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4PreSearchRoutingCommandlet.h) |
| `MHGZE4SheathedExitGraphFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4SheathedExitGraphFixupCommandlet.h) |
| `MHGZE4SheathedMacroNaming` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4SheathedMacroNamingCommandlet.h)；Editor模块，原短入口不变；历史写资产工具，无AuditOnly，不作为日常迁移验证命令 |
| `MHGZE4TailStartTimeRepair` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4TailStartTimeRepairCommandlet.h) |
| `MHGZE64DamageNumberSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE64DamageNumberSetupCommandlet.h) |
| `MHGZKinsectActionLayer` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZKinsectActionLayerCommandlet.h) |
| `MHGZM421LoopCandidateAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM421LoopCandidateAssetSetupCommandlet.h) |
| `MHGZM5DescendingThrustAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5DescendingThrustAssetSetupCommandlet.h) |
| `MHGZM5KinsectPiercerAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectPiercerAssetSetupCommandlet.h) |
| `MHGZM5KinsectPiercerCurveSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectPiercerCurveSetupCommandlet.h) |
| `MHGZM5KinsectSlashAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectSlashAssetSetupCommandlet.h) |
| `MHGZM5KinsectSlashCurveSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectSlashCurveSetupCommandlet.h) |
| `MHGZM631MarkConfig` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM631MarkConfigCommandlet.h) |
| `MHGZM636VisualAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM636VisualAssetSetupCommandlet.h) |
| `MHGZM63PowderVortexAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM63PowderVortexAssetSetupCommandlet.h) |
| `MHGZM64UISubsystemAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM64UISubsystemAuditCommandlet.h)；Editor模块试迁移，原入口保持不变 |
| `MHGZM6AssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6AssetSetupCommandlet.h) |
| `MHGZM6BaselineAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6BaselineAuditCommandlet.h) |
| `MHGZM6RecordedHunterFlightSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6RecordedHunterFlightSetupCommandlet.h) |
| `MHGZM6ReleaseFacingNotify` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6ReleaseFacingNotifyCommandlet.h) |
| `MHGZPMM4AssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM4AssetFixupCommandlet.h) |
| `MHGZPMM4AssetReport` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM4AssetReportCommandlet.h) |
| `MHGZPMM5AnimGraphFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM5AnimGraphFixupCommandlet.h) |
| `MHGZPMM7AssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM7AssetFixupCommandlet.h) |
| `MHGZPMMAssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMAssetFixupCommandlet.h) |
| `MHGZPMMExtendedStop` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMExtendedStopCommandlet.h) |
| `MHGZPMMNotifyTrackRepair` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMNotifyTrackRepairCommandlet.h) |
| `MHGZPoleVaultMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPoleVaultMontageSetupCommandlet.h) |
| `MHGZPoseSearchTelemetryExport` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPoseSearchTelemetryExportCommandlet.h) |
| `MHGZStripPresentationRootMotion` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZStripPresentationRootMotionCommandlet.h)；历史写资产工具，不作为迁移验证命令 |
| `MHGZWuTaMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZWuTaMontageSetupCommandlet.h) |
