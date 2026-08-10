# M0 资产迁移审计——MHGZ 虫棍 Demo

> 状态：以分阶段实施前基线为起点的证据清单。2026-08-11 已补做 UE 5.6 Asset Registry 与加载查询，并据此把旧虫棍动作原型从“迁移/重存”改为“删除重建”；本轮只修改文档，不删除 `.uasset`。

## 1. Baseline and build status

- Baseline commit: `870c39bccb0dfbefe492041d6fd250a633d9103e` — "chore: 保存分阶段实现前基线" (verified as HEAD on 2026-08-10).
- Baseline build: **succeeded** — `MHGZEditor Win64 Development`, 2026-08-10.
- M0 code build: **succeeded**；`MHGZ.M0` 5 个自动化测试全部通过（含 TagLedger 重叠所有者回收）。
- M0 全资产 Data Validation 能加载旧 `DA_IG_Combo`，无 Missing Struct/Property；它仅稳定报告旧唯一节点缺少 `TransitionID` 与 `TargetState`。这些错误现在作为“旧包确实不完整”的审计证据，不再要求 E0 补录。
- M0 代码阶段没有修改任何 `.uasset`。E0 只体检保留资产；旧原型等 M2 解除运行时引用后由 E3/E4 删除重建。

## 2. Evidence conventions

- **Filename/string evidence**: verified from the git tree, the `Content/` file listing, and read-only `rg` over `Source/`/binary package strings.
- **UE evidence**: UE 5.6 `AssetRegistry.get_dependencies/get_referencers`、加载后 `find_package_referencers_for_asset` 与资产类/父类查询；报告位于 gitignored 的 `Saved/E0RefQuery/e0_ref_query_report.json`。
- Reference Viewer 仍作为实际删除前的最后一道人工门禁；任何额外引用都阻止删除，禁止 Force Delete。

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

### 3.3 2026-08-11 UE 5.6 reference audit

| Legacy package | Size | Verified referencers | Decision evidence |
|---|---:|---|---|
| `DT_WeaponComboConfig` | 2,347 B | Config `WeaponComboConfig`; no asset referencer | One `IG` row; only soft-depends on old Combo |
| `DA_IG_Combo` | 3,177 B | `DT_WeaponComboConfig` (soft) | One incomplete Y→`GA_IG_R_TuCI` transition |
| `GA_IG_R_TuCI` | 15,967 B | `DA_IG_Combo` | Only depends on its old Montage and native classes |
| `AM_Shth_R_TuCi` | 8,786 B | `GA_IG_R_TuCI` | No custom MHGZ Notify found; keeps `AS_Shth_R_TuCi` dependency |
| `GA_IG_BaDao` | 15,953 B | none | Unreachable prototype branch |
| `AM_Shth_BaDao` | 8,789 B | `GA_IG_BaDao` | No custom MHGZ Notify found; keeps `AS_Shth_BaDao` dependency |
| `DA_IG_HuoLongGun` | 2,847 B | none | Only soft-depends on `SKM_IG_Glaive`; final fields are fully documented |
| `IA_RTA/IA_RTB/IA_RTY/IA_YB` | 1,096–1,100 B each | `BP_PlayerState`, `IMC_MHGZ_Demo` | Old precomposed chord actions; remove both referencers before E3 deletion |

Core references independently justify keeping the remaining E0 assets: `DefaultEngine.ini` and `L_DemoArena` reference `BP_Demo_GameMode`; GameMode references Character/PlayerState/Controller; the map contains `BP_TrainingDummy`; `ABP_MH_Character` and imported animations carry the actual locomotion/action data.

## 4. Keep / Rewrite / Delete / Defer

| Target | Verdict | Evidence / required adjustment | Owner |
|---|---|---|---|
| BP_PlayerState (ASC/AttributeSet/Equipment identity) | Keep | Present. Refactor-scope "GAS 身份": remove ASC input binding and pawn-state-init authority (M1). | M1 |
| BP_IG_Character | Keep | Present. E2.3 keeps the default weapon unset while the old definition exists; E3 reconnects the newly created final `DA_IG_HuoLongGun`. | M1/M2/E3 |
| BP_MHGZ_PlayerController | Keep | Present. E2.2: clear `DefaultMappingContexts`; single `IMC_MHGZ_Demo` owned by `UMHGZInputComponent`. | M1 |
| BP_Demo_GameMode | Keep | Present. E2.4: final class wiring (Pawn/Controller/PlayerState/HUD). | M2 |
| ABP_MH_Character + PSD/PSS | Keep | Present. Refactor-scope "地面移动": motion-matching locomotion; rotation/movement tokens yield ownership (M5). | M1/M5 |
| GA_IG_BaDao, GA_IG_R_TuCI | Delete; create final actions | `BaDao` 无引用者；`R_TuCI` 只被旧最小 Combo 引用。两者不 re-parent、不复制旧图，E4 按最终动作清单新建。 | E3 delete / E4 create |
| AM_Shth_BaDao, AM_Shth_R_TuCi | Delete; create final montages | 各自只被对应旧 GA 引用；UE/二进制审计未发现自定义 Notify，只有原始 AnimSequence 与 Skeleton 依赖。保留 `AS_Shth_*` 与 Skeleton。 | E3 delete / E4 create |
| DA_IG_Combo | Delete + recreate same final path | 只有一个 Y→`GA_IG_R_TuCI` 最小节点且验证不通过；不补录、不转存。E3 新建空壳，E4 一次回填完整 Transitions。 | E3/E4 |
| DA_IG_HuoLongGun | Delete + recreate same final path | UE Asset Registry 证明零引用，只软引用 `SKM_IG_Glaive`；E3 从最终 `UMHGZWeaponDefinition` 新建，按文档填写正式字段。 | E3 |
| DT_WeaponComboConfig | Delete, no replacement | 仅一行 `IG`，软引用旧 Combo；`DefaultGame.ini` 与 Equipment 旧读取必须先在 M2 解除。 | M2 code / E3 asset |
| IA_* + IMC_MHGZ_Demo | Keep base; delete old chord IA | M1 删除代码读取，E2 清 PlayerState，E3 清 IMC 后删除 `IA_RTA/RTB/RTY/YB`；Y/B/LT/RT 与移动/视角资产保留。`IA_A` 暂留到 E3 判定闪避绑定。 | M1/E2/E3 |
| DA_TrainingDummy + BP_TrainingDummy | Keep concept | Present. E5/E7.3: `MonsterBody` + three `MonsterHitzone` (Head=Red, Torso=Orange, Leg=White), deterministic `CounterTestAttack`. | M2 |
| L_DemoArena | Keep | Present; single demo map. | M2/E5 |
| IG Glaive/Kinsect art | Keep | Present under `/Game/Weapons/InsectGlaive`. | M2/M3 |
| Imported `AS_Shth_*` sequences | Keep/Defer | Present; wired in M4/M5 only. `Review/Unknown` + `Review/UnusedCandidate` need editor review; never guess-delete by filename. | M4/M5 |
| Missing shells (DA_IG_Combat, DA_IG_InputProfile, DA_WeaponRuntime_IG) | Create | Absent；最终 `.uasset` 在 E3 创建，M0 只完成 C++ 类型。 | E3 |
| Empty GE/Cue/UI folders | Create later | `.gitkeep` only. | M1 (Dodge cue), M2 (hit cues), M3 (extract GE), M6 (UI/cues) |
| IA_Dodge | Create/verify | Absent by name; `IA_A` present. Editor check binding. | M1 |

## 5. 源码级迁移证据

- `FComboNode/ComboTable` 已从源码运行时移除；协调器与装备收集代码只消费 `FComboTransition/Transitions`。旧结构/属性依靠 §6 的 Redirect 完成过只读加载审计。
  - `bRequiresHitToGrantTags` 是唯一暂留的序列化兼容字段；最终 Combo 不从旧包迁移。M2 停止读取兼容语义，E3 删除旧包，M4 移除此字段与 `PostLoad` 兼容。
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

## 7. E0 verification steps (exact, from `docs/editor/demo-setup.md` §2.1–2.3)

1. Close Unreal Editor; build Development Editor; record clean/dirty `Content` state and confirm the Git stage commit exists.
2. Start UE 5.6 and wait for Asset Registry discovery. Shader completion is only a noise/performance concern; Blueprint skeletons rebuild per loaded target.
3. Open only keep assets: PlayerState、Character、Controller、GameMode、TrainingDummy、AnimBP/PSD/PSS、map and base input assets. Do not Save All first.
4. Record Missing Class/Property、Unknown Struct、failed pins、missing parent and broken default-class/component references.
5. Do not fill or resave the old Combo/GA/Montage/weapon definition; E0 stops here.
6. Actual deletion waits for M2 to remove the old ini/DataManager/Equipment chain, then E3 uses Reference Viewer and the exact §4 list. Any unexpected referencer blocks deletion; never Force Delete.

## 8. Migration ownership by milestone

| Milestone | Owns |
|---|---|
| M0 | 文本配置、最终公共 C++ 类型、`FComboTransition/Transitions` 源码更名、审计用 Redirect、Data Validation、测试与资产引用审计；E0 只体检保留资产。 |
| M1 | 新 FSM 执行事务、InputRouter + IMC 所有权、`GA_Dodge` 重写、RuntimeHost/TagLedger 行为、删除 ASC 输入绑定与旧 float 成本；Dodge Cue。 |
| M2 | 接线保留的 Character/TrainingDummy；monster Body + three Hitzones + `CounterTestAttack`；删除 DefaultGame.ini 旧表项、DataManager getter、Equipment 与 Attack 兼容运行时读取；序列化壳保留到 E3 删除旧包，M4 再移除。 |
| Later | M3: kinsect Collision Root + Hitzone Sweep, remove Yellow mapping and `GE_IG_YellowExtract` path, extract GE. M4/M5: ground/aerial actions + sequences. M6: Kinsect Attack, powder, UI, cues. M7: packaging and full verification. |

## 9. 编辑器内待确认项

- 删除前用 Reference Viewer 复核 §4 的引用链没有在本次审计后新增引用；不再检查旧 GA/Combo 字段值，因为不迁移。
- `IA_A` 是否复用为闪避；`IA_RTA/RTB/RTY/YB` 的已知 PlayerState 引用是否在 E2 归零、IMC 引用是否在 E3 归零。
- Asset-registry references to `/Game/GameplayEffects/InsectGlaive/GE_IG_YellowExtract` from code-loaded or asset-stored paths.
- HUD/UI asset existence outside `/Game/UI` (no filename evidence of any).
- `DA_TrainingDummy` 的字段内容与 `BP_TrainingDummy.DummyConfig` 尚未接线；该资产保留并在 E5 填写，不属于删除清单。
