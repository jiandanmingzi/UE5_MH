# MHW MOD3 + LMT → FBX：本机前置与操作

录制游戏内数据见[World录制器说明](Recorder.md)，脚本为同目录 `MHGZ_MHW_AerialTrajectoryRecorder.lua`。原始录制的游戏输出目录及已安装脚本不因仓库整理改变。

## 本次准备范围和状态

本机 Blender 位于 `F:\games\Steam\steamapps\common\Blender\blender.exe`，实测版本为 **5.2.2 LTS**，Python 3.13.13。

已经安装并启用 [Feuleur 的 MHWorld tool suite 1.3.9](https://github.com/Feuleur/blender-mhworld-tools-suite/releases/tag/1.3.9)，包含 MOD3 和 LMT 导入菜单。Blender 内置 `io_scene_fbx` 已启用。安装目录为：

```text
C:\Users\ROG\AppData\Roaming\Blender Foundation\Blender\5.2\scripts\addons\mhworld_tools_suite
```

官方包下载到 `Saved/MHWBlenderSetup/mhworld-suite-1.3.9-release.zip`；本机计算的 SHA-256 为 `51f004b6f3103b045921b22b1347266b334a9f329667e67a216de570856ecefd`。此哈希用于复核本机下载文件，不代表作者提供的签名。

该版本上游仍使用旧 `Action.fcurves`。本次在安装副本的 `lmt/lmt_loader.py` 增加 Blender 5.2 的 Action slot → layer → keyframe strip → channelbag 兼容路径，保留旧版分支；位置、旋转和缩放曲线都放到同一个 Object 类型 slot。另在 `lmt/ui.py` 增加单文件脚本调用的 filepath 回退。补丁没有重命名骨骼或修改原有坐标转换。

安装前的偏好备份：`Saved/MHWBlenderSetup/userpref-before-mhw-20261002.blend`。启动一个新后台 Blender 进程，已确认 `mhworld_tools_suite` 和 `io_scene_fbx` 从保存后的偏好自动启用。

**先保存当前打开的工作，再重启 Blender**，当前已经运行的窗口才会重新读取安装结果。

2026-10-02 已使用用户提供的真实解包素材完成候选导出和 UE 审核导入。源文件位于 `D:\study\MHW\chunk`；源 FBX 保存在 `Saved/MHW_AerialDive_Export_20261002` 与 `Saved/MHW_AerialDive_FullBody_Export_20261002`。2026-10-03 按清理要求，两处导出目录已清理为仅保留 FBX；`.blend` 与 JSON 清单已送入 Windows 回收站，可恢复。早期 `Review/Unknown` 下的临时导入也已清理；源解包内容未修改。

2026-10-02 用户补齐并提取 `pl001_0000` 的 `arm/body/helm/leg/wst` 五个 MOD3 部位后，已完成骨骼覆盖审计和全身审核模型组装。五个部位共提供 93 个不同 remap ID；WP10 LMT 候选 slot 380/381/360 各自的 68 条非根轨道均被覆盖，且 `f_body001_0000.mod3` 单独已经包含这 68 个动作轨道 ID。五部位共 13 个网格对象重绑前后的 Blender 世界顶点最大偏差约 `1.27e-7`，通过静止姿势几何保持检查。

全身审核 rig 的 FBX 位于 `Saved/MHW_AerialDive_FullBody_Export_20261002`。FBX 以 body 骨架作主层级，将在部位文件中缺失或 rest matrix 不同的少数骨骼保留为部位专用 alias；这是审核 rig，不是可直接用于玩法的最终 MHW 骨架。对应 `.blend` 场景已随 2026-10-03 清理送入回收站。

**曾经导入的全身审核资产（已于 2026-10-03 清理）**位于 `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive`，包括：

- SkeletalMesh：`SK_MHW_IG_AerialDive_ReviewRig`
- 独立 Skeleton：`SK_MHW_IG_AerialDive_ReviewRig_Skeleton`
- PhysicsAsset：`SK_MHW_IG_AerialDive_ReviewRig_PhysicsAsset`
- AnimSequence：`AS_MHW_IG_AerialDive_LMT380_Review`、`AS_MHW_IG_AerialDive_LMT381_Review`、`AS_MHW_IG_AerialDive_LMT360_Review`

这些 AnimSequence 曾绑定该 Skeleton，Preview Mesh 指向新全身网格。2026-10-03 在完成目标骨架重定向后，审核 Mesh/Skeleton/PhysicsAsset 和这三条源 AnimSequence 均通过 UE 资产 API 删除；当时再次确认它们没有目录外引用。玩法资产未修改。

2026-10-02 曾建立 MHW 审核骨架到本项目 `SK_Demo_Body` 的 UE IK Retargeter 配置（Source/DemoIG 两个 IK Rig 和一个 IK Retargeter）；该配置以及旧重定向 AnimSequence 已于 2026-10-03 清理，没有连接过 GA、Combo、伤害或玩法资源。

2026-10-03 完成手指与武器挂点修复后，最终两条 AnimSequence 已从临时审核目录提升到统一虫棍导入目录，并按项目 `AS_Unsh_<招式>` 命名习惯规范为：

- `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_LMT380`：`1.15s`，60 Hz，70 个采样关键帧。
- `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_LMT381`：`2.6833s`，60 Hz，162 个采样关键帧。

保留 `LMT380/381` 来源序号，是因为当前证据尚未证明两个动作片段各自对应的招式阶段，不把它们擅自命名为起手、循环或收尾。两条均绑定 `SK_Demo_Body`，播放速率 1.0；目标预览角色是 `SKM_Demo_Body`。十条手指链及 Root/身体 RAW/压缩轨道已通过数值核验；武器挂点与本项目真实 Socket 的数值关系见下节。编辑器目视握持仍需最终确认；这两条是最终保留的移植动画资产，但尚未接入 GA、Combo 或玩法。

最终动画资产位于 `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported`。本次已通过 UE 资产 API 清理 `Imported/Review/MHW/AerialDive` 下的 11 个中间资产包（旧导入序列、旧重定向结果、IK Rig/Retargeter、审核 SkeletalMesh/PhysicsAsset/Skeleton）；删除前确认没有目录外引用。380/381 FingerFixed 动画已移出该审核目录并保留。两处 `Saved/MHW_AerialDive_*_Export_20261002` 现在仅保留 FBX；`.blend` 和 JSON 清单已送入回收站。`Saved/AssetProbe` 的验证报告与 `Saved/Backups` 的历史备份仍在，是否进一步清理待用户确认。

### 380/381 Root 偏离修复与再生成合同

初次默认 IK 重定向只核验 Skeleton、时长和缩放，未检查身体位移，不能算动作验收。源参考姿势的 `bone_013` 组件空间高度为 0；目标 `Waist_00` 高度为约 `-6.365cm`。UE 5.6 默认 Pelvis Motion 将近地面的源高度改为 1 再计算比例，导致身体位置乘上约 `-6.365`，380 腰部组件空间 Z 因此达到约 `-2859.6cm`。默认 Root Motion 又选择了额外的静止 `Armature_MHW_body001_0000`，而非真正运动的 `bone_root`；381 的正常根位移被错误留在、反向放大到腰部轨道。

当前修复不篡改 Limb FK 姿势，也不把真实位移清零，固定使用以下最终后处理：

1. `Root` 平移 = 目标参考 Root 平移 + 源 `bone_root` **组件空间位移相对第一帧的增量**。移除动作起始坐标偏置，保留每帧真实位移；目标 Root 保持本项目原参考朝向，不额外纠正骨骼轴。
2. `Cog` 的局部平移/旋转来自源 `bone_000`，`Waist_00` 的局部平移/旋转来自源 `bone_013`；`R_Foot_IK` / `L_Foot_IK` 分别来自源 `bone_249` / `bone_250`，`LookAt` 来自源 `bone_252`，`PL_Cam` 和 `Parts` 来自源 Root 原点轨道 `bone_253`。这些源骨骼与目标辅助骨骼都直接挂在各自 Root 下，且 Root 朝向相同，局部轨道可对应传递。身体不再依赖错误的腰部高度比例。Root、Cog、Waist、根级辅助骨骼和两根武器骨骼缩放强制为 1。
3. 保留现有主关节 FK 与新增手指 FK 轨道，保存到新的 `Retargeted_FingerFixed` 审核资产；不改 Skeleton 的参考姿势。380 的源 Root 无逐帧位移，因此目标 Root 静止；381 的目标 Root 仍前移约 `424.24cm`。
4. **IK Retargeter 直接导出的结果仅是中间结果，不是可签收的最终动画。** 现有 Retargeter 默认操作仍不适合源参考高度；任何再导出都必须执行 `repair_mhw_aerial_dive_root_motion.py`。`retarget_mhw_aerial_dive_sequences.py` 强制执行 Root/Cog/Waist 后处理、保存后的 RAW/COMPRESSED 验证和手指逐帧审计，不能只改 Preview Mesh/速率就宣布通过。初次批量导出仅支持完整编辑器，禁止在 commandlet 中调用会访问 Slate 的批量接口；已有输出的后处理可以通过 commandlet 执行。

`Saved/AssetProbe/mhw_aerial_dive_fingerfixed_root_repair.json` 记录修复与备份；`mhw_aerial_dive_fingerfixed_root_verification.json` 记录保存后 RAW/COMPRESSED 评估结果；`mhw_aerial_dive_finger_retarget_audit.json` 记录源与目标手指各链逐帧旋转范围及目标首帧相对参考姿势的偏差。Root、身体和手指数据均通过数值检查；这不代替网格目视签收。

**当前尚待编辑器目视验证：**在 `SKM_Demo_Body` 上播放上方两条保留动画，检查手指握持、Root/身体相对位置、381 原有前移、武器贴掌位置/朝向及整段动作连续性。武器挂点已按目标网格实际 Socket 层级修复并通过局部变换数值检查（见下节）；数值匹配不替代编辑器里的最终视觉确认。审核前不接入玩法。

### FingerFixed 武器挂点对齐（2026-10-02）

武器位置问题不是简单地把源骨骼 `bone_030` 和 `bone_047` 都按名称/编号复制到目标造成的。先检查 `SKM_Demo_Body` 的真实 Socket 配置：左侧 `Weapon_L` Socket 的父骨骼为 `L_Weapon_00`，Socket 局部平移为零、局部旋转 Roll 为 -90°、缩放为 1；右侧 `Weapon_R` 则挂在 `R_Weapon_00`。因此本项目当前武器的实际左手挂点应以 `L_Weapon_00` 为准，而不能仅凭“武器骨骼”命名推断两侧都应照搬源轨道。

将 LMT 381 末帧与未收刀虫棍 Idle `AS_UnSh_Idle_IP`（并交叉检查 `/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Idle`）逐骨骼比较后，确认：

- `L_Weapon_00` 的 381 末帧局部变换与 Idle 最近姿势的差异仅约 `0.016 cm / 0.025°`。IK Retargeter 原输出没有保留此处所需的源 `bone_030` 平移；FingerFixed 修复版现将源 `bone_030` 的局部平移和旋转传给目标 `L_Weapon_00`，缩放固定为 1。
- 该项目标局部平移误差为 0，局部旋转误差小于 `0.00004°`。组件空间仍可因手臂/整段姿势与 Idle 不完全相同而有约 `6.8 cm / 1.3°` 的差异；数值结果只证明挂点局部姿势匹配，不等同于已经通过编辑器目视验收。
- 源 `bone_047` 在末帧具有显著动作偏移，与 Idle 的 `R_Weapon_00` 局部姿势差异约 `35.3 cm / 39.5°`；将它复制过去反而破坏右侧姿势。因此目标 `R_Weapon_00` 保留 IK Retargeter 的原轨道，不从源 `bone_047` 覆盖。

修复与只读审计脚本：`Scripts/AssetProbe/repair_mhw_aerial_dive_root_motion.py`、`Scripts/AssetProbe/verify_mhw_aerial_dive_root_motion.py`、`Scripts/AssetProbe/audit_mhw_aerial_dive_weapon_pose.py`。结果报告为 `Saved/AssetProbe/mhw_aerial_dive_fingerfixed_root_repair.json` 和 `Saved/AssetProbe/mhw_aerial_dive_fingerfixed_root_verification.json`。380/381 RAW 与压缩轨道数值验证通过；最终还需在 UE 中打开 `AS_Unsh_JiXiTuCi_LMT381` 检查武器是否贴合手掌、方向正确且动画过程中无滑动。临时审核用 UE 资产已清理，源 FBX 保留；两条动画尚未接入玩法。

本次候选为：运行时 `49532` 对应候选 `w10_00` slot 380；运行时 `49533` 对应候选 slot 381，以及备选 slot 360。slot 380 的末关键帧为 69；slot 381/360 的末关键帧为 161，381 有 root 位移而 360 没有。**运行时 `action_lmt_id` 49532/49533 尚未被证明能直接映射到这些 LMT slot**；候选是按末帧对应与相邻 slot 关系筛出的，必须目视验证，不能据此直接接入玩法。

## 已完成的验证与尚缺的材料

`Saved/MHWBlenderSetup/smoke-5.2-guarded/verification.json` 为最终验证报告，使用已安装的插件。人工测试骨架经过插件的模型构建路径、LMT Action 构建路径、FBX 导出及回读；3 根骨骼、层级和动画绑定正确，根位移和蒙皮运动的回读误差均小于 `0.000001` Blender 单位。

验证脚本只允许在独立的 `--background --factory-startup` 进程中运行，还会拒绝已经加载 `.blend` 文件的进程；在任何清场或文件创建前检查这些条件。不带 `--factory-startup` 的拒绝路径已验证，日志为 `Saved/MHWBlenderSetup/verify-guard-rejected.log`，该次运行没有创建输出目录。

这是**人工 parser-output 合成数据**的历史管线测试，不是真实游戏动作证据。真实源文件现已由本地插件解析、导出并导入；FBX 回读可见动画 Action，UE 导入报告验证了三个 AnimSequence 的 Skeleton 绑定和长度。真实候选动作与运行时 ID 的语义映射仍未验证。

本次使用到的实际文件为：人物骨架候选 `pl/f_equip/pl001_0000/body/mod/f_body001_0000.mod3`，虫棍动作表 `hm/wp/wp10/mot/w10_00/w10_00.lmt` 及其 `.mbd`，另有公共动作表 `hm/common/mot/co00_00/co00_00.lmt` 及其 `.mbd`。当前候选导出只用了人物 MOD3 与 WP10 LMT；公共动作表尚未导出。

## 1. 获得解包后的源文件

优先使用作者的 [MHWNoChunk](https://github.com/JodoZT/MHWNoChunk) 提取所需文件。它支持合并 chunk，从多个更新包中取最新版本；其前置为 .NET Framework 4.7.2 或更高版本，以及 `oo2core_8_win64.dll`。本机已有 .NET Framework 4.8。

**本次所需素材已就绪；通用解包工具链仍未完成。** 用户已提供解包后的文件夹 `D:\study\MHW\chunk`，因此本次不需要 MHWNoChunk 或 Oodle 依赖。若将来要从原始 chunk 提取更多文件，仍可按下列流程准备可信的 MHWNoChunk 与匹配依赖；不要把“当前样本可用”误写成“通用解包工具已安装”。

若之后官方下载恢复，将解包工具放到 `Saved/MHWBlenderSetup/tools/MHWNoChunk`；Oodle 必须采用该工具支持的官方依赖，不能把其他版本 DLL 改名替代，也不要下载来历不明的 DLL。`prepare_mhw_oodle.py` 可解压作者使用的 LZMA 依赖包，并检查作者源码指定的 MD5 `3169b48a9a2086e53c4493c03579902c`；它不负责下载，也没有因本次下载失败而生成 DLL。该 MD5 是版本匹配检查，不替代来源可信性检查。

工具和依赖到位后，提取时保留原有相对目录结构：

1. 打开 MHWNoChunk，启用 **Combine all chunks**。
2. 拖入 `F:\games\Steam\steamapps\common\Monster Hunter World\chunk\chunkG0.bin`。
3. 查找人物模型的 `.mod3` 和对应武器动作目录内的 `.lmt`；模型文件和动画常分别在 `mod`、`mot` 目录中。
4. 勾选要提取的文件，点击 **Extract Selected**。建议输出到 `D:\study\MH\MHGZ\Saved\MHWExtracted`。
5. 若后续需要材质，再取同路径的 `.mrl3` 和被引用的 `.tex`。单纯骨架、动画和 FBX 导出可以先不取材质。

`Saved` 下的源文件和工具属于本机准备数据，清理 Saved 前需另行保留需要的内容。不要把游戏安装根目录直接当作“解包根目录”；归档尚未展开时，插件不能通过它寻找引用的材料。

## 2. 导入 MOD3 骨架

1. 重启 Blender 后，打开 **Edit → Preferences → Add-ons**，搜索 `MHWorld tool suite`，应已启用。
2. 使用空场景，进入 **File → Import → Monster Hunter World → Model files (.mod3)**。
3. 首个动画验证样本使用以下设置：

| 导入项 | 设置 | 用途 |
| --- | --- | --- |
| LoD | 0 | 读取最高细节模型 |
| Fix rotation | 开启 | 使用插件原有的对象层 90° 坐标转换 |
| Fix scale | 开启 | 使用插件原有的对象层 0.01 缩放 |
| Rename bones | 关闭 | 保持 LMT 使用的数字骨骼映射 |
| Connect bones | 关闭 | 保持原始静止姿势，连接骨骼会改变它 |
| Import material | 关闭 | 先完成骨架/动画验证 |
| Import VM geonode | 关闭 | 首个动画样本不需要材质变形节点 |

4. 导入后应有 Armature 和模型，骨骼包含 `bone_root` 及 `bone_###`。保留导入时的对象旋转和缩放；先不要对骨架执行 Apply Transforms。

如果要导入材质，在插件偏好中将 **Unpacked game path** 指向含原目录树的解包根目录，再启用 Import material。

## 3. 导入 LMT，并选择一个动作

1. 切回 Object Mode，选中且只选中目标 Armature。
2. 进入 **File → Import → Monster Hunter World → Animation files (.lmt)**。
3. 开启 **Keep animations on file save**，以便未使用的 Action 也能保留在 `.blend` 文件中。
4. 导入后的动作位于 `bpy.data.actions`，名称形如 `LMT文件名_000`；尾部数字是该 LMT 的条目索引。
5. 把一个窗口切换为 **Dope Sheet → Action Editor**，保持 Armature 选中，在顶部 Action 下拉框中选择目标动作。Blender 5.2 如显示 Action Slot，使用该动作唯一的 **MHW Armature** slot。
6. 在时间轴设置该动作的 Start/End 帧。先按 Action 实际关键帧范围检查；首个真实样本还要对照原游戏动作时长，确认有没有末尾保持段未被解析。
7. 在 Output Properties → Frame Rate 设置导出采样帧率。准备验证可先用 60 fps，但当前插件不自动设置帧率；真实文件必须用动作关键帧跨度和原游戏记录时长确认，不能只凭渲染 FPS 判断动作时间基准，也不要预先给所有动作加 2× 速率。
8. 拖动时间轴确认关节有运动，检查 `bone_root` 是否有原始位移。游戏运行时曲线驱动的额外位移未必存在于 LMT，不能用 FBX 没有那部分位移来推断转换失败。

导入器会在控制台输出 `UNSUPPORTED ENCODING` 等诊断；如出现该信息，或部分骨骼无动作，应先核对 LMT 与骨架是否匹配及编码支持，不能将缺轨动作当作完成资源。Windows 的控制台可通过 **Window → Toggle System Console** 打开。

## 4. 导出单个动作的 FBX

本次已将 `mhw_animation_current_action.py` 操作预设安装到 `C:\Users\ROG\AppData\Roaming\Blender Foundation\Blender\5.2\scripts\presets\operator\export_scene.fbx`。重启后，可在 FBX 导出窗口顶部的 Operator Presets 中选择 **Mhw Animation Current Action**。

1. 给 Armature 指定要导出的 Action，确认 Slot、Start/End 和 Frame Rate。
2. 只选中该 Armature；如果需要模型同时导出，再选择对应的 Mesh。
3. 进入 **File → Export → FBX (.fbx)**，选择上述预设。
4. 预设启用 Selected Objects，只导出 Armature/Mesh；Forward 为 `-Y`、Up 为 `Z`；关闭 Add Leaf Bones 和 Only Deform Bones，保留 `bone_root`。
5. Bake Animation 开启；NLA Strips、All Actions 关闭；Sampling Rate 为 1，Simplify 为 0。这样一个 FBX 只包含当前 Action，按每帧采样。
6. 通用手动流程可输出到 `Saved/MHWFBX`。本次自动化候选输出位于 `Saved/MHW_AerialDive_Export_20261002`，包括参考网格 FBX、3 个动画 FBX、`candidate_manifest.json` 和复查用 `.blend`。
7. 首个 FBX 在空场景中回读验证骨骼层级、首末姿势、根位移和时长。回读到 Blender 对比时，FBX Import 的 Animation Offset 设为 **0**，避免默认偏移一帧造成假误差。

MHW 导出的骨骼名称与本项目现有角色骨架不同；本次已用独立 IK Rig/IK Retargeter 生成指向本项目目标 Skeleton 的审核候选。候选仍待编辑器目视签收，之后才讨论正式动作导入/制作与玩法接入。

## 本次脚本

- `probe_blender_mhw.py`：后台读取 Blender 版本、Action API、已启用插件与 FBX 能力，不保存场景或偏好。
- `enable_blender_mhw.py`：后台启用已安装插件和 FBX，并保存用户偏好；安装前已备份偏好。
- `verify_blender_mhw.py`：人工测试数据的模型/动作构建与 FBX 回读验证，只允许独立后台工厂场景，输出目录必须不存在。
- `mhw_animation_current_action.py`：FBX 导出窗口中的操作预设，不应直接在普通 Text Editor 上下文运行。
- `export_aerial_dive_candidates.py`：在隔离 Blender 后台中加载指定 MOD3/LMT，输出 49532/49533 候选 FBX；拒绝覆盖已有输出目录。
- `import_aerial_dive_candidates.py`：通过 UE 5.6 Python commandlet 将候选 FBX 导入独立审核目录，复用参考 Skeleton 并跳过已有动画，禁止覆盖。
- `audit_mhw_rig_coverage.py`：只读比较多个 MOD3 的 remap ID、父级、rest 矩阵和候选 LMT 轨道覆盖；可选执行 Blender importer bind-pose 对照。
- `export_mhw_fullbody_candidates.py`：将五个部位 MOD3 组装为独立全身审核 rig，先验证重绑不改变 rest-pose 网格，再导出 FBX 与候选动作；拒绝覆盖已有输出目录。
- `import_mhw_fullbody_candidates.py`、`import_mhw_fullbody_on_existing_skeleton.py`、`repair_mhw_fullbody_review_assets.py`：早期排错脚本，仅保留作历史记录；不要再运行。
- `reimport_mhw_aerial_dive_review.py`：在新目录创建独立 Skeleton/PhysicsAsset/全身网格和三条规范命名 AnimSequence；验证新资产与引用后，精确删除本次生成的旧审核包，保留所有 FBX、Blend 和源文件。
- `audit_mhw_fullbody_ue_assets.py`：只读核验新 SkeletalMesh、独立 Skeleton、PhysicsAsset、三条动画的 Skeleton 绑定和 Preview Mesh。
- `inspect_mhw_lmt_action_motion.py`：只读列出候选 LMT 动作各骨骼轨道是否实际变化，用于区分源动作缺轨与 UE 预览网格选择错误。
- `inspect_exported_fbx_scene.py`：在 Blender 中只读回读检查 FBX 的 armature、骨骼、网格权重组与 Armature Modifier。
- `Scripts/AssetProbe/create_mhw_aerial_dive_retarget_setup.py`：创建本次独立 IK Rig 与 IK Retargeter 审核配置。
- `Scripts/AssetProbe/retarget_mhw_aerial_dive_sequences.py`：在完整编辑器内生成 `Retargeted_FingerFixed` 单位缩放候选；强制执行 Root/Cog/Waist 后处理、RAW/COMPRESSED 验证和手指审计。
- `Scripts/AssetProbe/audit_mhw_aerial_dive_finger_retarget.py`：只读审计 380/381 源动画与目标动画的 10 条手指链逐帧旋转及参考姿势偏差。
- `Scripts/AssetProbe/verify_mhw_aerial_dive_fingerfixed_disk.py`：独立 commandlet 重载并验证已保存的 FingerFixed 结果。
- `Scripts/AssetProbe/repair_mhw_aerial_dive_root_motion.py`：备份、修复两条候选的祖先位移轨道和单位缩放，核验源/目标 Root 相对姿势，再精确删除无引用的 PreserveWeaponScale 对照包。
- `Scripts/AssetProbe/verify_mhw_aerial_dive_root_motion.py`：只读重载检查 RAW/COMPRESSED 位移、Root 相对身体位置、缩放、Skeleton、时长/速率及已退役资产不存在。
- `Scripts/AssetProbe/audit_mhw_aerial_dive_root_motion.py`：只读逐帧导出源/目标 Root、Cog、腰部、头和脚的局部/组件变换，供排查层级和位移问题。
- `Scripts/AssetProbe/create_mhw_aerial_dive_scale_variants.py`：**已退役，执行会报错拒绝**；仅保留历史实现，不再生成非单位缩放版。
- `Scripts/AssetProbe/inspect_mhw_aerial_dive_retarget_outputs.py`：只读核验目标 Skeleton、动画时长/采样帧数及武器缩放范围。
- `prepare_mhw_oodle.py`：校验并解压来自作者官方依赖地址的 Oodle LZMA 包；当前没有可用下载，尚未生成 DLL。
