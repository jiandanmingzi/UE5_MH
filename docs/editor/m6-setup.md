# E6：觉虫击、虫印/虫粉、粉尘聚爆、HUD与视觉反馈编辑器指导（不含音效）

> **2026-10-24配置归属及兼容清理更新：** 聚爆专属的数量档动作值、吸引/爆炸半径、到达后延迟、Cue和专属声音均在`GA_IG_PowderVortex → 类默认值 → Powder Vortex`；第一段按猎虫攻击力×档位MV/100。单团粉尘MV、普通产粉CD和共享生命周期仍在`DA_IG_Combat`。其余单招参数也已迁入对应GA；Combat中的66个兼容/重复字段已删除并重存，包含旧聚爆、猎虫滑翔、旧降龙及旧虫印斑块等。不要重放下方历史搭建Apply；当前入口见[配置归属](../design/insect-glaive.md#当前配置归属2026-10-24覆盖下文历史combat招式参数说明)及维护状态。

> **当前入口（2026-10-13）：** 用户确认五项收尾第1项（近期修复/E6.3真实PIE）与第2项（普通猎虫采集/回手交付）已完成。当前只实施§4的E6.4伤害数字；其他视觉Cue及音效延期且不阻塞本Demo。较早“E6.3待PIE、E6.4不可进入、普通回手欠项”均为历史记录。数字需本包静态检查与短PIE确认，第4/5项综合总验收及发布验证尚未完成。

> **当前入口（2026-10-12统一落地）：** 所有普通空中落地统一既有`AM_IG_AerialLanding`，觉虫击低空冲刺不再用专用落地资产/覆盖参数，见§3.15。§3.14连续地面目标小抛弧保持，原作距离公式仍未确认，观感待PIE。§3.13角度补丁/裁段撤回。通用落地和主Montage、通知、相机均不重新生成或裁剪。

> **最新运动追加（2026-10-12）：** 觉虫击199的贴地立即结束已按最新MHR录制修改，见§3.12。无需重摆通知或重生成资产，重开编辑器载入新DLL后复验。E6.3仍待PIE，不覆盖§3.11的其余UI/命中复验。

> **当前入口（2026-10-12第二次录制）：** 以§3.11为准：放虫真实接触、贴地新指令接管、操虫斩携带/回手、觉虫击消费所有灯及椭圆简化已实施；补最后采样但觉虫击视觉漏判仍须具体复验。相机、通知、判定半径和速度曲线不改。下方§3.10/39项结果是前一轮历史证据；E6.3仍未签收，不进入E6.4。

> **最新进度（2026-10-12，追加修复已实施，待PIE签收）：** 初始化/准心绘制修复保留；已修三灯后图标全灭、臂上放虫平行偏靶及觉虫击起点视差，携带图标改为竖立椭圆。Development Editor编译成功，本轮定向回归39/39（其中UI 6/6，含真实WBP GPU绘制）通过，证据与复验见§3.10。五张《世界》原图永久保存在§3.0。相机位置/管理、快照采样时机与单精华机制不变；改变的是飞行请求对冻结目标的使用。无需重新生成或接线Widget，E6.3仍待PIE。

> **2026-10-12 当前状态：** E6.1/E6.2历史签收保留；E6.3追加修复39/39定向回归通过，待按§3.10复验。虫印约1m立体云团、覆盖100×100cm；虫粉半径75cm，普通爆炸半径150cm，聚爆半径300cm。完整M6此前两项非UI失败保留，本轮未重跑整组，不将跨阶段39/39写成M6全绿；见[M6计划§12](../archive/demo-2026-10-13/m6-plan.md)。
>
> 本轮不获取、创建、配置或验收音效。现有 `DA_IG_Combat` Sound 字段保留，不能批量清空。阶段状态见[门禁表](../design/milestone-gates.md)。

## 0. 通用操作约定

1. 本文 Content 路径采用 `/Game/...`，即编辑器内容浏览器中的项目 Content；`Source/...` 是工作区C++文件，不是资源目录。
2. 觉虫击资产由 `MHGZM6AssetSetupCommandlet` 管理；聚爆资产由独立`MHGZM63PowderVortexAssetSetupCommandlet`管理。M6.3.6已完成新合同资产与`DA_IG_Combat`接线并通过AuditOnly；本阶段只剩PIE。不要重跑可能覆盖用户调参的`-Apply`，也不要用M6.2旧生成器重建聚爆资产或覆盖觉虫击196/199源段。
3. 外部commandlet写资产前保存并关闭编辑器；使用编辑器内部接口时也先确认无未保存冲突。不要外部写后又保存编辑器的旧版本。
4. 新资产沿用最终类、稳定命名、相同Skeleton；有同名资产先检查父类/引用，不随意Duplicate出 `_New`、`_Test`。
5. 编译/保存每个改变的蓝图；Montage通知、轨道和Section由C++只读审计回读，不用Python“发现0条”签收。
6. 实际通知帧由资源/录制证据核对；秒数、录制采样行号、UE序列帧不是一回事。不得为了调混合改变伤害/移动时间。

## 1. E6.1：觉虫击已签收动作的回归清单

### 1.1 入口检查

**E6.1觉虫击与E6.2虫印/虫粉/聚爆均已由用户签收；本节动作配置不再是当前待办。** 以下Montage/GA/输入操作仅供未来改动后的针对性回归。聚爆专用Montage/GA复用操虫Sequence，不另造重复资产。

若源动画速度仍只有原作一半：由脚本先烘焙真正2x版并保留Root曲线/正确轴向；不要仅在最终Montage把RateScale改2，也不旋转Skeleton“纠正”Root朝向。

### 1.2 通用虫粉资产（已完成；PIE验收见§2.5）

`BP_IG_PowderCloud`、橙褐动态烟团＋金色火星的虫粉粒子系统及独立淡黄表面虫印粒子均已生成并接入DA。虫印和虫粉最新外观见本文件顶部与M6计划§6.8；需在重启编辑器后的PIE中确认真实尺度、亮度和动态形变。E6.1签收不代替虫粉生成闭环验收，最终仍按§2.5 PIE矩阵检查；保持烟团+火星的新外观口径。

父类为`AIGPowderActor`，状态/寿命/伤害由Resource/C++管理；M6.3字段及自然爆炸接口、资产接线均已落地，当前只做PIE验证。

### 1.3 常驻Montage核对

在 `/Game/Weapons/InsectGlaive/Anims/Montage/` 核对生成的：

- `AM_IG_JueChongJi`：193起手 → 193后半段按住循环 → 松开转195 → 196 → 199。
- `AM_IG_JueChongJi_Fall`：206表现，下落物理由C++管理。

操作：

1. 双击Montage，确认Skeleton与现有虫棍相同。
2. Slot=`DefaultGroup.DefaultSlot`，这是全身功能动作，不是送/收虫的上半身Slot。
3. 确认Sections为 `Startup193`、`Hold193Loop`、`Send195`、`PreFlight196`、`HunterFlight199`。进入`Hold193Loop`后最多循环3遍：前两遍后继仍是自身，第三遍后继自动转`Send195`；若提前松开Y+B，Startup193播完后转195，Hold193Loop中则立即转195。
4. 时间均按Montage播放时间理解，序列`RateScale=2`已计入。193源序列6.433s，候选切点源时间3.667s/Montage时间1.833s，完整有效时长3.217s；195完整有效时长1.617s。MHR F9记录196动作态样本在55.05007227s，首个199样本在55.05886133s，间隔0.00878906s。当前196片段已按该间隔裁为源时间`[0, 0.01757812]`（RateScale2后约0.00878906s），199 Section从该片段末尾开始；这只把持续时间对齐记录，196首帧是否是正确视觉姿势仍须预览确认。199目前仍是完整源段占位（有效1.467s），须从源动画中找出实际约0.75–0.768s的源时间片段（RateScale2后对应约0.375–0.384s），起始帧由资源/视觉证据确认。
5. 对193第220帧循环起点，重点检查首尾姿势与武器/Root接缝；数值审计只说明该处是姿势相似候选，不替代预览。
6. 所有有效段用源资产原有RateScale，Montage Segment PlayRate=1；核对手指/武器/脚/Root不跳变，无末帧T字形。
7. Root Motion审计已确认199与206序列均为`Root motion=false`；两段胶囊位移/重力由CMC/Host管理，不得在Montage或蓝图中另加Root Motion位移。
8. 保留Enable Auto Blend Out；Blend In/Out沿用本项目当前动作基线，记录实际值，不全局一键改为0。
9. 下落蒙太奇只能表现姿势；是否保持末帧/循环由本包PIE确认，不自行设置一个固定0.5s后回Idle。

### 1.4 通知轨道与放置

遵守现有语义轨道：

| 轨道 | 通知 | 核对点 |
|---|---|---|
| `Kinsect` | `IG Awakened Kinsect Event`，Event=KinsectLaunch | MHR录制确认的猎虫离手点，一次 |
| `Action` | 同类点通知，Event=ReleaseFacingCorrection | 第230帧（60fps Montage时间`3.833333s`），启动角色面向释放准心的平滑修正，一次 |
| `Action` | 同类点通知，Event=HunterFlightStart | MHR动作态进入199的边界，一次 |
| `IGAerialHandoff` | 现有 `IG Aerial Handoff` | 只有有真值支持的可操作空中释放点 |
| `Combo` / `Dodge` | 现有Window（仅本包明确允许时） | 不默认整段可以被任意攻击/翻滚打断 |

这里使用的是**点通知**，不是AnimNotifyState窗口。三个事件共用已编译的 `IG Awakened Kinsect Event` 点通知类，通过Event枚举区分。`HunterFlightStart`仍位于`HunterFlight199`入口、`KinsectLaunch`仍在已确认的离手点；`ReleaseFacingCorrection`已由`MHGZM6ReleaseFacingNotifyCommandlet -Apply`放在`AM_IG_JueChongJi`的`Action`轨道第230帧（显示/触发时间`3.83333325s`），一次。只读复核确认三者数量唯一且顺序为ReleaseFacingCorrection→KinsectLaunch→HunterFlightStart。该点只启动角色Yaw的`0.1s EaseOut`；Aim/ActorForward已在最终Y/B释放时冻结。点通知不需要拉持续长度。打开Montage时只需目视确认通知在Action轨道，不要再手工添加第二个。

本招伤害来自猎虫飞行，无额外武器段时**不创建Attack Collision窗口**。别用一个武器碰撞窗模拟多次贯通，也别放两个通知与一个0.4sTimer同时启动猎人位移。

时机必须来自MHR实录，不能凭UE Montage预览主观猜帧。`mhrise_20261006_205745_02_samples.csv` 中196样本约55.05007227s，首个199样本约55.05886133s；记录间隔0.00878906s。已将196 Montage占位段裁为对应短时长，并把 `HunterFlightStart` 放在紧随其后的 `HunterFlight199` Section入口（`Action`轨道），不放在GA/Montage开头，也不使用旧Timer。该采样间隔是运行时动作ID边界的观测分辨率，不证明196首帧姿势；须在编辑器预览核验接缝。199内部源帧范围仍待确认。

同一份F9 CSV**没有猎虫世界位置、骨骼/socket变换或离手事件字段**，因此不能由它推出 `KinsectLaunch`；该点由用户按MHR离手真值放置，不能把“195开始”直接当离手时刻。用户已确认完成放置；`MHGZM6RecordedHunterFlightSetup` 的只读审计现已确认两种事件各1个、先后顺序正确，并输出实际间隔及速度/距离派生结果。现有M6资产设置命令let使196短段与199入口可审计；不要使用旧版工具覆盖自定义剪裁。

### 1.5 GA Defaults

`/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi` 父类 **MHGZAwakenedKinsectAttackAbility**。

| 配置 | 填写 |
|---|---|
| Attack Montage | `AM_IG_JueChongJi` |
| Default Entry Section | `Startup193` |
| Attack Segments | 恰好1段；由M6资产设置工具复制已验收 `GA_IG_CaoChongZhan.AttackSegments[1]`，即 `IG_RearTip → Root`、现有伤害GE、命中部位防御、单次ContactOnly。不要另造GE或猜动作值 |
| Dance Montage / Section | 复用 `GA_IG_CaoChongZhan` 的常驻舞踏Montage和 `HitDance189`；DanceFromDashTuning、Preparation时长、BlendIn由工具从操虫斩GA复制 |
| Max Correction Angle | `90`；RTYB激活时依据Action相机准心方向即时修正Yaw，最大修正90° |
| Weapon Resource Costs | 空；C++在激活时决定是否原子消费一灯，不能在CDO里固定扣灯 |
| 下落/运动配置 | 普通206下落继续用本招配置；仅命中后的189舞踏明确复用操虫斩的Dash→Dance运动参数 |
| Required Tags | Grounded、Unsheathed；TripleUp不是Required，没三灯仍走普通猎虫分支 |
| Blocked Tags | Attacking/Dodging/Sheathing/Hitstun/Knockdown/Dead/Aerial完整Tag，见M6计划§5.1；不把空中CantAttack当通用地面锁 |

“标签”栏的Required/Blocked与Combo边是两层门槛，都不能取代C++真实Handle/猎虫/Runtime检查。不要在EventGraph再次扣三灯或调用DeployKinsect。

#### 觉虫击猎人武器命中与189交接核对

1. 在 `GA_IG_JueChongJi` 的 `Attack` 数据中确认只有 `AttackSegments[0]`，碰撞几何为棍尾 `IG_RearTip` 到角色 `Root`；其GE/动作值来自已验收操虫斩武器段，不要自行复制GE资产或替换动作值。
2. 在 `AM_IG_JueChongJi` 的 `HunterFlight199` 段确认已有 `Attack Collision` NotifyState，`ConfigIndex=0`。它必须完全落在该Section内。设置工具仅做只读范围审计，不改通知的开始帧、结束帧或轨道。
3. 不要在Send195/PreFlight196/起手193或共用189蒙太奇上添加本次武器判定窗口。进入舞踏只由199期间真实伤害GE成功回调触发；普通Sweep接触不算命中。
4. 保存后运行M6资产设置工具的 `-AuditOnly`；报告应确认GA只有一段、轨迹端点正确、DanceMontage/HitDance189及入口参数与操虫斩GA一致、AttackCollision窗口只读审计通过。PIE再验证：命中进入189并增加独立 `AwakenedKinsect` 舞踏来源；未命中继续206。

`DA_IG_Combat`中设置本招运动参数（初始值为F9候选，必须保留可调性）：

| 字段 | PIE前候选值 | 含义 |
|---|---:|---|
| `AwakenedHunterFlightDurationSeconds` | `0.38` | 199状态间隔；从HunterFlightStart通知起计 |
| `AwakenedHunterFlightSpeed` | `4826` | 释放帧锁定方向上的总初速cm/s，不是平面速度 |
| `AwakenedHunterFlightGravityMagnitude` | `1190` | 199重力cm/s² |
| `AwakenedAimHorizontalCorrectionAngle` | `90` | 松键帧锁定的猎虫/猎人方向相对释放帧Forward限幅±90°；角色Yaw转向由第230帧Notify启动，C++共享方向任务用0.1s EaseOut完成 |
| `AwakenedAimCorrectionAngle` | `60` | 释放帧俯仰限幅 |
| `AwakenedHunterFallHandoffVelocity` | `(0,0,900)` | 206接管时的UE世界速度cm/s；清除199水平惯性 |
| `AwakenedHunterFallGravityMagnitude` | `2885` | 206重力cm/s² |
| `AwakenedHunterFallMontage` | `AM_IG_JueChongJi_Fall` | Host托管的206姿势Montage；直到CMC真实落地 |

觉虫击的猎虫飞行不在 `DA_IG_Combat` 配射程/速度：审计读取 `KinsectLaunch`=`3.95804286s` 与 `HunterFlightStart`=`4.84202242s`，未命中间隔为`0.88397956s`。普通目标距离取当前199猎人飞行候选名义路径`4826 cm/s × 0.38s = 1833.88cm`，因此C++固定速度为`1833.88 / 0.88397956 = 2074.573cm/s`；三灯固定速度暂取1.5倍`3111.8595cm/s`。相同时间内两档距离分别约`1833.88cm`、`2750.82cm`。运行时仅为通用`FKinsectFlightRequest`派生停止距离=`固定速度 × Montage通知间隔`，不保留可编辑射程/速度资产字段。普通分支命中、碰地/世界阻挡或到达距离终点后均只悬停；猎人冲刺自然结束、途中落地或被中断时才召回。三灯贯通并逐次点灯，到请求终点后按原策略召回。若PIE需要调普通飞行范围，只改C++普通速度常量，三灯始终为普通速度的1.5倍；不要新增额外怪物体长。

旧报告 `Saved/M6Audit/m6_hunter_flight_notify_placement.json` 只包含 `HunterFlightStart`，生成时间早于用户在 `Kinsect` 轨道放置 `KinsectLaunch`，是过期/不完整输出，不是当前资产缺事件的证据。已重跑更新后的只读 `MHGZM6RecordedHunterFlightSetupCommandlet`：两类事件各1个且顺序正确，报告同时列出显示/触发时间、`0.88397956s`间隔及两档速度/终点距离。报告 `success=true`；该审计不保存 Montage。不要运行 `-Apply` 覆盖已放置的通知。

### 1.6 InputProfile与Combo

生成器已完成本轮接线。只读查看并确认下列结果，不要手动复制第二条或清空既有边：

1. `DA_IG_InputProfile.Chords` 的RTYB：OutputTag=`Input.Weapon.RTYB`；TriggerControls=**现有Y/B物理映射值**；RequiredHeldModifiers=`Input.Modifier.RT`；ExactModifiers=true；AimSnapshotContext=Action；ConsumeTriggerControls=true。为了让 GA 在放开组合键时收到同一激活身份的 `Completed`，必须设 `ReleaseControlTag=Input.Weapon.Y`、`AdditionalReleaseControlTags=[Input.Weapon.B]`。RT只作修饰键，不能设为保持释放控制。保持现有时限与优先级规则，较完整组合必须胜过RTY/RTB。
2. `DA_IG_Combo.Transitions` 新边：TransitionID=`Idle_RTYB_AwakenedKinsectAttack`；SourceState=`Idle`；InputTag=`Input.Weapon.RTYB`；AbilityClass=本GA。
3. ExecutionPolicy=ActivateAbility；StatePolicy=Replace；TargetState=`IG.Special.AwakenedKinsectAttack`（**FName状态，不是GameplayTag**）。
4. RequiredTags仅为Grounded/Unsheathed（TripleUp不是必需门槛）；Combo边`MaxCorrectionAngle=-1`（继承GA）；GA `MaxCorrectionAngle=90°`，供RTYB激活时的Action相机准心Yaw修正使用；bMatchAnyState=false；bAutoTransition=false；初始Idle边bRequiresComboWindow=false。
5. 不新增任意攻击状态全局入口；不在CoreAbilities再授予这招。
6. 保存 → 只读审计 → PIE确认一次组合不会连发其他GA。分别在Startup193和Hold193Loop测试松键：前摇中松键必须完整播完Startup193，再转入195；循环中松键须立即转入195；分别覆盖先松Y/先松B，RT持续按住也不应阻塞。观察姿势接缝；若有突变，调整Montage Send195入口，不增加输入等待。

### 1.7 PIE最小验收

使用三色木桩和已有采集途径取得三灯，不临时写UI加Tag。2026-10-12操虫斩采集改为共用真实猎虫单精华携带槽与回手交付，命中先显示椭圆颜色、回手后再点猎人灯；不再用旧“操虫斩即时点灯”作当前验收口径。自动化负责到期/失败边界。

- 没三灯RTYB：走普通猎虫分支，不扣灯；动作/输入正常。
- 有三灯空放：只消费一次，猎虫返回，猎人完整执行并正常落地/再操作。
- 穿过有效部位：逐击伤害、精华、粉尘计数对应，可重新形成三灯且后续不刷新。
- 普通武器/猎虫触碰粉尘不爆炸。
- 镜头/摇杆在按出后变化，不改变被冻结的本招方向；角色不在最终松Y/B时立刻转向，而由Montage第230帧`ReleaseFacingCorrection`点通知启动0.1s EaseOut；丢虫Notify不瞬转；超角Clamp而不是无法触发。观察此前报告的单帧放大/黑屏是否仍出现，复现时区分纯黑帧与模型放大并保留当次日志。
- 记录正常速度和必要的慢放观感；运动/计数以Telemetry分析为准。

## 2. E6.2：局部虫印、虫粉与两段粉尘聚爆

### 2.1 入口与自动化边界

**E6.2已完成并由用户签收。** 以下是已完成工作的回查步骤，不是当前待办；虫印/虫粉表现资产和DA配置不需要重建。M6.4 HUD修复专项已通过，当前按§3.7复验E6.3，不重新接线或生成同名Widget。

### 2.2 虫印来源与90秒配置

1. 回读 `GA_IG_ChongYin`：父类具有成功武器伤害建印钩子、MarkSegmentIndex对应用户已有有效碰撞段；不因为原生Setter已存在就跳过资产审计。
2. 回读 `GA_IG_JiXiTuCi` / `AM_IG_JiXiTuCi_Over`：识别地面第一击真实ConfigIndex，映射新建印字段/钩子；禁止调整四段碰撞窗或把下坠段作为来源。
3. `DA_IG_Combat` 配置：KinsectMarkDuration=90s、表面切向覆盖100×100cm（半宽50cm）、法向容差、自动寻印范围/攻击间隔。现有四种近战来源共用唯一Resource虫印，收刀/收虫不会清除；四连印斩GA段1与觉虫击猎人冲刺段0的追加回查见§3.18。
4. 已生成并接入 `PS_IG_KinsectMarkCloud`：淡黄色、发光、不规则雾状，粒子填充三维球形空间，视觉包络约1m×1m×1m；Gameplay斑块为100×100cm表面切向范围。运行时组件跟随命中组件/骨骼局部点，不跟随怪物Actor原点或放大覆盖整个部位；Gameplay范围保持固定100cm世界宽度，不再随目标Scale额外膨胀。
5. Debug绘制的产粉局部区域与粒子覆盖相符；在同一Hitzone内攻击标记中心和明显远离中心处，分别验证可产/不可产。

### 2.3 红色虫粉Actor与新生命周期

1. 已生成 `/Game/Weapons/InsectGlaive/BP/BP_IG_PowderCloud`，继承 `AIGPowderActor`，并已接入DA；筛选、伤害、寿命和爆炸事务仍由C++管理。
2. 已生成 `/Game/Weapons/InsectGlaive/VFX/PS_IG_PowderRedCloud`：红色、不发光、近球状雾团；用户已确认外观。粒子只作表现，虫印黄色粒子与红色可消费虫粉分别引用。
3. 所有表现组件NoCollision、SimulatePhysics=false、无Overlap战斗逻辑；不阻挡Pawn/Visibility/武器/Hitzone。
4. DA已接入正式PowderActorClass与两个Cue。当前默认值：PowderLifetime=180s（按现行玩法合同固定）、PowderMaxCount=100、MarkedHitPowderCooldown=10s、PowderNaturalExplosionRadius=150cm、PowderVortexExplosionRadius=300cm、PowderVortexDelay=1s、PowderMotionValue=100（原始百分数）、PowderDespawnDistance=10000cm、PowderVortexDamageAt1/5/10=10/50/100。除固定寿命合同外，其余均为可编辑调参项。粉尘表现包络半径约75cm，与伤害球半径相互独立。
5. 自动寻印和粉尘聚拢半径均为3000cm（训练场可走地面边长的一半），自动攻击间隔2s。以上字段可在`DA_IG_Combat`编辑；单团伤害按生成时猎虫攻击力快照×`PowderMotionValue/100`。
6. 到期180s自动一段爆炸；离玩家过远仅消失；同PowderID不能再被聚爆。聚爆刷新ExpireAt，旧到期Timer不得在原时刻引爆。
7. 100团上限按当前计划“拒绝新团”；Available+Reserved共同计数。基于粒子数量设置表现预算，不能把100团逻辑上限改成只显示/计算最近5团。

### 2.4 聚爆输入、目标、Montage与场地范围

1. `DA_IG_InputProfile` 的LTYB Chord输出仍为Input.Weapon.LTYB，但AimSnapshotContext从旧None改为Kinsect；在组合解析时冻结目标，不在GA结束或吸粉中重新读相机。
2. 已有 `GA_IG_PowderVortex` 父类/输入/Combo壳保留：Idle→IG.Special.PowderVortex，Grounded+Unsheathed，AttackSegments/三灯成本为空。不要加到CoreAbilities或重复新增边。
3. 常驻 `AM_IG_PowderVortex` 继续用AS_Unsh_CaoChong、DefaultSlot、Section=PowderVortex、Action轨道唯一提交点事件。该事件提交ToPoint猎虫请求；实际聚拢在抵达目标时开始。审计新类回调含义，不重复加伤害窗。
4. 约0.766667s的猎人Montage不必覆盖旅途+1s；它正常结束后Resource事务可继续。不得关AutoBlendOut冻结末帧凑时长，也不能在EndAbility把已提交请求无条件取消。
5. 地图只读审计确认 `L_DemoArena` 可走地面边长6000cm；`PowderGatherRadius=3000cm`。`KinsectAutoMarkSearchRadius`也按同一半边长设为3000cm。
6. 到达时预留球内全部己方Available团并刷新寿命；不再用旧600cm/0.5s/5。吸粉视觉在1s内完成，操虫/收虫可取消；取消团停留当前位置、保留刷新寿命、恢复Available。
7. 飞行目标为准心部位/地点，不为旧猎虫位置；怪物目标跟随局部锚点，世界/空气目标固定。0粉尘允许飞向目标，但不造成两段伤害。

### 2.5 PIE固定矩阵

| 测试 | 观察及可读取证据 |
|---|---|
| 虫印斩 / 急袭地面第一击 / 四连GA第2段 / 觉虫击猎人冲刺 | 分别在成功伤害后建印一次，索引0/2/1/0；其他段/空挥/伤害失败不建印；原伤害仍正常 |
| 虫印替换/90s/怪物死亡 | 替换时只剩新印；到期或目标死亡清理；收刀/收虫/离范围保留 |
| 局部命中 | 同Hitzone标记覆盖内外有区别；Gameplay方形100×100cm，黄色视觉为约1m立体球团，且不随目标Scale膨胀 |
| 自动寻印 | 猎虫已离手才自动攻击；Attached不自动飞出；真实攻击间隔与配置一致 |
| 玩家操作和招式优先 | 操虫先完成原请求再自动寻印；收虫回手后不重飞；操虫斩/觉虫击不被自动请求抢占 |
| 10s产粉/贯通例外 | 普通和招式猎虫命中虫印局部受CD；三灯觉虫击每成功Hit产一团，同Hit不双产 |
| 虫粉外观/180s/远距/100团 | 橙褐烟团立体翻涌、周围有金色火星，视觉半径75cm；到期一次150cm半径伤害；远距仅消失；存量不超100 |
| 定点聚爆和寿命刷新 | 猎虫飞往准心点，全体范围内己方团吸入，寿命刷新，不在旧猎虫点原地爆炸 |
| 操虫/收虫取消 | 到达前或1s吸粉期均立即取消引爆；新指令生效、无原位瞬移、无旧Timer迟到爆炸 |
| 数量0/1/4/5/9/10/100 | 0团无伤害；1/5/10阈值跳变；D2=实际ΣDi |
| 两段 / 多Hitzone | 每目标两个独立阶段提交/数字，半径均300cm；不用同AttackID吞第二段，也不按部位重复 |
| Montage结束/连续请求/关闭 | 动作播完不提前取消已提交事务；取消与重复回调幂等，Runtime清理无残留 |

正常速度操作并用现有Telemetry录制；寿命/CD/边界可由自动化加速验证，不要求人工精确掐秒。完整通过后才签收E6.2；觉虫击已签收动作只做被新公共路径影响的针对性回归。

## 3. E6.3：主HUD、条与资源面板

### 3.0 目标视觉真相源：用户提供的《怪物猎人：世界》五张参考图

五张原图已从临时附件目录**原样复制**到 `docs/editor/references/m6-ui-world/`，源文件与副本的SHA256均一致。后续实施者必须先打开这些项目内图片，不依赖对话附件、上下文摘要或“世界风格”几个字猜设计。这里保存的是设计参考，不是运行时Texture或已完成的Widget资产。

| 用户原图 | 永久参考文件 | 要实现的内容 |
|---|---|---|
| 图一：LT无目标 | [01-lt-no-target.png](references/m6-ui-world/01-lt-no-target.png) | 屏幕中心小型浅灰白断开的菱形/斜向标记；无有效目标时不显示金色锁定样式 |
| 图二：LT有目标 | [02-lt-target.png](references/m6-ui-world/02-lt-target.png) | 金黄色细菱形轮廓、中心点及上下左右四个尖形标记；不是普通十字线 |
| 图三：三灯与猎虫携带精华 | [03-kinsect-extract-icons.png](references/m6-ui-world/03-kinsect-extract-icons.png) | 横排1/2/3菱形灯，依次为红/白/橙；左侧猎虫图标及独立携带精华提示 |
| 图四：RT无目标 | [04-rt-no-target.png](references/m6-ui-world/04-rt-no-target.png) | 浅灰白细圆环、中心点及四个小三角标记 |
| 图五：RT有目标 | [05-rt-target.png](references/m6-ui-world/05-rt-target.png) | 橙色圆环、中心点及较明显的四个三角标记；**不能替换成菱形** |

三灯/携带精华图标的局部原图：

![世界虫棍三灯及猎虫携带精华参考](references/m6-ui-world/03-kinsect-extract-icons.png)

血条/耐力条参考图一、二、四、五的左上角：上方细长绿色生命条，下方细长黄色/金色耐力条，深色细边、浅色高光和收尖/斜切端部，比例及间距按原图布局实现。当前截图中的两个深青色矩形底框加字符徽标不是目标样式。资源面板应是紧凑图标区，不以大块 `RED/WHITE/ORANGE` 文字和三根横条作为正式外观；舞踏、粉尘数量等本项目额外信息可保留，但不能代替参考中的猎虫与三灯图标。

只参考本包要求的血条、耐力条、猎虫资源图标和两种准心，不因此增加原作小地图、投射器、招式列表等整套HUD。红/白/橙是三灯及携带精华图标的颜色；LT目标准心按图二用金黄色，不能把此前文档中的“LT按目标精华色变色”当成用户已确认的外观要求。

显示合同不变：只有按住LT/RT才显示对应准心，松开隐藏；同时按住时保持现有Kinsect模式优先。检测目标沿用本项目Hitzone与遮挡规则。**镜头位置、镜头管理、SpringArm、俯仰限制及Gameplay AimSnapshot均保持现有项目不变。**

### 3.1 已生成资产与引用核对

五个Widget已由`Scripts/AssetOrganization/run_m6_ui_remote.py`通过编辑器Python Remote创建；原生`MHGZUIAssetSetupLibrary`构建Designer树，脚本编译并保存资产/引用。下表是实际资产及父类，不是待创建清单。现有`BP_MHGZHUD`和GameMode均复用，禁止重建、覆盖或删除：

| 资产 | 父类 |
|---|---|
| `/Game/UI/HUD/WBP_HUD` | MHGZMainHUDWidget |
| `/Game/UI/HUD/WBP_HealthBar`、`WBP_StaminaBar` | MHGZAttributeBarWidget（分别设为Health/Stamina） |
| `/Game/UI/HUD/WBP_Crosshair` | MHGZCrosshairWidget（由原生绘制逻辑显示准心） |
| `/Game/UI/InsectGlaive/WBP_IG_ResourcePanel` | MHGZInsectGlaiveResourceWidget |

资产路径：`/Game/Blueprints/HUD/BP_MHGZHUD`（`Content/Blueprints/HUD/BP_MHGZHUD.uasset`）。其原生父类已确认；保留原有生命周期所有者与GameMode引用，不重父类、不替换资产。

自动化已完成的引用回读：`BP_MHGZHUD`的MainHUD/HealthBar/StaminaBar/Crosshair类均指向上表对应WBP；`BP_Demo_GameMode.HUDClass`指向现有`BP_MHGZHUD`；`DA_WeaponRuntime_IG.ResourceWidgetClass`指向`WBP_IG_ResourcePanel`。不得再次运行会覆盖用户资产的手工重建流程。

脚本生成的是Designer布局骨架；2026-10-12在这些**同一份WBP资产的原生父类**中补齐显示绘制和初始化修复，没有重建/覆盖WBP。§3.0仍是视觉真相源，§3.8保留旧失败证据，§3.9记录本次修复及验证。不要添加重复Slot或第二套Widget。

`WBP_IG_KinsectStamina`/`WBP_IG_ExtractDisplay`可作为面板内部设计子控件后续拆分；首版直接在ResourcePanel里放组件即可，不为完成旧概括清单额外制造第二个绑定/生命周期所有者。

### 3.2 WBP_HUD：四个容器

Designer操作：

1. Root放Canvas Panel。
2. 放四个Overlay，准确命名 **HealthBarSlot、StaminaBarSlot、CrosshairSlot、WeaponResourceSlot**，勾选Is Variable；对应C++ BindWidget必须拼写一致。
3. HealthBarSlot在左上，StaminaBarSlot在其下方，WeaponResourceSlot在资源区；CrosshairSlot覆盖全屏（锚点 `(0,0)` 到 `(1,1)`、Offsets为0），准心由其子Widget的NativePaint绘制在本地几何中心。
4. 四个Slot内不要在Designer再放同一WBP实例；实际子Widget由HUD创建并AddChild。
5. 不在Construct/EventTick里CreateWidget或AddToViewport，不调用GetPlayerCharacter→写Resource。
6. Root设不拦截游戏输入的可见性（Self Hit Test Invisible/Not Hit-Testable对应选项），不要让覆盖全屏的控件吞鼠标/手柄。
7. Compile/Save；缺BindWidget即检查名字和组件类型，不删掉代码校验。

### 3.3 血条与耐力条

现有WBP保留 `ValueBar`、`ValueText` 名称以兼容已有资产；原生 `NativeConstruct` 隐藏旧矩形占位子控件，`NativePaint`绘制细边、绿色/金色填充、高光、斜切端部与小徽标。不用再在Designer放一层可见矩形条。比例直接来自Health/MaxHealth或Stamina/MaxStamina，不能写死为满条。创建前绑定不会再被NativeConstruct的0/0占位覆盖。

Class Defaults填写：

- HealthBar CurrentAttribute=MHGZAttributeSet.Health，MaxAttribute=MaxHealth。
- StaminaBar CurrentAttribute=Stamina，MaxAttribute=MaxStamina。

不需要在EventGraph覆写 `On Value Updated`，也不要把 `ValueText` 改成数值或为Percent/Text创建逐帧Binding。C++是唯一数据绑定所有者；测试Max值变化时，条比例应立即更新，不只看Current变化。

### 3.4 虫棍资源面板

整体视觉按§3.0图三实现：原生绘制猎虫图标、横排编号菱形1/2/3（红/白/橙）、独立携带精华色标和金色猎虫耐力条。额外小字 `D`/`P` 分别显示舞踏层数/可用粉尘数。参考图仅作设计依据，不把整张游戏截图当运行时UI；绘制缓存不拥有战斗状态。

现有Designer中的以下命名控件保留兼容，但原生构建时隐藏旧文本仪表盘；无需重新放置，也不作为可见皮肤：

- 猎虫耐力ProgressBar、当前/最大Text、猎虫状态Text。
- Red/White/Orange三个Image或边框，各一个倒计时Text/进度条。
- Triple高亮/倒计时区域。
- 猎虫携带精华图标：显示快照中的 `KinsectPendingExtractColor`；无效Tag时隐藏，Red/White/Orange时显示对应色。精华交付后该图标清除，已交付精华由三灯图标和计时显示。
- DanceStacks整数Text；可另显示DanceSource用于调试。
- 可用/预留粉尘数量Text（首版调试提示，可后续隐藏）。

**当前不需在EventGraph覆写 On Resource UI Updated**：原生实现从FIGResourceUISnapshot更新只读绘制缓存并请求重绘。若未来自定义BP皮肤覆写该事件，须先调用Parent以保留原生缓存。以下为字段语义，不是当前必须手工补的接线：

1. KinsectStamina/Max → 安全比例SetPercent；不存在猎虫时显示不可用，不自行Spawn。
2. 各灯显示为`bRed/bWhite/bOrange || bTripleUp`；计时由资源负责。首版不强制显示倒计时文字，Remaining/Ratio保留供后续皮肤使用。
3. bTripleUp=true时三个编号灯均高亮，单灯GE被替换而单灯布尔值变false是正确状态；**不能调用ApplyExtract给三个灯补GE**。携带精华是独立竖立椭圆，不从猎人三灯推导；按用户确认，仅改图标，不扩展当前单精华携带/交付机制。
4. bTripleUp=false → 按三个单灯实际状态显示。
5. DanceStacks→SetText；清零立即显示0；不要让Widget保存另一份舞踏计数。
6. 粉尘计数只显示；不在Widget按钮里Consume/Release。

没有EventTick/延迟等待Resource；Native Bind会立即推当前快照。不要在面板Construct重复绑定四个旧Resource委托，造成重复更新。

### 3.5 准心

`WBP_Crosshair`的Designer树只保留空RootCanvas；不要再放中心Image、短线或重复绘制逻辑。`UMHGZCrosshairWidget::NativePaint`负责画四种状态，HUD把该Widget按Fill加入全屏CrosshairSlot。准心有四个可区分状态：

| `Mode` | Target | 视觉要求 |
|---|---|---|
| `Kinsect`（LT） | 无有效部位 | 图一的小型浅灰白断开菱形/斜向标记，不显示金色锁定 |
| `Kinsect`（LT） | 有效Hitzone | 图二的金黄色细菱形、中心点与四向尖形标记；不以目标精华色替代金色 |
| `Action`（RT） | 无有效部位 | 图四的浅灰白细圆环、中心点与四个三角标记 |
| `Action`（RT） | 有效Hitzone | 图五的橙色圆环、中心点与四个三角标记，不画成菱形；不授予精华 |
| `None` | 任意 | 隐藏并清除旧目标颜色/锁定效果 |

可见性输入约定已由用户确认：**只有持续按住LT或RT时显示；松开后隐藏是正确行为，不是故障。** 未收刀时LT显示Kinsect模式，RT显示Action模式；同时按下时Kinsect模式优先。验收需在PIE内分别按住LT、RT检查无目标/有目标，再松开确认隐藏。

原生层订阅 `Combat.State.Aiming.Kinsect` 与 `Combat.State.Aiming.Action`，同步AimComponent目标/部位并在变化时请求重绘；Blueprint不需要另订GameplayTag。HUD现在仅在子控件树就绪后接收Pawn上下文，并重放当前Runtime Ready。日志应依次出现 `Widget tree ready`、有效Context、`Gauge bound ... ratio=...`、准心 `ASC bound=AbilitySystemComponent` 和 `Resource panel created`。只看到Context仍不能当作子控件绑定证据。四态绘制为LT断开斜线/金黄菱形、RT灰色/橙色圆环；不以精华色改变LT目标颜色。

不在WBP自己LineTrace，不调用晚AimLock获取显示；准心只显示，Gameplay输入快照由现有Router/AimComponent独立决定。**本次只读取当前PlayerController相机视点进行准心可见目标检测；不得改相机位置、SpringArm、PlayerCameraManager、镜头管理/俯仰限制、输入快照时机或瞄准方向。**

### 3.6 静态引用已接线，运行时绑定尚未通过

脚本已经完成并保存以上引用。只读回读结果：

- `BP_MHGZHUD`: MainHUD=`WBP_HUD`、HealthBar=`WBP_HealthBar`、StaminaBar=`WBP_StaminaBar`、Crosshair=`WBP_Crosshair`。
- `BP_Demo_GameMode.HUDClass`=`BP_MHGZHUD`。
- `DA_WeaponRuntime_IG.ResourceWidgetClass`=`WBP_IG_ResourcePanel`；`ResourceComponentClass`保留原`URes_InsectGlaive`。
- `L_DemoArena`已有GameMode配置未改；脚本没有改相机位置/管理。

§3.8的初始化缺陷现已按§3.9修复并通过实际资产生命周期/渲染专项；剩余工作是§3.7的真实关卡PIE目视复验。静态引用、专项测试和离屏预览分别是接线、绑定、绘制证据，均不能单独代替用户PIE签收。

当前`ResourceWidgetClass`已配置；若未来清空该字段，资源组件仍须保留有效的`URes_InsectGlaive`，不能为成对配置而一起清除。

### 3.7 HUD验收

- 开始PIE就显示正确值，不要先扣一次耐力/点一次灯才能显示。
- LT无目标/有效Hitzone、RT无目标/有效Hitzone四种样式逐一对照§3.0原图；LT目标金黄菱形，RT目标橙色圆环；墙遮挡/无效目标回到该模式的中性样式，松开瞄准后隐藏并清色。
- 猎虫命中目标后，携带精华图标按快照显示；猎虫把精华交给猎人后携带图标消失，三灯显示对应灯色/时间。
- 对比改动前后相机位置、镜头管理和动作方向快照：必须完全不变。
- 灯到期/三灯消费/重新三灯、猎虫耐力、落地舞踏清0即时更新。
- 第二次PIE仍只有一个HUD，UnPossess/重建不残留旧资源面板；日志/Telemetry证明旧绑定清理。
- UI不导致输入模式变为UIOnly、游戏暂停或Focus抢走游戏按键。

### 3.8 最新PIE失败根因与后续修复合同（2026-10-11；本轮仅诊断/文档）

本节保留2026-10-11的失败证据与当时合同；其中“待实现/目前/下一轮”描述的是修复前状态。2026-10-12实施结果见§3.9，不删除或改写历史证据。

实际失败截图已保存为 [pie-hud-unbound-20261011.png](references/m6-ui-world/pie-hud-unbound-20261011.png)，**不是目标参考图**。现有证据足够定位初始化缺陷，当前不需要用户重复录同一版本。

`Saved/Logs/MHGZ.log`两次PIE（日志时间14:42:08、14:42:58）均出现相同顺序。第一场的关键记录为：

```text
14:42:08.308 [E6.3HUD] Context ... pawn=BP_IG_Character_C_0 ASC=AbilitySystemComponent Aim=AimComponent Host=WeaponRuntimeHost
14:42:08.309 [E6.3HUD] ... Main=WBP_HUD_C_0 ... CrosshairSlot=CrosshairSlot ResourceSlot=WeaponResourceSlot
14:42:08.309 [E6.3HUD] Attribute style applied=WBP_HealthBar_C_0 ... ratio=0.000
14:42:08.309 [E6.3HUD] Attribute style applied=WBP_StaminaBar_C_0 ... ratio=0.000
14:42:08.309 [E6.3HUD] Crosshair widget created and attached: WBP_Crosshair_C_0
```

两场运行期间均没有有效 `ASC bound=AbilitySystemComponent`；准心的 `ASC bound=None` 与 `Refresh visible=0 mode=0 aim=None` 只在退出PIE解绑时出现。实际Controller、Pawn、ASC、Aim、Host及主面板插槽均已存在，不能再把这次失败归因为没有Possess、没按瞄准键或找不到Crosshair类。

源码调用链与断点：

1. `AMHGZPlayerController::BeginPlay/OnPossess` 及 `AMHGZCharacter::PossessedBy` 能提前调用 `AMHGZHUD::RefreshPawnContext()`；本次日志直接证明Context刷新早于UI创建，具体是哪一个入口不影响后面的确定性缺陷。
2. `MHGZHUD.cpp` 的 `RefreshPawnContext`先保存BoundCharacter/ASC/Aim/Host并设置 `bPawnContextInitialized=true`；此时子Widget尚不存在，三个 `if (HealthBarWidget/StaminaBarWidget/CrosshairWidget)` 都无法执行绑定。
3. `BeginPlay`随后创建三个子控件，末尾再调用Refresh；因为上下文指针相同且已标记initialized，直接return。**“数据源未变”被错误当成了“新建控件已绑定”。** 血/耐力条因此停在NativeConstruct的0/0占位，LT/RT准心没有订阅瞄准Tag或Aim委托，不能进入可见状态。
4. 资源面板存在第二处同类缺陷：`HandleWeaponRuntimeReady`先写 `BoundRuntimeToken=Token`，之后才检查 `MainHUDWidget/WeaponResourceSlot`。Ready若在主面板创建前到达，创建被跳过，Token却已被记为处理完成；相同Token后来也会提前return。结合本次提前刷新和资源面板缺失，这条路径解释猎虫面板未创建；当前日志未逐项打印Ready前置条件，不能声称已从日志直接观测到每个分支。
5. 视觉另有独立未完成项：`MHGZUIAssetSetupLibrary`生成的属性条/资源区仍为矩形ProgressBar+Text骨架；`MHGZCrosshairWidget::NativePaint`目前把RT有目标画成菱形、LT有目标按精华色画。即使修好绑定也不会自动变成五张目标图，须完成视觉实现，不能用调高亮度/扩大Slot替代。

下一轮实现须冻结以下合同，不以Delay、Tick反复重绑或强制常显准心掩盖顺序错误：

1. 将“Widget树可绑定”与“Pawn上下文已绑定”分开；早到的Pawn/Ready信号不能把尚不存在的UI记成已完成。创建根面板与子控件后统一执行一次有效绑定和当前值同步；首次无Pawn、后续Possess及再次PIE同样成立。
2. Resource Ready必须可在容器就绪后重放；成功创建并绑定面板后才标记该Token已完成，或用独立的待处理状态。Runtime重建/UnPossess/EndPlay仍成对解绑，不建立第二个UI所有者。
3. 新增实际创建HUD/子Widget的生命周期回归，覆盖“先Refresh/Ready后BeginPlay”和“先Widget后Pawn/Ready”两种顺序。原 `MHGZ.M6.UI` 两项测试只测试资源快照与遥测队列，**不创建AMHGZHUD/准心/属性条，2/2通过不证明这次链路正确**。
4. 修复后再做短PIE：初始条值及猎虫区立即出现，分别按住LT/RT检查四态，释放隐藏，携带精华/交付更新正确；第二次PIE不重复或漏绑定。此时日志与屏幕结果同时作为验收证据，镜头合同保持不变。

### 3.9 HUD修复实施与验证（2026-10-12；E6.3仍待PIE）

实际修复内容：

- `AMHGZHUD`新增独立的Widget树就绪门禁，提前Refresh不再消费Pawn绑定状态；创建全部子控件后统一绑定ASC、Aim、Host并同步当前值。
- Resource Ready只有成功创建、插入并绑定资源面板后才消费Token。容器、Pawn或资源类晚到时可重试；重复Ready不重复建面板，旧Generation事件不影响新上下文；UnPossess/EndPlay成对解绑。
- 属性条在NativeConstruct保留已绑定当前值，不再重置0/0。健康与耐力均从Current/Max属性读取，Max变化同样更新。
- 复用现有五个WBP及其类引用，由`MHGZAttributeBarWidget`、`MHGZInsectGlaiveResourceWidget`和`MHGZCrosshairWidget`原生父类绘制目标结构：绿/金细长斜切条、紧凑猎虫与1/2/3红白橙菱形灯、独立携带精华提示、LT灰色断菱形/金色目标准心、RT灰色/橙色圆环准心。旧Designer占位子控件隐藏，保留命名兼容，不叠加第二套HUD。
- 显示仍由原ASC Tag、属性及Resource/Aim委托驱动；未修改相机、输入、位移、战斗或Gameplay AimSnapshot。没有重建/保存运行时uasset。绘制是按参考实现的原生矢量样式，并非将原作截图直接作为贴图；最终比例、颜色和可读性仍以真实PIE目视为准。

| 验证 | 最终结果与证据 |
|---|---|
| Development Editor编译 | 成功；`Saved/Logs/E63HUD-repair-build.log`，关闭Live Coding使用`-NoUBA` |
| 实际HUD专项 | `MHGZ.M6.UI` **5/5 Success、0失败**；其中4项含测试夹具警告。报告`Saved/M6Audit/E63HUDRepairFinalTests/index.json`；日志`Saved/Logs/E63HUD-repair-final-tests.log` |
| 生命周期覆盖 | 实际加载`BP_MHGZHUD`及配置的WBP类，不由测试补填引用；覆盖先Context/Ready后Widget、先Widget后Pawn/类引用、初始值、Current/Max更新、资源更新、重复Ready、旧Generation拒绝、解绑重绑与EndPlay |
| 准心与绘制覆盖 | LT/RT Tag显示与优先级、释放隐藏；`-MHGZUIRenderPreview -RenderOffscreen`下D3D12实际绘制，五态像素哈希不同，中心区域在隐藏态为空、四个瞄准态非空 |
| 离屏预览 | `Saved/M6Audit/E63HUDRepairPreview/00-idle.png`、`01-lt-no-target.png`、`02-lt-target.png`、`03-rt-no-target.png`、`04-rt-target.png`；资源亮灯是显示测试快照，不是战斗初始三灯 |
| 未验证范围 | 尚未得到修复后用户PIE签收；未重跑完整M6、DataValidation或打包，不以专项代替总验收 |

测试范围与日志限制：早期测试夹具曾触发无GameViewport的LocalPlayer ensure，早期绘制曾因数组元素自引用追加触发断言，均已修正并重跑。最终运行退出码0、无该ensure/fatal；启动阶段仍有两条未命名`LogAutomationTest: Error: Condition failed`，发生在本组开始前，不计入这五项失败；夹具无骨骼/视觉插槽等警告保留。离屏截图证明控件确实绘制，不能证明真实关卡曝光/DPI下的最终颜色，更不能签收E6.3。

**下一步只需短PIE复验：** 重新打开编辑器加载新编译模块；初始血/耐力与猎虫图标区应立即出现。分别按住LT、RT，朝空处和木桩有效部位检查四态，松开隐藏；放虫采集/回手检查携带精华与三灯是否分别更新；结束后再开一次PIE确认不重复/丢失HUD。无需手工重接线，不要求精确掐帧。若仍有问题，请说明哪一种状态并保留日志/截图；有动态更新问题再用现有Telemetry录制。

通过上述目视后才关闭E6.3并进入E6.4；本次未提前签收，也未关闭M6总验收。

### 3.10 三灯/携带图标与放虫瞄准追加修复（2026-10-12；待PIE）

本节是当前入口，§3.9保留前一轮5/5历史证据。用户最新录制：`Saved/RuntimeTelemetry/20261012-013354-BP_IG_Character_C_0-56417`。Character/State中27.346922～93.431213s有TripleUp，单灯Tag被正常替换；UI/Events只有表头，未记录UI状态、相机起点/目标或猎虫坐标，不能仅凭CSV计算偏靶距离。日志中第二次觉虫击有成功猎虫伤害，不能将现象描述为每次都不命中。确定性原因及修复由以下源码链路支持：

| 问题 | 原因 | 当前实现 |
|---|---|---|
| 三灯后三个灯熄灭 | TripleUp替换三个单灯GE，Widget仅看单灯布尔值 | 每盏灯显示`单灯有效 || bTripleUp`；不伪造GE、不改资源真值 |
| 臂上LT+Y偏离准心 | 从手臂沿相机Direction平行飞，既未移到中心射线也未指向TargetPoint | SendCommit按冻结Origin/Direction与当前猎人竖直平面求交，原子Detach并瞬移，再以ToPoint飞向冻结目标；场外重新放虫不瞬移 |
| 觉虫击偏右 | 棍尾与猎人共用相机方向，从不同起点发出平行射线 | 松键时仍只冻结一次目标；棍尾离手、猎人起飞分别从实际起点指向同一目标，沿用原Yaw/Pitch限幅。不重读当前相机、不实时追踪 |
| 携带精华样式 | 旧提示为菱形 | 改成竖立椭圆；渲染支持1色全填、2色二分、3色三分。**实际Gameplay仍最多携带一种**，显示快照容器最多由资源填一种，不积累/批量交付 |

臂上中心投影的近目标例外：朝近地板/墙瞄准，若射线目标位于猎人竖直平面之前，起点沿同一射线退到目标前（到达半径+碰撞半径间距），不强行瞬移到表面背后。拒绝无效方向/起点/目标时保留原猎虫状态。瞬移段不参与攻击Sweep，也不计飞行距离。

觉虫击保留速度、固定飞行时长、重力、三灯分支、丢虫通知、第230帧平滑Yaw修正及飞行结束召回合同。初始方向校正不取消角度限制或重力，因此不承诺任意角度/任意距离必命中；真实接触仍以攻击判定为准。普通收刀RT前向放虫保持原ActorForward合同。

验证：Development Editor冷编译成功；`Saved/M6Audit/E63AimCargoFixAcceptedTests/index.json`报告**39/39 Success**（UI 6、中心/视差2、觉虫击14、M3/M4基础8、自动寻印3、聚爆6）。GPU离屏测试检查真实WBP三灯渲染与三个单灯同时有效的渲染逐像素一致、到期熄灯，以及椭圆1/2/3色的不同输出。预览见`Saved/M6Audit/E63HUDRepairPreview/06-triple-lamps.png`与`08-cargo-1/2/3.png`；后两种是显示测试数据，不是已扩展Gameplay。日志`Saved/Logs/E63AimCargoFix-accepted-tests.log`；启动前两条未命名Condition failed和无骨架夹具警告保留，39项均无失败，进程退出码0。旧M3/M4夹具补齐Aim目标及当前Resource召回指令身份，没有修改生产召回逻辑或放宽断言。本轮未重跑完整M6、打包或DataValidation。

**下一步PIE复验：** 重新打开编辑器加载新模块，不重生成资产。通过已有方式获得三灯，三个编号灯应同时亮；消费/到期后按真实单灯状态更新。LT瞄准木桩分别检查臂上放虫中心起点、场外重新放虫不瞬移、命中后椭圆颜色/回手消失；再朝近地板放虫。觉虫击无三灯/有三灯各一次，从不同水平角瞄准有效部位，松键后转镜头不改已冻结目标。目视与当前Telemetry/日志共同复核后才签收E6.3；E6.1历史签收保留，本次改到的觉虫击瞄准做定向回归。

### 3.11 放虫接触/贴地接管/精华链路追加修复（2026-10-12）

本节覆盖§3.10的当前入口；§3.10及其39/39证据保留为历史快照。源录制`Saved/RuntimeTelemetry/20261012-111807-BP_IG_Character_C_0-56417`，原CSV只读保留。跨Input、Animation、Combat、CapsuleHits按世界时间/动作序号核对，不把“有身体碰撞”当作攻击命中。UI/Events仍只有表头，不能从该文件确认每帧椭圆外观。

| 项目 | 原因及现行修复 |
|---|---|
| 臂上放虫采集不到 | 普通ToPoint在50cm到达容差内即可停下，尚未进入实际部位胶囊Sweep。瞄准有效Hitzone的伤害请求新增接触型终止：必须实际接触或越过目标才结束，不以大容差提前Hover；空气/世界目标及聚爆原终点合同不变。普通操虫依旧先携带，回手才点灯 |
| 碰地后新指令难接管 | 原普通请求会旋转物理长胶囊，贴地朝上重新放虫可能旋进地板。所有飞行请求固定直立物理胶囊，只让Mesh朝飞行方向；重新激活Projectile仍完整重绑UpdatedComponent/Velocity。中心射线近表面起点按冻结法线、胶囊支撑长度留出间距，不在浅俯角时把胶囊塞入地板 |
| 操虫斩携带UI无色 | 生产成功回调原先直接ApplyExtract，根本没有PendingExtract。现经`CaptureActionKinsectExtract`写入真实猎虫单槽并立即发布UI，回手共用原`OnKinsectReachedPlayer`交付。保留单容量；武器伤害段不采集。旧探针兼容测试路径并非生产链路，不能以其即时点灯断言解释当前行为 |
| 觉虫击有一/两灯却不消耗 | 新合同：成功发动清空所有猎人灯，仅完整三灯强化。Triple仍用原子预留成本；单灯在GAS/Action确认成功、Montage前由Resource清除。失败预检不清灯，旧Action身份不能清新Action的灯，不消费虫子未交付精华 |
| 觉虫击视觉接触未舞踏 | 原窗口末帧和物理前冲退出间存在漏采样边界。NotifyEnd先补仍开放窗口的最后Sweep，199自然结束也在退役前补一次；取消/已关闭窗口不扩大。仍要求真实棍尾→Root攻击及GE成功；身体碰撞不是舞踏入口。**完整现象尚不能宣布解决**，见下方证据限制 |
| 资源面板简化 | 携带椭圆从12×20扩大为20×34 UI局部单位，长轴竖直；去掉D/P调试文字，保留编号三灯、实际猎虫耐力及携带色。仍未扩展多精华Gameplay |

觉虫击只读资产审计成功、changed=0：实际唯一AttackCollision为ConfigIndex=0，蒙太奇4.850000～5.333333s，真实区域`IG_RearTip -> Root`，半径14cm、间距20cm、上限16采样。录制中的身体碰撞如动作33/34/36/43/44/45/53/64/70处于约5.03～5.13s，均在该窗口内，不能归因为“通知太晚”。其中45/70有日志证明成功进入189；其他视觉接触不能凭身体碰撞记录推断武器真实接触。原录制没有逐候选武器Sweep/拒绝原因，**无法仅用现有CSV裁定余下全部漏判的根因**。

本轮添加`[AwakenedHitAudit]`日志：真实候选的baseGate/flightGate/精确Montage身份/位置，及关闭窗口时真实武器网格的首尾点、冻结目标到最终区域距离；最终距离只代表末段姿势，不代表整窗最小距离。沿用既有网格解析器，避免找错同名Root。Combat增加`KinsectCargoCaptured`和`SingleExtractsConsumed`，与原TripleCost事件区分。没有改用户通知、伤害区域/半径、相机、飞行速度或曲线，不用自动锁定/强制舞踏掩盖未接触。

验证：最终Development Editor编译成功；`Saved/M6Audit/E63KinsectFollowupReviewedTests/index.json`为本轮定向集合，M2伤害2、M3猎虫5、M4基础3、M5操虫斩27、M6 UI/猎虫/觉虫击/聚爆34，共71项。覆盖真实接触不被容差抢停、贴地朝上新指令实际位移、ActionAttached携带快照不提前点灯、单容量、普通觉虫击清已有红/白灯而不强化，以及旧三灯/伤害/召回/资源UI回归。GPU离屏检查并目视确认20×34椭圆及D/P文字移除；`Saved/Logs/E63KinsectFollowup-reviewed-tests.log`记录运行，构建日志`E63KinsectFollowup-final-build.log`。资产AuditOnly success=true、changed=0，日志`E63KinsectFollowup-asset-audit.log`。未运行完整M6、打包或DataValidation；自动化不代替真实姿势下的全部命中判断。

复验：重开编辑器，LT臂上放虫真实接触后观察椭圆，收虫交付；碰地后LT重新瞄准空中/部位检查能否出发；操虫斩猎虫段成功后先看携带色再看回手点灯。觉虫击用一灯、两灯、三灯分别发动，激活时旧灯应清空，仅三灯分支贯通；近距离几次前冲确认真实命中舞踏，若仍视觉漏判保留当前日志和录制，由新审计数据裁定，不改通知凑结果。E6.3和这次行为改动的针对性PIE仍待签收，E6.4未开启。

### 3.12 觉虫击199贴地起飞立即结束修复（2026-10-12）

最新原作来源为`mhrise_20261012_141811_02_samples.csv`，模型与20段分类见[真值§7.2.1](../reference/操虫斩曲线.md#721-2026-10-12追加199贴地不能立即结束)。本次不改数据资产数值，也不要求重新摆通知或攻击窗口。199从普通Falling切为现有保护飞行曲线驱动，保持4826/1190/0.38；碰地只裁位移，末端再实查地面，避免旧Action42约0.033s即落地退出。

重开编辑器使用新DLL，在现有场地录制以下短回归：

1. 地面平视或略俯视释放，不命中木桩：应持续冲刺约0.38s，不刚起步就停。尾端贴地则回地面，不额外向上弹起或留飞行状态。
2. 仰视释放后自然未命中：199正常衔接206下落。不中途掉回Idle，也不残留高速水平惯性。
3. 贴地/空中前冲命中攻击窗口：仍经真实伤害成功进入共享189舞踏并召回猎虫，不能因为用了保护曲线而失去命中分支。
4. 朝墙冲刺及强制取消：不能穿墙、补追受阻位移或永久悬停，取消后旧租约不再写入新动作。

Development Editor已编译成功。本轮定向自动化覆盖觉虫击、操虫斩、强化操虫穿刺、急袭突刺和公共Movement，**79/79通过**（31项无测试警告、48项带测试警告，0失败、0未运行）。最终报告`Saved/M6Audit/AwakenedFlight20261012AcceptedCodeTests/index.json`，日志`Saved/Logs/AwakenedFlight20261012-accepted-code-tests.log`。真实CMC测试覆盖空中自然完成、取消、贴地平视、贴地向下、撞墙、同Action199→189准备段租约交接及地面舞踏/次数清理；另验证Hermite位置/速度和垂直基退化。E6.3与M6总验收保持待PIE，自动化不替代目视签收。

### 3.13 历史：角度阈值补丁与裁段已撤回（2026-10-12）

**该节只保留历史记录。当前参数、完整落地和复验改看§3.14；不要再配置−50°阈值或裁0.30s。**

最新原作样本的水平位移约532/539cm，水平速率约1445/1460cm/s，199编号仍约0.375s。上一轮把贴地也按自由飞行速率处理，并只回Walking、没有请求落地表现，不能还原此情况。

已增加贴地实查＋松键冻结视角的短程分支。`DA_IG_Combat`的`AwakenedGroundedDownwardSpeed`默认1450cm/s、`AwakenedGroundedDownwardPitchThreshold`默认−50°；这是局部近似阈值，原作精确条件未测明。空中同角度不减速、仰视仍用4826/1190模型。未改变镜头/准心/通知/伤害窗、猎虫射速或189参数。

已生成并连接`/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi_Landing`，使用`AS_Unsh_FallDown_Walk_Medium`（原作78），按18条录制约0.291～0.300s的阶段时长取源序列前0.60s（RateScale=2），有效0.30s，而非完整0.75s。`DefaultSlot`、`Landing78` Section、Action轨道。由Host托管Root Motion隔离、落地速度及结束释放，不是临时拼Montage。生成只使用`-run=MHGZM6AssetSetup -LandingOnly -Apply`；复查用`-LandingOnly -AuditOnly`，不得直接重跑完整`-Apply`。仅允许迁移未配置通知的初始全长落地占位；其他用户编辑布局保留并报错。本轮仅写新落地资产和CombatConfig的该引用。

重开编辑器载入新DLL，无需用户接线或摆通知。短PIE：

1. 平地把视角压到最低，不命中木桩：199前进应约5～6m，然后明确播放落地动作，不能滑约18m后突然Idle。
2. 平地仰视/平视各一次，保证没有全方向统一变短。日志`[AwakenedKinsect] 199 profile`可核对`groundedDownward`、释放Pitch与水平速度。
3. 空中最低俯视一次：不误套贴地短程；真实伤害成功仍进舞踏，未命中保留206。
4. 落地动作结束后继续移动/再出招；取消、同Action命中接管及其他动作的通用落地不应被专用蒙太奇覆盖。

本轮Development Editor编译、落地资产Apply/AuditOnly已通过，AuditOnly `changed=0`。最终离屏定向回归 **84/84通过**（35项无测试警告、49项带夹具警告，0失败、0未运行），报告`Saved/M6Audit/AwakenedGround20261012AcceptedTests/index.json`，日志`Saved/Logs/AwakenedGround20261012-accepted-tests.log`；覆盖短程500～600cm、实际0.30s常驻78落地播放、地面清舞踏/次数、空中同角度不减速且完整方向不变、取消与同Action交接，并包含共享落地回归。首次NullRHI尝试在既有`Landing203PointerMigration`虚拟窗口夹具中止，未完成当前新测试，不计作通过证据；随后改离屏渲染通过。参数回读为`Saved/M6Audit/awakened-ground-asset-values.json`，其中关联CombatConfig和落地Montage **DataValidation 2/2有效、0错误、0警告**，最终校验日志`Saved/Logs/AwakenedGround20261012-final-asset-validation.log`。E6.3与M6总签收继续保持待PIE，不用旧79/79签收当前行为。

### 3.14 地面目标连续路程与完整落地（2026-10-12）

用户要求：不能用最低角度特判；本项目落地序列与原作不同，必须恢复完整自然播放。已删除`AwakenedGroundedDownwardPitchThreshold/Speed`，按真实地面目标选择连续路程模型，不改主Montage、用户通知和攻击窗。

**配置与算法：** 起飞实查地面＋冻结快照阻挡命中可行走地面（排除怪物Hitzone）。路程为水平目标距离夹在550～1833.88cm之间，水平速度为路程/0.38；竖直初速140cm/s、重力1190cm/s²，名义抬升约8.24cm。地面目标的猎人修正和起飞方位取有原±90°限制的冻结视角朝向，不因地面点在人物背后变成90°侧飞。猎虫仍保持原冻结目标解析。

`DA_IG_Combat`可调`AwakenedGroundTargetMinimumDistance`（550cm）与`AwakenedGroundTargetVerticalLaunchSpeed`（140cm/s）。**距离公式是项目还原策略，不是已确认原作公式**；录像可确认完整199阶段、低速近匀速水平运动及小抛弧，却没有原作准心命中点。真值/限制见[§7.2.3](../reference/操虫斩曲线.md#723-当前低角度策略地面落点连续路程与小抛弧2026-10-12)。不回读实时相机来追目标。

上一轮曾把`AM_IG_JueChongJi_Landing`恢复为78项目序列全长、有效0.75s；**该专用资产选择已由用户撤销**。当前落地统一`AM_IG_AerialLanding`，见§3.15。连续飞行曲线保持不变。

重开编辑器载入新DLL，不需接线。短PIE复验：

1. 平地最低视角未命中：约5～6m、小幅离地抛弧、明确完整落地，不能贴地滑18m或播到一半切Idle。
2. 缓慢改变俯视角，对近处/中距离/远处地面点各一次：路程应连续变化，不能在某角度跳变或方向侧甩。日志`199 profile`中的`groundTarget/targetDistance/travelDistance/horizontalSpeed/verticalSpeed`可判断实际模型。
3. 近处落点已因195前进变到人物背后：猎人仍沿修正后的前向飞，不侧飞/后飞；猎虫独立遵循原目标。
4. 平视/仰视、空中及真实怪物命中：不误套地面目标小抛弧，原206/189和其他动作的落地保持。

Development Editor编译与资产恢复已通过；两项关联资产DataValidation有效、0错误、0警告。离屏定向回归**84/84通过**（35无测试警告、49带夹具警告，0失败、0未运行），报告`Saved/M6Audit/AwakenedContinuous20261012Tests/index.json`，日志`Saved/Logs/AwakenedContinuous20261012-tests.log`。覆盖连续550/551/800/1200/上限路程、小抛弧、真实CMC的近/中/远地面和后方点、空中不误分类、完整0.75s资源、189交接及既有动作。启动阶段既有两条未命名Condition failed未归入测试条目，保留记录，不称整个项目无告警。当前观感/全部角度仍待用户PIE，E6.3与M6总验收未关闭。

### 3.15 所有普通空中动作统一落地（2026-10-12）

用户裁定：统一使用现有`/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding`；觉虫击低空199也不需要特殊落地动画。不生成、不裁剪、不改速、不更改该共享Montage的通知。

觉虫击199自然结束且实查地面后，结束自身动作，再调用无参数`Host.PlayAerialLandingPresentation()`；与撑杆跳、空回、操虫斩等共用`CombatConfig.GetAerialLandingMontage()`。删除`AwakenedHunterLandingMontage`字段和Host普通落地的覆盖参数。189/206真实落地也继续由共享Host管理；短程模型、相机、命中和用户通知均不变。新动作已接管时仍不允许迟到的落地回调覆盖它。

“普通落地”不包括强化操虫穿刺203、急袭突刺381等招式自身的地面攻击段；这些维持已验收的功能动作，不换成落地表现。自然普通落地保持通用资产的完整播放规则，未额外添加输入锁。

`MHGZM6AssetSetup -LandingOnly`现只校验/连接共享资产引用，不再创建78专用Montage；缺少已有共享资产就明确失败，无临时Montage回退。Apply只保存CombatConfig、清理退役专用字段的序列化引用，AuditOnly不写资产。

复验重点：觉虫击最低/中间视角结束确实播放`AM_IG_AerialLanding`，以及空回/撑杆跳等普通落地无变化。曲线观感仍按§3.14的近/中/远目标录制，不因落地统一而视为已签收运动。

本轮Development Editor编译、定向回归**84/84通过**（35无测试警告、49带夹具警告，0失败/未运行），报告`Saved/M6Audit/UnifiedLanding20261012Tests/index.json`，日志`Saved/Logs/UnifiedLanding20261012-tests.log`。`DA_IG_Combat`与共享落地两项DataValidation均有效、0错误/警告；回读共享资产有效长约0.583333s、低空参数仍550/140，退役字段不存在，旧专用资产的包引用为空，见`Saved/M6Audit/unified-landing-asset-values.json`。共享落地与觉虫击主Montage的文件SHA256均与操作前一致。旧专用资产暂保留在磁盘供回退，不参与运行，不会被生成器再次连接；E6.3/M6仍不因本轮自动化而总签收。

### 3.16 觉虫击擦击漏判：受击表面覆盖修复（2026-10-12）

源录制`20261012-193350-BP_IG_Character_C_0-56417`：8次199均有Body接触，共41条；接触位于有效攻击窗口内，但没有有效武器候选。旧柱面/Body半径75cm，三枚部位球仅56.25cm且上下相切，表面与部位接缝未完整覆盖。当前统一要求见[总规划§1.6](../design/demo-implementation-plan.md#16-全项目命中覆盖硬约束2026-10-12)。

**修复范围：** 通用部位查询新增Sphere/Box选择。盒形使用唯一查询子组件，原球碰撞禁用；攻击、猎虫、准心、虫印、粉尘和肉质结算经统一Resolve解析部位，保留原始查询HitResult。已有球形资产/测试不改形状；重配与销毁同步清理子组件，不留下隐藏碰撞体。

`DA_TrainingDummy`三段已改Box，基础半尺寸51×51×38.5cm，运行时76.5×76.5×57.75cm，完整覆盖直径150cm、高337.5cm柱面及实体Body；接缝小幅外扩，三色、肉质、中心和火圈不变。旧“不允许部位相交”校验退役，保留三色、有效形状/尺寸校验和攻击跳去重。盒形木桩还在运行时限制最小半尺寸，避免调小后再次缩入柱面。

自动迁移脚本`Scripts/AssetProbe/configure_training_dummy_hit_coverage.py`：`-CoverageApply`只改三段Shape/BoxHalfExtent并备份原DA；不带该参数为只读回读/验证。原资产备份位于`Saved/AssetBackups/HitzoneCoverage20261012/DA_TrainingDummy.uasset`。不重新生成Montage或更改用户AttackCollision通知；觉虫击仍用IG_RearTip→Root/14cm扫掠及成功伤害→189舞踏条件，没有新增身体碰撞自动命中或改动飞行曲线。

**最终静态验证：** Development Editor编译成功（`Saved/Logs/HitzoneCoverage20261012-runtime-fixture-build.log`）。M2/M3/M5/M6定向集合**164/164通过**：M2 28、M3 10、M5 77、M6 49；54项无测试警告、110项含既有夹具警告，0失败/未运行。最终报告`Saved/M6Audit/HitzoneCoverage20261012VerifiedTests/index.json`，日志`Saved/Logs/HitzoneCoverage20261012-verified-tests.log`。新增144个真实木桩表面点、三段颜色、五种14cm擦击、重配清理、旋转/非均匀缩放及实际盒形命中伤害/携带精华检查均通过；有限生命测试目标从1000扣至998。

旧Mark夹具补了生产BeginPlay，并以实际网格面/法线独立查询代替理想圆柱假设；旧粉尘数值夹具显式选有限生命/50cm测试半径，不改变生产无限木桩和150cm爆炸半径。最终DA只读回读`changed=false`，DataValidation 1/1有效、0错误/警告，报告`Saved/M6Audit/hitzone-coverage-audit.json`。觉虫击主Montage SHA256仍为`2F0447EC6B7518B0FED134DA2A84EFEC207DB6A1BB15BC24FE83564A4199CC53`，通知和窗口未修改。未运行全项目打包或全Content验证。

**PIE仍待本次修复复验：** 重开编辑器加载新模块，无需重新接线。正面、侧面及三段接缝各尝试觉虫击擦击，确认真实武器命中后进入舞踏；再测普通放虫采集三色、虫印施加/自动寻印、粉尘聚爆与召回。仅身体擦过、武器未接触的情况仍允许未命中；墙体不应触发舞踏。该修复不自动签收E6.3/M6。

### 3.17 强化操虫穿刺/急袭突刺地面收尾翻滚交接（2026-10-12）

用户最新录制确认前一轮命中相关问题已解决；新增问题为强化操虫穿刺地面收尾的直接A输入取消攻击却没有翻滚。录制 `20261012-215612-BP_IG_Character_C_0-56411` 有8次同类失败。急袭突刺专用Over回调也有同一缺口，反例测试已复现。

**修复合同：** 普通攻击与两招地面MontageTask共享 `HasPreparedDodgeSupersede()`。合法精确DodgeAcceptWindow已通过、翻滚接管处于Prepare→Commit之间时，旧Montage的同步中断不得提前结束来源GA或清空任务；成功提交才按Superseded清理。替换失败仍回滚，真正外部中断仍结束来源。窗口外A仍拒绝直接激活，保留原有可缓存输入策略；窗口、通知、预输入时长、伤害、曲线及动画资产均未修改。

**验证：** 新增 `MHGZ.M5.Aerial.LandingDodgeHandoff.{KinsectPiercer,DescendingThrust}`，每项8种情形：前/左/右/后直接翻滚、预输入、窗口外拒绝、真实播放后失败回滚、外部中断。夹具仅直接建立地面相位；落地任务、Root Motion所有权、结束回调、Coordinator及翻滚走生产代码，读取现有Over/GA_Dodge，不替代完整空中链的PIE。反例 `Saved/M6Audit/LandingDodgeHandoff20261012NegativeControl/index.json` 两项均失败，抓到翻滚未存活和来源误判Interrupted。最终 `Saved/M6Audit/LandingDodgeHandoff20261012Verified/index.json` **174/174通过**（54无测试警告、120带夹具警告，0失败/未运行），覆盖M2/M3/M5/M6及M4翻滚/预输入。构建成功见 `Saved/Logs/LandingDodgeHandoff20261012-final-build.log`；初轮夹具未正确建立来源的失败不作为生产bug证据。

**短PIE（无需接线/改通知）：** 重开编辑器加载最终模块。两招各在正常落地收尾的已开放翻滚窗口直接按A，确认来源被翻滚接管而非双双取消；再各提前按一次A，确认预输入正常。无需人为卡精确帧，重试几次并顺带左右方向即可。窗口前且超出缓存有效期的输入不应强行打断。此修复不自动签收E6.3/M6，也不重做整个M5矩阵。

### 3.18 四连印斩与觉虫击冲刺建印（2026-10-13）

**当前配置与代码：** `GA_IG_SiLianYinZhan` 已继承 `UMHGZMarkSlashAbility`，`MarkSegmentIndex=1`，不需要重新接线。用户确认“第二下”指GA第二个伤害段，即 `AM_IG_SiLianYinZhan` 约1.033333～1.283333s的ConfigIndex=1；前两个ConfigIndex=0窗口保持不变。觉虫击仅猎人199冲刺ConfigIndex=0的有效武器伤害成功后调用同一 `SetKinsectMarkFromMeleeHit`，然后保留既有下一帧189舞踏交接；普通与三灯分支均建印，猎虫自身的飞行攻击不是此来源。伤害失败、起飞前和重复成功回调不刷新虫印。建印失败不阻断原舞踏逻辑。

**验证：** `MHGZ.M6.Mark.FourHitOnlySecondSuccessfulDamageEstablishesMark` 验证四连指定段、失败GE、其他段、重复回调与下一次激活替换；`MHGZ.M6.AwakenedKinsect.HunterFlightSuccessfulDamageEstablishesMark` 用现有常驻觉虫击Montage验证普通/三灯分支、起飞前、失败GE、有效冲刺与重复回调。目标使用项目实际 `BP_TrainingDummy` 及真实GE/Hitzone链路；动画孤立夹具不代替完整PIE。最终 `Saved/M6Audit/AdditionalMarkSources20261013Verified/index.json` **176/176通过**（54无测试警告、122带测试警告，0失败/未运行）；Development Editor构建成功见 `Saved/Logs/AdditionalMarkSources20261013-final-build.log`。只读审计 `Saved/M6Audit/AdditionalMarkSources20261013-asset-audit.json` 成功回读两个GA父类、段索引和Montage窗口；本轮未写入GA/Montage资产。

**短PIE（重开编辑器，无需改通知）：** 四连印斩命中木桩，确认GA段1成功伤害时出现虫印，段0单独命中不提前建印；普通与三灯觉虫击分别让猎人冲刺命中，确认虫印出现且原舞踏仍正常。空挥不生成新印，换位置再次命中只保留新印。本次仅验新增来源，保留E6.2历史签收，不自动签收E6.3/M6。

### 3.19 强化觉虫击多色携带与回手交付（2026-10-13）

**原因与现行合同：** 旧强化请求 `ApplyPerValidHit` 在成功贯通伤害后直接给猎人ApplyExtract，所以不是UI误显示。现改为 `CollectPerValidHit`，按真实部位颜色收集不同的红/白/橙精华，重复色不加格数、不覆盖前色；伤害失败不采集。贯通过程仍逐击伤害/产粉，角色灯保持未交付状态。猎虫召回或中断不丢携带，真正到手臂后先取出/清空整个容器再逐色交付，三色可在此时形成新三灯；交付时已有三灯仍吞灯、不刷新。

**UI用途：** 独立竖立椭圆读取猎虫实际携带集合，一色全填、两色二分、三色三分，回手后清空；编号菱形显示猎人已有灯。普通猎虫/操虫斩仍只携带一种。双色能力也预留给未来可携带两种精华的猎虫，不代表本Demo普通容量已扩展。现有Widget原生绘制已支持多色，无需生成资产或接线。

**验证：** Development Editor构建成功，`Saved/Logs/AwakenedCargo20261013-final-build.log`。最终相关回归 `Saved/M6Audit/AwakenedCargo20261013Verified/index.json` **177/177通过**（54无测试警告、123带测试警告，0失败/未运行）。新增 `PiercingCargoDefersOneTwoThreeColorsUntilReturn` 使用真实GE/Sweep/Return测试一/二/三色途中不点灯、同色去重、普通捕获不扩容、中断保留、回手交付/清空及三色仅此时形成三灯；原失败伤害/产粉测试改为验证采集失败无颜色、成功伤害保留产粉。同组含真实WBP的一/二/三色D3D12离屏绘制测试；静态通过不代替PIE目视。

**短PIE：** 重开编辑器，先普通放虫采集/召回一次，确认仍单色。取得三灯后发动强化觉虫击，让猎虫穿过不同颜色部位：飞行期间只变携带椭圆，不提前亮猎人灯；回手后椭圆清空、相应灯亮，完整三色则形成三灯。重复同色仍只一格且贯通伤害/粉尘继续；顺带操虫斩单色回手。无须卡精确按键帧。保留既有E6.1/E6.2签收，当前追加修复仍待PIE，不自动关闭M6。

## 4. E6.4：伤害数字（本轮唯一范围）

### 4.1 数字

已按既有伤害路由生成并接线三份常驻资产（2026-10-13）；无需手动新建或重接线：

| 资产路径 | 父类/作用 |
|---|---|
| `/Game/UI/Feedback/WBP_DamageNumber` | `MHGZDamageNumberWidget`，Designer树有绑定的`DamageText`；原生`SetDamageDisplay`按最终伤害格式化整数，不必在BP重写事件 |
| `/Game/Blueprints/Feedback/BP_DamageNumberActor` | `MHGZDamageNumberActor`，WidgetClass已接线；Screen空间，无碰撞/物理，总寿命1s，前0.75s不动，末0.25s以60cm/s上飘并淡出后销毁 |
| `/Game/GameplayCues/GCN_Hit_DamageNumber` | `MHGZDamageNumberCue`，ActorClass已接线，Tag=`GameplayCue.Hit.DamageNumber`，目录发现唯一处理器 |

普通数字浅色20号、暴击金色24号，均有黑描边。颜色/字号在WBP Class Defaults可调；Actor Class Defaults的`DisplayLifetime`为总寿命，`FadeOutDuration`为包含在总寿命内的末段淡出时长，`RiseSpeed`仅用于末段上飘。当前1s总寿命/0.25s淡出，即前0.75s原地且不透明、末0.25s上飘并逐渐透明，之后销毁。数字只显示最终伤害及已结算暴击，不执行DamageGE；0伤害不出数。聚爆两段分别出数并小幅错位，不合成一笔；不在HUD/木桩/GA重复Spawn。

脚本：`-run=MHGZE64DamageNumberSetup`默认只读审计；`-Apply`只创建/连接上述三资产，不触碰已签收HUD/动画/资源配置，不重建已有Designer树。资产创建与只读回读结果保存到`Saved/M6Audit/e64_damage_number_assets.json`。

### 4.2 其他视觉Cue（延期，不是本轮步骤）

下表保留未来方案；除已完成接线的伤害数字Cue外，其他视觉Cue均不要求本轮生成/签收，也不作为E6.4数字或本Demo门禁。不得为了完成数字顺带创建音效/命中特效。

放 `/Game/GameplayCues/`，最终资源命名 `GCN_*`，**GameplayCueTag在Class Defaults显式设置**：

| 资产示例 | GameplayCueTag | 最小内容 |
|---|---|---|
| `GCN_Hit_Slash` | GameplayCue.Hit.Slash | 斩击命中视觉 |
| `GCN_Hit_Kinsect` | GameplayCue.Hit.Kinsect | 小号猎虫命中视觉 |
| `GCN_Hit_Crit` | GameplayCue.Hit.Crit | 暴击视觉 |
| `GCN_Hit_DamageNumber` | GameplayCue.Hit.DamageNumber | 原生MHGZDamageNumberCue的数据子类 |
| `GCN_IG_ExtractGained` | GameplayCue.IG.ExtractGained | 精华吸收提示 |
| `GCN_IG_TripleUpActivated` | GameplayCue.IG.TripleUpActivated | 三灯形成提示 |
| `GCN_IG_ExtractExpired` | GameplayCue.IG.ExtractExpired | 自然到期提示 |
| `GCN_Character_Dodge` | GameplayCue.Character.Dodge | 闪避启动视觉 |
| `GCN_IG_PowderExplosion` | GameplayCue.IG.PowderExplosion（M6新增） | 一次聚合爆炸视觉 |
| `GCN_Hit_IG_Powder` | GameplayCue.Hit.IG.Powder（M6新增） | 单目标粉尘命中视觉 |

一次性简单Cue可继承GameplayCueNotify_Static或本包原生视觉类，OnExecute只生成视觉，使用Parameters.Location/Normal/EffectContext。选择Actor型Cue时必须有有限时长自动结束；不能留下无限常驻实例。

1. 在Class Defaults填正确Tag，Compile/Save。
2. 确认只有一份Cue处理同一Tag，避免双数字；文件名含Tag不等于Class Defaults已经有Tag。
3. 若使用Niagara，只在代码接口/模块依赖已实现后赋System；临时Mesh/粒子可先完成闭环。
4. Sound字段留空，不添加新声音播放节点；已有别处声音不删除。
5. 不创建降龙Cue，不因为Config里保留旧Tag就扩展当前Demo范围。

### 4.3 PIE

重开编辑器加载模块，在PIE中用普通武器、猎虫和粉尘伤害击中木桩：数字更小且可见，前段原地停留、仅末段上飘并渐隐，总共约1s消失；同一伤害阶段/目标只一份，合法多段各一份。聚爆保持两段独立数字，0团无数字；暴击标志只取已结算结果。木桩为无限血量目标，应核对Combat最终伤害与UI事件，不以不变Health差值判数字错误；有限血量目标才对实际扣血。本轮不要求其他特效/声音，也不重验已签收1、2项。

### 4.4 实施与静态证据（2026-10-13）

**最新字号/消失效果调整：** 普通24→20号、暴击28→24号；前0.75s原地不透明、仅末0.25s上飘并线性淡出，总寿命仍1s。`FadeOutDuration`已暴露到Actor Class Defaults；跨过淡出起点的帧只积分末段时间，不会将整帧位移提前加入。已有蓝图只读回读实际值为20/24、1s、0.25s、60cm/s，无需重建/接线。构建`E64NumberFade20261013-final-build.log`成功；最终报告`Saved/M6Audit/E64NumberFade20261013FinalVerified/index.json` **179/179通过**（54无测试警告、125带测试警告，0失败/未运行），实测前段不动/不透明、末段渐隐/上飘、到期销毁与真实WBP新字号。初轮时序断言因约1e-7浮点误差失败，已使用0.0001容差，不修改生产时序迎合测试。下方首轮E64数字报告为调整前基线，本包PIE仍待确认。

Development Editor构建成功：`Saved/Logs/E64DamageNumber20261013-final-build.log`。`-Apply`生成上述三资产；新进程默认AuditOnly回读文本绑定/父类/类引用/唯一数字Cue成功：`Saved/M6Audit/e64_damage_number_assets.json`，日志`E64DamageNumber20261013-audit.log`。不修改已有HUD、GA、Montage、伤害配置或相机。

相关回归`Saved/M6Audit/E64DamageNumber20261013Verified/index.json` **179/179通过**（54无测试警告、125带测试警告，0失败/未运行）。新增 `MHGZ.M6.UI.DamageNumber.RealCueRouteAndLifetime` 走真实ASC→注册Cue→数字Actor→常驻WBP，验证最终值/整数格式、暴击标志、独立阶段错位、0伤害不出数、显示不扣血、上飘和1s销毁；`ActualWidgetRender`实绘普通/暴击文本。预览`Saved/M6Audit/E64DamageNumberPreview/{normal,critical}.png`已检查可见文本/描边。静态通过不代替§4.3真实PIE，E6.4当前为“实现和接线完成，待PIE签收”。

UE DataValidation已完成（落盘资产、含新增数字三资产），进程退出0、0错误/0警告，日志`Saved/Logs/E64DamageNumber20261013-validation.log`。这是本包资产安全检查，不代替E6.5综合PIE或M7独立打包验收。

## 5. E6.5：总验收与问题提交

入口：M6.5构建/自动化/阶段资产审计通过，E6.1～E6.4已逐包确认。

1. 开始现有Telemetry，正常操作覆盖[M6固定矩阵](../archive/demo-2026-10-13/m6-plan.md#113-e65-固定矩阵)。
2. 记录目視问题对应的大致动作/时间，不需要人为精确到帧。
3. 停止捕获并确认文件有行数；由AI核账Combat/Events、UI/Events及原有Input/Character/Animation。
4. 做最小M5回归：收拔刀/翻滚、普通放收虫、空回→操虫斩/急袭突刺、落地与再出手，不重开所有旧测试。
5. 结果写回M6计划及总体门禁：通过的是当前裁定的Demo范围；其他视觉Cue/音效延期，普通回手已签收，不再列欠项。

用户交接只需：本次会话路径、目視是否异常、有异常的大致动作；文件/日志可读取时不要求把全部内容手工复制到对话。

> **当前门禁（2026-10-13）：** 收尾第1/2项及E6.3已由用户确认完成。E6.4仅伤害数字已实现/接线并通过179/179相关回归及只读资产审计，待§4.3短PIE确认；其他视觉Cue和音效延期且不阻塞。E6.4签收后才进入第4项M6.5/E6.5综合验收，M6总验收尚未关闭，不重新展开已签收动作矩阵。

**既有运行证据：** E6.1用户已签收，当前不重新展开其矩阵。转向回调、三灯选择/消耗和HunterFlight启动的记录 `Saved/RuntimeTelemetry/20261007-210436-BP_IG_Character_C_0-56055` 与 `Saved/Logs/MHGZ.log` 作为历史证据保留；用户已确认1.3/1.4/1.6资产接线，第230帧通知无需重放置。历史Telemetry缺逐次HitID/CameraYaw等字段的限制仍适用于定量复核，不撤销用户对E6.1整体验收的签收。后续只在对应生产代码/资产改变时做定向回归。
