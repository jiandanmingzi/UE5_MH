# E6：觉虫击、粉尘、HUD 与视觉反馈编辑器指导（不含音效）

> **2026-10-06，指导文档，尚未执行。** 必须先完成并冷编译对应[M6代码工作包](../using/m6-plan.md)。本文中的新C++类/属性/Notify目前属于待实现方案；在代码包出口前搜不到这些项目是正常的，不要用错误父类占位。
>
> 本轮不获取、创建、配置或验收音效。现有 `DA_IG_Combat` Sound 字段保留，不能批量清空。阶段状态见[门禁表](../design/milestone-gates.md)。

## 0. 通用操作约定

1. 本文 Content 路径采用 `/Game/...`，即编辑器内容浏览器中的项目 Content；`Source/...` 是工作区C++文件，不是资源目录。
2. 优先让本轮资产生成脚本/commandlet创建并赋值，之后只读审计，再进行本页目視核对。工具尚未实现时，不声称其命令可运行。
3. 外部commandlet写资产前保存并关闭编辑器；使用编辑器内部接口时也先确认无未保存冲突。不要外部写后又保存编辑器的旧版本。
4. 新资产沿用最终类、稳定命名、相同Skeleton；有同名资产先检查父类/引用，不随意Duplicate出 `_New`、`_Test`。
5. 编译/保存每个改变的蓝图；Montage通知、轨道和Section由C++只读审计回读，不用Python“发现0条”签收。
6. 实际通知帧由资源/录制证据核对；秒数、录制采样行号、UE序列帧不是一回事。不得为了调混合改变伤害/移动时间。

## 1. E6.1：觉虫击与通用粉尘

### 1.1 入口检查

M6.1/M6.2已编译；成本结算、PowderActor、Awakened GA、精确点通知、运动参数与下落交接专项通过。资产审计已确认193/195/196/199/206源序列、真实倍率及本招曲线。

若源动画速度仍只有原作一半：由脚本先烘焙真正2x版并保留Root曲线/正确轴向；不要仅在最终Montage把RateScale改2，也不旋转Skeleton“纠正”Root朝向。

### 1.2 通用粉尘资产

拟最终资产 `BP_IG_PowderCloud`，父类 **IGPowderActor**，目录见M6.1审计后的Actor目录；不能选普通Actor后在BP实现权威状态。

1. 打开Class Defaults确认父类为 `AIGPowderActor`。
2. 增加/配置仅表现组件：可先用一个小球Mesh、半透明/发光材质或粒子。临时造型即可，不需要原作音效。
3. 表现Mesh Collision Preset=`NoCollision`、Simulate Physics=false；任何组件不Block Pawn/Visibility/Hitzone。
4. 权威SpawnTime/Lifetime、OwningResource、Available/Reserved/Consumed由C++初始化。不要在EventTick筛选玩家，不绑定Overlap爆炸，不在BP ApplyDamage/GameplayEffect。
5. 有状态显示事件时只改变颜色/透明度；Reserved可高亮，Consumed/到期由C++收尾。
6. 保存。将最终Class填入 `DA_IG_Combat.PowderActorClass`，按本包参数表填Lifetime、每团贡献和集约参数；参数单位：距离cm、时间秒、数量整数。

`PS_IG_MarkYellowPowder`可供审美参考，但现有虫印视觉与本粉尘的可消费状态完全不同，不能把虫印Actor当粉尘Actor。

### 1.3 常驻Montage核对

在 `/Game/Weapons/InsectGlaive/Anims/Montage/` 核对生成的：

- `AM_IG_AwakenedKinsectAttack`：193 → 195 → 196 → 199。
- `AM_IG_AwakenedKinsectFall`：206表现，下落物理由C++管理。

操作：

1. 双击Montage，确认Skeleton与现有虫棍相同。
2. Slot=`DefaultGroup.DefaultSlot`，这是全身功能动作，不是送/收虫的上半身Slot。
3. 确认Sections为 `Startup193`、`Send195`、`PreFlight196`、`HunterFlight199`，对应各段起点；正常链依次前进，不接回第一段，不自动增加194循环。
4. 最终段使用烘焙正常速度序列、Segment PlayRate=1；播放预览核对手指/武器/脚/Root不跳变，无末帧T字形。
5. HunterMovement由任务驱动时，动画不能同时贡献胶囊Root Motion；按现有表现盾/IP生成方案核对，不能只观察人物看似移动。
6. 保留Enable Auto Blend Out；Blend In/Out沿用本项目当前动作基线，记录实际值，不全局一键改为0。
7. 下落蒙太奇只能表现姿势；是否保持末帧/循环由本包审计确认，不自行设置一个固定0.5s后回Idle。

### 1.4 通知轨道与放置

遵守现有语义轨道：

| 轨道 | 通知 | 核对点 |
|---|---|---|
| `Kinsect` | `IG Awakened Kinsect Event`，Event=KinsectLaunch | 丢虫动作的实际离手点，一次 |
| `Action` | 同类点通知，Event=HunterFlightStart | 进入猎人功能位移的审计位置，一次 |
| `IGAerialHandoff` | 现有 `IG Aerial Handoff` | 只有有真值支持的可操作空中释放点 |
| `Combo` / `Dodge` | 现有Window（仅本包明确允许时） | 不默认整段可以被任意攻击/翻滚打断 |

添加的是**点通知**，不是AnimNotifyState窗口。在对应通知轨道右键 → Add Notify → 选择已编译类；选中通知后在Details填事件枚举。点通知不需要拉持续长度。

本招伤害来自猎虫飞行，无额外武器段时**不创建Attack Collision窗口**。别用一个武器碰撞窗模拟多次贯通，也别放两个通知与一个0.4sTimer同时启动猎人位移。

通知的精确时间由脚本审计输出并核对；当前文档不杜撰帧号。放置后保存并运行本轮只读审计，检查事件数量、时间、轨道、所属Section及精确解析条件。

### 1.5 GA Defaults

`/Game/Blueprints/Ability/InsectGlaive/GA_IG_AwakenedKinsectAttack` 父类 **MHGZAwakenedKinsectAttackAbility**。

| 配置 | 填写 |
|---|---|
| Attack Montage | `AM_IG_AwakenedKinsectAttack` |
| Default Entry Section | `Startup193` |
| Attack Segments | 空数组；猎人未定义武器伤害 |
| Max Correction Angle | 0，禁用通用摇杆入招修正 |
| Weapon Resource Costs | 1项：CostType=`Cost.IG.TripleUp`、Amount=1 |
| 下落/运动配置 | 本招审核过的最终引用，不复制操虫斩参数 |
| Required Tags | Grounded、Unsheathed、TripleUp；实际完整名按M6计划 |
| Blocked Tags | Attacking/Dodging/Sheathing/Hitstun/Knockdown/Dead/Aerial完整Tag，见M6计划§5.1；不把空中CantAttack当通用地面锁 |

“标签”栏的Required/Blocked与Combo边是两层门槛，都不能取代C++真实Handle/猎虫/Runtime检查。不要在EventGraph再次扣三灯或调用DeployKinsect。

### 1.6 InputProfile与Combo

先用脚本导出当前两张DA，不覆盖旧数组。仅新增/修正本轮条目：

1. `DA_IG_InputProfile.Chords` 的RTYB：OutputTag=`Input.Weapon.RTYB`；TriggerControls=**现有Y/B物理映射值**；RequiredHeldModifiers=`Input.Modifier.RT`；ExactModifiers=true；AimSnapshotContext=Action；ConsumeTriggerControls=true。保持现有时限与优先级规则，较完整组合必须胜过RTY/RTB。
2. `DA_IG_Combo.Transitions` 新边：TransitionID=`Idle_RTYB_AwakenedKinsectAttack`；SourceState=`Idle`；InputTag=`Input.Weapon.RTYB`；AbilityClass=本GA。
3. ExecutionPolicy=ActivateAbility；StatePolicy=Replace；TargetState=`IG.Special.AwakenedKinsectAttack`（**FName状态，不是GameplayTag**）。
4. RequiredTags为Grounded/Unsheathed/TripleUp；MaxCorrectionAngle=0；bMatchAnyState=false；bAutoTransition=false；初始Idle边bRequiresComboWindow=false。
5. 不新增任意攻击状态全局入口；不在CoreAbilities再授予这招。
6. 保存 → 只读审计 → PIE确认一次组合不会连发其他GA。

### 1.7 PIE最小验收

使用现有三色木桩和正常获得三灯的已实现途径；如果普通回手尚难验收，可使用已签收操虫斩即时点灯路径，不临时写UI加Tag。自动化负责人工难以摆出的到期/失败边界。

- 没三灯RTYB：失败且后续输入正常。
- 有三灯空放：只消费一次，猎虫返回，猎人完整执行并正常落地/再操作。
- 穿过有效部位：逐击伤害、精华、粉尘计数对应，可重新形成三灯且后续不刷新。
- 普通武器/猎虫触碰粉尘不爆炸。
- 镜头/摇杆在按出后变化，不改变被冻结的本招方向；超角Clamp而不是无法触发。
- 记录正常速度和必要的慢放观感；运动/计数以Telemetry分析为准。

## 2. E6.2：粉尘集约

### 2.1 动画与Montage

入口：M6.3冷编译/审计通过，集约的真实动画候选已确认；不能拿一段无关动画充当正式签收素材。

1. 核对常驻 `AM_IG_PowderVortex`，Slot=`DefaultGroup.DefaultSlot`，保持同Skeleton。
2. 在 `Action` 通知轨道放**点通知** `IG Powder Gather Begin`，位置为实际开始聚拢的动作点。
3. 确认此点到Montage有效末端至少有完整PowderGatherDuration；不设GatherBegin后立即结束或循环。
4. 本招无猎人武器段，无Attack Collision窗口；爆炸由粉尘事务完成产生，不放多条“爆炸伤害”Notify。
5. 保存、审计通知唯一性与聚拢时间覆盖。

### 2.2 GA与DA

GA路径 `/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex`；父类 **MHGZPowderVortexAbility**。

- AttackMontage=`AM_IG_PowderVortex`；DefaultEntrySection填该Montage的实际首Section。
- AttackSegments空；WeaponResourceCosts空；MaxCorrectionAngle=0。
- RequiredTags=Grounded/Unsheathed；不要求三灯；BlockedTags沿用合法动作锁。
- CombatConfig里分清GatherRadius与ExplosionRadius。600cm/0.5s/5是已有集约默认值，不是原作验收结论；新Lifetime/贡献/基础爆炸伤害须显式填入。

LTYB Chord：Trigger=现有Y/B物理Tag，Modifier=`Input.Modifier.LT`，AimSnapshotContext=None，Output=`Input.Weapon.LTYB`；组合获胜消费Y/B。

新增Combo边：`Idle_LTYB_PowderVortex`、Source=Idle、Target=`IG.Special.PowderVortex`、AbilityClass=本GA、Execution=ActivateAbility、State=Replace、Required=Grounded/Unsheathed、Auto=false、MatchAny=false、Idle入口RequiresComboWindow=false。不创建空中降龙边。

### 2.3 PIE

先E6.1产几团粉尘，再正常LTYB：

- 无粉尘/无猎虫不启动；有粉尘按本玩家、距离、上限聚拢。
- 聚拢完成只一个爆炸，多部位木桩只一跳总伤害。
- 使用本轮自动化/调试取消入口验证取消，不需要为了测试编写空中受击/死亡玩法。
- 取消不炸，仍存活粉尘恢复原位；过期粉尘不复活。
- 连续重复输入不重用同团粉尘、不重复爆炸。

## 3. E6.3：主HUD、条与资源面板

### 3.1 创建最终资产

M6.4编译后，在 `/Game/UI/` 创建/核对：

| 资产 | 父类 |
|---|---|
| `BP_MHGZHUD` | MHGZHUD（Actor蓝图，不是Widget） |
| `WBP_HUD` | MHGZMainHUDWidget |
| `WBP_HealthBar`、`WBP_StaminaBar` | MHGZAttributeBarWidget |
| `WBP_Crosshair` | MHGZCrosshairWidget |
| `WBP_IG_ResourcePanel` | MHGZInsectGlaiveResourceWidget |

Widget创建方法：Content Browser右键 → User Interface → Widget Blueprint → 在父类选择器找到本轮原生类；若先创建了默认UserWidget，Class Settings → Parent Class改为表中类并Compile。不能把WBP_HUD改成HUD Actor。

`WBP_IG_KinsectStamina`/`WBP_IG_ExtractDisplay`可作为面板内部设计子控件后续拆分；首版直接在ResourcePanel里放组件即可，不为完成旧概括清单额外制造第二个绑定/生命周期所有者。

### 3.2 WBP_HUD：四个容器

Designer操作：

1. Root放Canvas Panel。
2. 放四个Overlay，准确命名 **HealthBarSlot、StaminaBarSlot、CrosshairSlot、WeaponResourceSlot**，勾选Is Variable；对应C++ BindWidget必须拼写一致。
3. HealthBarSlot在左上，StaminaBarSlot在其下方，WeaponResourceSlot在资源区，CrosshairSlot锚点中心 `(0.5,0.5)`、Alignment `(0.5,0.5)`、Position `(0,0)`。
4. 四个Slot内不要在Designer再放同一WBP实例；实际子Widget由HUD创建并AddChild。
5. 不在Construct/EventTick里CreateWidget或AddToViewport，不调用GetPlayerCharacter→写Resource。
6. Root设不拦截游戏输入的可见性（Self Hit Test Invisible/Not Hit-Testable对应选项），不要让覆盖全屏的控件吞鼠标/手柄。
7. Compile/Save；缺BindWidget即检查名字和组件类型，不删掉代码校验。

### 3.3 血条与耐力条

分别打开WBP，Designer放ProgressBar（如 `ValueBar`）和TextBlock（如 `ValueText`），设Is Variable。

Class Defaults填写：

- HealthBar CurrentAttribute=MHGZAttributeSet.Health，MaxAttribute=MaxHealth。
- StaminaBar CurrentAttribute=Stamina，MaxAttribute=MaxStamina。

EventGraph：Override **On Value Updated**（参数Current、Max）。

```text
Max > 0 ? Clamp(Current / Max, 0, 1) : 0 → ValueBar.SetPercent
Format Text "{Current}/{Max}"           → ValueText.SetText
```

Format Text是将命名占位参数转换成Text的蓝图节点：右键搜索Format Text，在节点格式里输入上述字符串，出现Current/Max引脚后接事件参数；若不需要小数，先Round/转换为整数显示。不要在Percent/Text属性右侧点Create Binding逐帧取ASC。

C++负责Bind/初始同步/Unbind；BP这里**只显示**。测试修改Max值时条比例应立即更新，不只看Current变化。

### 3.4 虫棍资源面板

Designer至少放：

- 猎虫耐力ProgressBar、当前/最大Text、猎虫状态Text。
- Red/White/Orange三个Image或边框，各一个倒计时Text/进度条。
- Triple高亮/倒计时区域。
- DanceStacks整数Text；可另显示DanceSource用于调试。
- 可用/预留粉尘数量Text（首版调试提示，可后续隐藏）。

EventGraph Override **On Resource UI Updated**（M6.4目标事件，参数FIGResourceUISnapshot），Break该Struct：

1. KinsectStamina/Max → 安全比例SetPercent；不存在猎虫时显示不可用，不自行Spawn。
2. bRed/bWhite/bOrange → 各图标亮/暗；RemainingSeconds可Ceil显示秒数；Ratio→对应进度。
3. bTripleUp=true → 三色视觉高亮并显示TripleRemaining/Ratio；**不能调用ApplyExtract给三个灯补GE**。
4. bTripleUp=false → 按三个单灯实际状态显示。
5. DanceStacks→SetText；清零立即显示0；不要让Widget保存另一份舞踏计数。
6. 粉尘计数只显示；不在Widget按钮里Consume/Release。

没有EventTick/延迟等待Resource；Native Bind会立即推当前快照。不要在面板Construct重复绑定四个旧Resource委托，造成重复更新。

### 3.5 准心

Designer放中心Image/四个短线/简单符号，设置不拦截输入；不用原游戏截图素材也可先临时形状。

Override现有 **On Aim Target Updated(Target, HitzoneTag, ExtractColor)**：

- 合法Target且ExtractColor为红/橙/白对应Tag → 设置对应颜色。
- 无Target/无合法颜色 → 中性颜色。

Override M6.4新增显隐事件（如 **On Aim Visibility Updated(bool bVisible)**）：true→Visible/不命中测试，false→Hidden并重置中性。HUD/原生类已经决定Action/Kinsect语义，BP不复制整套Tag判断。

不在WBP自己LineTrace，不调用晚AimLock获取显示；准心只显示，输入快照由现有Router/AimComponent决定。

### 3.6 HUD与GameMode接线

1. 打开 `BP_MHGZHUD` → Class Defaults。
2. MainHUDClass=`WBP_HUD`；HealthBarClass=`WBP_HealthBar`；StaminaBarClass=`WBP_StaminaBar`；CrosshairClass=`WBP_Crosshair`。
3. 打开 `BP_Demo_GameMode` → Class Defaults → HUD Class=`BP_MHGZHUD`。
4. 确认 `L_DemoArena` World Settings实际GameMode与以上一致，不要改了未使用的GameMode。
5. 打开 `DA_WeaponRuntime_IG`，ResourceComponentClass保持原有效URes_InsectGlaive，ResourceWidgetClass由None改为`WBP_IG_ResourcePanel`。
6. Compile/Save → 阶段DataValidation → PIE。

在步骤5前Widget=None合法，不阻止觉虫击/粉尘的早期代码测试。填Widget后必须有合法Resource；禁止为成对配置把ResourceComponentClass也清掉。

### 3.7 HUD验收

- 开始PIE就显示正确值，不要先扣一次耐力/点一次灯才能显示。
- RT Action准心中性；LT Kinsect合法部位正确变色，墙遮挡恢复中性；结束瞄准立即隐藏/清色。
- 灯到期/三灯消费/重新三灯、猎虫耐力、落地舞踏清0即时更新。
- 第二次PIE仍只有一个HUD，UnPossess/重建不残留旧资源面板；日志/Telemetry证明旧绑定清理。
- UI不导致输入模式变为UIOnly、游戏暂停或Focus抢走游戏按键。

## 4. E6.4：伤害数字与视觉Cue

### 4.1 数字

M6.4原生类编译后：

1. 创建 `WBP_DamageNumber`（父类MHGZDamageNumberWidget），放TextBlock `DamageText`。
2. Override **Set Damage Display(ActualDamage,bCritical)**，Round/整数格式→SetText；暴击可换颜色/大小，不重新算伤害。
3. 创建 `BP_DamageNumberActor`（父类MHGZDamageNumberActor），选原生WidgetComponent，WidgetClass=`WBP_DamageNumber`，Space=Screen（避免观察方向影响可读性），Collision=NoCollision。
4. Class Defaults设置显示Lifetime/上飘参数；不要设SimulatePhysics，不在BP ApplyDamage。
5. 对应DamageNumber Cue设置ActorClass=`BP_DamageNumberActor`。不要在HUD/木桩/GA再Spawn另一份数字。

### 4.2 Cue资产

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

真实武器、猎虫、粉尘击中木桩：命中点正确，Health实际差值与数字一致，同一次伤害只一个数字；三灯形成只一次脉冲，自然到期与主动消费不混淆；粉尘集约一次世界爆炸，多目标各自产生一个合法数字。

## 5. E6.5：总验收与问题提交

入口：M6.5构建/自动化/阶段资产审计通过，E6.1～E6.4已逐包确认。

1. 开始现有Telemetry，正常操作覆盖[M6固定矩阵](../using/m6-plan.md#113-e65-固定矩阵)。
2. 记录目視问题对应的大致动作/时间，不需要人为精确到帧。
3. 停止捕获并确认文件有行数；由AI核账Combat/Events、UI/Events及原有Input/Character/Animation。
4. 做最小M5回归：收拔刀/翻滚、普通放收虫、空回→操虫斩/急袭突刺、落地与再出手，不重开所有旧测试。
5. 结果写回M6计划及总体门禁：通过的是“非音效M6”；音效延期与普通猎虫回手欠项分别列明。

用户交接只需：本次会话路径、目視是否异常、有异常的大致动作；文件/日志可读取时不要求把全部内容手工复制到对话。

**本文生成时尚未开始E6。下一步不是先搭HUD蓝图，而是M6.1审计与基础设施代码。**
