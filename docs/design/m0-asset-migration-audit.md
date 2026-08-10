# M0 资产迁移审计——MHGZ 虫棍 Demo

> 状态：以分阶段实施前基线为起点的证据清单。本文记录磁盘上实际存在的资产、迁移处置与负责里程碑；无法从工作树验证的 `.uasset` 内部数据一律列为编辑器待确认项。

## 1. Baseline and build status

- Baseline commit: `870c39bccb0dfbefe492041d6fd250a633d9103e` — "chore: 保存分阶段实现前基线" (verified as HEAD on 2026-08-10).
- Baseline build: **succeeded** — `MHGZEditor Win64 Development`, 2026-08-10.
- M0 code build: **succeeded**；`MHGZ.M0` 5 个自动化测试全部通过（含 TagLedger 重叠所有者回收）。
- M0 全资产 Data Validation 能加载旧 `DA_IG_Combo`，无 Missing Struct/Property；当前仅稳定报告 E0 必须补录的 `TransitionID` 与 `TargetState` 两项错误。
- 本代码阶段没有修改任何 `.uasset`；首次重存仍由 E0 执行。

## 2. Evidence conventions

- **Filename/string evidence**: verified from the git tree, the `Content/` file listing, and read-only `rg` over `Source/`. Exact paths and identifiers.
- **Editor-only unknown**: requires UE 5.6 / Reference Viewer / asset binary inspection. Not asserted here; listed in §7.
- `.uasset` files are binary; no claim in this audit is based on decoding them.

## 3. Target asset inventory (filename evidence, `Content/`)

### 3.1 Present

| Asset | Path |
|---|---|
| BP_IG_Character | `/Game/Blueprints/Characters/Demo/BP_IG_Character` |
| BP_PlayerState | `/Game/Blueprints/Characters/Demo/BP_PlayerState` |
| BP_Demo_GameMode | `/Game/Blueprints/GameModes/Demo/BP_Demo_GameMode` |
| BP_MHGZ_PlayerController | `/Game/Blueprints/PlayerController/Demo/BP_MHGZ_PlayerController` |
| BP_TrainingDummy | `/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy` |
| ABP_MH_Character + PSD/PSS motion matching | `/Game/Blueprints/Characters/Demo/Animation/...` |
| GA_IG_BaDao / GA_IG_R_TuCI | `/Game/Blueprints/Ability/InsectGlaive/...` |
| DA_IG_Combo / DA_IG_HuoLongGun | `/Game/Weapons/InsectGlaive/Data/...` |
| AM_Shth_BaDao / AM_Shth_R_TuCi | `/Game/Weapons/InsectGlaive/Anims/Montage/...` |
| DT_WeaponComboConfig (old DataTable) | `/Game/Data/DT_WeaponComboConfig` |
| DA_TrainingDummy | `/Game/Monster/TrainingDummy/Data/DA_TrainingDummy` |
| Input actions + IMC | `/Game/Input/Actions/MHGZ/IA_{A,B,LB,Look,LT,MouseLook_Demo,Move,RB,RT,RTA,RTB,RTY,Y,YB}`, `/Game/Input/Contexts/IMC_MHGZ_Demo` |
| L_DemoArena | `/Game/Maps/L_DemoArena` |
| IG art: Glaive/Kinsect meshes, materials, textures | `/Game/Weapons/InsectGlaive/{Meshes,Materials,Textures}/...` |
| Imported IG sequences | `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/...` (incl. `Review/Unknown`, `Review/UnusedCandidate`, `Transitions`, `Locomotion`) |

### 3.2 Missing (target shells, filename evidence — absent from the tree)

| Missing target | Needed by |
|---|---|
| `DA_IG_Combat`, `DA_IG_InputProfile` | M0 (plan §M0 item 5) |
| `DA_WeaponRuntime_IG` | E3 (final C++ `UWeaponRuntimeDefinition` structure) |
| `GE_IG_WhiteExtract/OrangeExtract/RedExtract/TripleUp` | E4; `GameplayEffects/{Core,InsectGlaive}` contain only `.gitkeep` |
| `GA_Dodge`; all planned `GA_IG_*` actions | M1 / M3-M6 |
| `BP_MHGZHUD`, `WBP_*` widgets | E2/E6; `/Game/UI/*` contains only `.gitkeep` |
| GameplayCue assets | E6; `/Game/GameplayCues/{Hit,InsectGlaive}` contain only `.gitkeep` |
| `IA_Dodge` (by name) | E3 §5.3; `IA_A` exists — editor check whether it is the dodge action |
| `DA_IG_Kinsect_Speed`, `BP_IG_MarkProjectile`, `BP_IG_PowderCloud`, `BP_DamageNumber` | E5/E6 |

No `DT_WeaponResourceConfig` exists in this tree (only `DT_WeaponComboConfig` is present).

## 4. Keep / Rewrite / Delete / Defer

| Target | Verdict | Evidence / required adjustment | Owner |
|---|---|---|---|
| BP_PlayerState (ASC/AttributeSet/Equipment identity) | Keep | Present. Refactor-scope "GAS 身份": remove ASC input binding and pawn-state-init authority (M1). | M1 |
| BP_IG_Character | Keep | Present. E2.3: single RuntimeHost/Aim/MotionWarping, weapon on `Weapon_R` socket, default `DA_IG_HuoLongGun`, `ABP_MH_Character`. | M1/M2 |
| BP_MHGZ_PlayerController | Keep | Present. E2.2: clear `DefaultMappingContexts`; single `IMC_MHGZ_Demo` owned by `UMHGZInputComponent`. | M1 |
| BP_Demo_GameMode | Keep | Present. E2.4: final class wiring (Pawn/Controller/PlayerState/HUD). | M2 |
| ABP_MH_Character + PSD/PSS | Keep | Present. Refactor-scope "地面移动": motion-matching locomotion; rotation/movement tokens yield ownership (M5). | M1/M5 |
| GA_IG_BaDao, GA_IG_R_TuCI | Rewrite | Present. E0 first-round targets; re-parent to final IG action base, `InstancedPerExecution`, `AttackSegments`. | M1/M2 |
| AM_Shth_BaDao, AM_Shth_R_TuCi | Rewrite/migrate | Present. E0 montage list; notifies migrate to final native classes (AttackCollision/ComboWindow/DodgeWindow/Counter/Movement/MotionWarping). | M1/M2 |
| DA_IG_Combo | Migrate + resave | Present. C++ 已改为 `FComboTransition/Transitions` 并注册 Redirect；Data Validation 证明资产能加载，但 `TransitionID/TargetState` 尚未补录。 | E0 editor + M1 runtime |
| DA_IG_HuoLongGun | Rewrite | Present. E3 §5.1 final fields (ItemID/WeaponTypeTag/RuntimeDefinition/Mesh/AttachSocket/AttackPower); remove old DataTable rows. | M0/M2 |
| DT_WeaponComboConfig | Delete (after migration) | Present. Old DataTable；`DefaultGame.ini` 的 `[MHGZDataManager]` 仍暂时接线；重构合同禁止长期双运行时。 | M1/M2 |
| IA_* + IMC_MHGZ_Demo | Keep | Present. Binding ownership moves to `UMHGZInputComponent` (M1). `IA_RTA/RTB/RTY/YB` are old chord-trigger assets; new design forbids EI chord triggers — verify references in editor before deleting. | M1 |
| DA_TrainingDummy + BP_TrainingDummy | Keep concept | Present. E5/E7.3: `MonsterBody` + three `MonsterHitzone` (Head=Red, Torso=Orange, Leg=White), deterministic `CounterTestAttack`. | M2 |
| L_DemoArena | Keep | Present; single demo map. | M2/E5 |
| IG Glaive/Kinsect art | Keep | Present under `/Game/Weapons/InsectGlaive`. | M2/M3 |
| Imported `AS_Shth_*` sequences | Keep/Defer | Present; wired in M4/M5 only. `Review/Unknown` + `Review/UnusedCandidate` need editor review; never guess-delete by filename. | M4/M5 |
| Missing shells (DA_IG_Combat, DA_IG_InputProfile, DA_WeaponRuntime_IG) | Create | Absent (filename evidence). | M0 |
| Empty GE/Cue/UI folders | Create later | `.gitkeep` only. | M1 (Dodge cue), M2 (hit cues), M3 (extract GE), M6 (UI/cues) |
| IA_Dodge | Create/verify | Absent by name; `IA_A` present. Editor check binding. | M1 |

## 5. 源码级迁移证据

- `FComboNode/ComboTable` 已从源码运行时移除；协调器与装备收集代码只消费 `FComboTransition/Transitions`。旧结构/属性依靠 §6 的 Redirect 载入。
  - `bRequiresHitToGrantTags` 是唯一暂留的序列化兼容字段；`PostLoad` 把 true 迁为 `GrantTiming=OnFirstHit` 后立即清零，运行时决策不读取该旧字段。E0 重存后由 M2 删除。
- C++ 与配置中的精华名已统一为 Orange；Yellow 只存在于两个 GameplayTagRedirect 的 OldTagName 和迁移说明中。`URes_InsectGlaive` 仍保留 `/Game/...GE_IG_OrangeExtract` 等硬编码旧路径，M3 接入 `DA_IG_Combat` 后删除。
- Old falling tags (`IG_DanceJump`, `IG_JumpSlash`, `IG_KinsectSlashHit`, `IG_KinsectSlide`, `IG_PoleVault`) and `Input.Modifier.Aiming`: **no** `Source/` references found. Assets may still carry them (editor-only).

## 6. M0 配置变更

`Config/DefaultEngine.ini`:
- `ECC_GameTraceChannel3` Object Channel `Hitzone` (`DefaultResponse=Ignore`, `bTraceType=False`, `bStaticObject=False`).
- Presets `MonsterBody`, `MonsterHitzone`, `Kinsect`, `PlayerCapsule` per `docs/editor/demo-setup.md` §E1 / plan §3.5.
- `CoreRedirects`: `StructRedirect ComboNode→ComboTransition`; `PropertyRedirect` on `ComboTransition` for `StateName→SourceState`, `BlockedStateNames→BlockedSourceStates`, `NextState→TargetState`, `DirectionalInput→Direction`, `bRequiresWindowOpen→bRequiresComboWindow`; `PropertyRedirect MHGZWeaponComboData.ComboTable→Transitions`.
  - Property redirects are keyed on the **new** struct name because `FProperty::FindRedirectedPropertyName` resolves against the already-struct-redirected type (verified against UE 5.6 `CoreRedirects.cpp`/`Property.cpp`).

`Config/DefaultGameplayTags.ini`:
- 32 tags added (final M0–M7 set: aiming contexts, RedMode, IncomingHit, AdvancingCounterOpen, final aerial falling/dance-source names, LT/RT chords, `Input.Modifier.LT/RT`, Orange resource, `Mark.Active`, `Cost.IG.TripleUp`, `Data.Cost.Stamina` + three `Data.IG.Buff.*`, `Damage.DanceMultiplier`, `GameplayCue.Character.Dodge`).
- 8 stale entries removed (5 old falling names, `Input.Modifier.Aiming`, both `Yellow` entries).
- `GameplayTagRedirects`: `WeaponResource.IG.Extract.Yellow→Orange`, `UI.Aim.Extract.Yellow→Orange`.
- 189 registered tags total; no duplicates (validated).

## 7. E0 verification steps (exact, from `docs/editor/demo-setup.md` §2.1–2.2)

1. Close Unreal Editor; build the full project with the Development Editor configuration; do not rely on Live Coding/Hot Reload for reflection renames.
2. Confirm the refactor code already contains CoreRedirect/StructRedirect/PropertyRedirect for the actual old types.
3. Record the current `Content` worktree and existing user asset modifications; do not overwrite or recreate same-name `.uasset`.
4. First-round migration targets: `BP_PlayerState`, `BP_IG_Character`, Demo PlayerController, Demo GameMode, HUD, `BP_TrainingDummy`; current IG GA BPs incl. `GA_IG_BaDao`, `GA_IG_R_TuCI`; `DA_IG_Combo`, `DA_IG_HuoLongGun`, old Combo/Resource DataTable referencers; IG Montages with AttackCollision/ComboWindow/DodgeWindow/Counter/Movement/MotionWarping notifies.
5. Start UE 5.6; wait for the Asset Registry scan and all Shader/Blueprint skeleton updates; open Output Log and Message Log.
6. Do not click Save All first. Open each asset in the list and record: Missing Class / Unknown Struct / failed pins / missing Parent Class.
7. Verify renamed Combo fields redirect to `Transitions/SourceState/TargetState`; verify AttackAbility old Socket/Collision/cost fields still hold valid values.
8. On any Missing Property/Class: exit the editor and fix redirects or C++ types first; never delete unknown data in a Blueprint and continue saving.

## 8. Migration ownership by milestone

| Milestone | Owns |
|---|---|
| M0 | 文本配置、最终公共 C++ 类型、`FComboTransition/Transitions` 源码迁移、Redirect、Data Validation 与测试；编辑器创建 `DA_IG_Combat`/`DA_IG_InputProfile`/`DA_WeaponRuntime_IG` 壳并重存目标资产。 |
| M1 | 新 FSM 执行事务、InputRouter + IMC 所有权、`GA_Dodge` 重写、RuntimeHost/TagLedger 行为、删除 ASC 输入绑定与旧 float 成本；Dodge Cue。 |
| M2 | Resave AttackAbility/character/training-dummy assets per this mapping; monster Body + three Hitzones + `CounterTestAttack`; delete old Collision/Socket/cost fields, DataManager getter, runtime compat reads; retire `DT_WeaponComboConfig`; hit cues. |
| Later | M3: kinsect Collision Root + Hitzone Sweep, remove Yellow mapping and `GE_IG_YellowExtract` path, extract GE. M4/M5: ground/aerial actions + sequences. M6: Kinsect Attack, powder, UI, cues. M7: packaging and full verification. |

## 9. 编辑器内待确认项

- Tag/field references actually stored inside `GA_IG_BaDao`, `GA_IG_R_TuCI`, `DA_IG_Combo` (e.g., old `Yellow`, old falling names, `FComboNode` fields).
- Montage notify classes and instances inside `AM_Shth_BaDao` / `AM_Shth_R_TuCi`.
- Whether `IA_A` is the dodge action and whether `IA_RTA/RTB/RTY/YB` are referenced anywhere.
- Asset-registry references to `/Game/GameplayEffects/InsectGlaive/GE_IG_YellowExtract` from code-loaded or asset-stored paths.
- HUD/UI asset existence outside `/Game/UI` (no filename evidence of any).
- `DA_IG_HuoLongGun` and `DA_TrainingDummy` row/field contents.
