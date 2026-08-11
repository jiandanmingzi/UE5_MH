# 验证方案

> **使用时机：** 对应 C++ 与 [编辑器接线](demo-setup.md) 完成后执行。本文首先列出虫棍木桩 Demo 的权威验收项；后半保留的全项目测试不阻塞本轮。任何条目必须以当前源码、Content、PIE 或打包结果为证据，不能因文档已写入而标记通过。

## 当前迭代：原创虫棍木桩 Demo

### 范围与通用架构

1. README、Demo 指南、虫棍资源和动作文档统一声明：以《世界》为基底，选择性改造三部曲动作；不是《崛起》逐项复刻。
2. 项目中没有翔虫资源/恢复/消费、集中模式或钩爪作为 Demo 前置系统；降龙等移植动作不隐式检查翔虫。
3. 通用协调器只处理 InputSnapshot、转移边、ActionToken 与标签条件；新增虫棍动作不增加 `if Weapon==InsectGlaive`。
4. 每条 Combo 转移拥有唯一 TransitionID；自动派生按 ID 定位。ActivateAbility 边经过 Pending→Resource Reserve→GAS Commit→Consume→Confirm，StateOnly 只用于同一 ActionToken 的自动阶段变化。
5. TryActivate、Reserve 或 Commit 失败均不改变 CurrentState/Tag，且 reservation 被释放；旧 Ability 的迟到 Hit/End/Notify 不能清理或重置新 ActiveTransition。

### 输入与地面连招

6. WeaponInputRouter 能稳定区分 Y、B、Y+B、LT+Y、LT+B、RT+Y、LT+RT、LT+Y+B、RT+Y+B；LT/RT 可提前长按，也可在 Trigger 候选超时前最后补齐。每个必需成员 Started 都重算候选，组合成立后不会额外触发组成它的单键动作。
7. 角色面朝屏幕左侧且摇杆向左时，`前+Y+B` 匹配 Forward；摇杆上推不匹配。方向、按键、修饰态和 Ground/Aerial、Sheathed/Unsheathed 均来自同一 InputSnapshot；Grace 期间变姿态不误触发新姿态动作。
8. 收刀 LT 只进入未使用的投射物瞄准；收刀不能 LT+Y 放虫；收刀 RT 执行拔刀并沿角色 Forward 放虫。
9. 持刀 LT+Y/LT+B 分别送虫/召回；普通 Y/B 仍用于《世界》地面连招，不被猎虫操作抢占。
10. 《世界》地面基底动作按 ComboData 正常起手、派生、超时和回 Idle。
11. Y+B 的 Idle Start 边不要求窗口；Derive 边只在 ComboWindow 内从除飞圆斩外的地面动作派生四连印斩；不能任意帧打断。四个 AttackSegment 独立去重。
12. Forward+Y+B 在相同候选中优先触发突进回旋斩；未成功反击时结束规则与四连印斩一致。
13. 四连印斩/未反击回旋斩结束后只可接突刺、上捞斩、横扫、飞身跃入斩；AnyState 节点不能绕过限制。
14. 木桩测试攻击先进入 IncomingHitResolver：窗口外同一 AttackInstanceID 只 Apply 一次；窗口内由绑定当前 ActionToken 的 Counter Token 消费，不伤害玩家且只触发一次舞踏。

### 精华、红灯模式与猎虫

15. 木桩 Head/Torso/Leg 分别提供 Red/Orange/White；准心、普通猎虫和操虫斩读取同一 Hitzone.ExtractColorTag。
16. 普通猎虫通过 Hitzone Object Capsule Sweep 对最早部位造成一次伤害并记录 PendingExtract；命中后立即关闭伤害/Sweep 并原地 Hover，不自动回手。主动召回或耐力归零后 Returning，到达玩家时原子取出并清 Pending、只交付一次；下一 Flight 可取得不同颜色。
17. ClassicMovesetGate 为默认：无红灯使用弱化动作组，有红灯使用正常完整动作组。
18. 非攻击状态把 RuntimeHost 的 ActiveRedExtractMode 从默认 Classic 覆写为 Numeric 后，原子替换互斥 Mode Tag 并 ResetCombo，不修改 DataAsset、不重授 Ability；有/无红灯使用同一正常动作组。
19. 红白橙齐全进入不可刷新的三灯；三个单灯状态 Tag 消失，但 Triple GE 保留 `Combat.Branch.Extract.Red` 动作权限；自然结束后全部熄灭。持续时间/倍率只在唯一 CombatConfig 调整。
20. 三灯期间操虫斩或普通猎虫吸收颜色时，统一 ApplyExtract 直接吞灯；不创建单灯、不缓存颜色、不刷新三灯。

### 空中、舞踏和特殊动作

21. 空中直接 B 的操虫斩沿角色 Forward；LT+B 沿激活瞬间准心方向；两者均受距离/碰撞限制且命中后点灯。
22. 只有操虫斩命中和突进回旋斩反击增加舞踏；达到 MaxDanceStacks 后不超限，逐层倍率读取配置。
23. 舞踏倍率只作用于猎人的空中攻击，不作用于猎虫、粉尘和地面伤害；落地、受击、收刀/卸装、急袭突刺、降龙会按规则清空。
24. 强化操虫穿刺只可从操虫斩来源的舞踏用 LT+Y 派生；回旋斩来源的舞踏不能派生。
25. 空中不存在跳跃突进斩；强化跳跃斩和急袭突刺继承进入动作时的水平惯性，动作结束正确交还 FinalVelocity。
26. 持刀 LT+RT 命中 Hitzone 后创建唯一虫印；新印替换旧印，射空不替换，收刀保留，目标失效/到期/卸装清除。
27. 地面 RT+Y：有效虫印在距离内时猎虫滑翔追向虫印；否则短距前飞并少量抬升；命中怪物后小跳进入空战但不增加舞踏。
28. 三灯下 RT+Y+B：觉虫击 Commit 通过 `Cost.IG.TripleUp` 原子清空旧三灯，准心方向限制在角色正前方上下左右 60°；猎虫每次成功提交贯通伤害后恰好按 Hitzone ApplyExtract 一次并留下通用粉尘一团，命中三色可在途中重新形成三灯，之后的萃取被吞且不刷新；伤害提交失败不点灯、不产粉尘；猎人位移有距离/碰撞上限。
29. 无三灯觉虫击激活失败且不移动、不耗资源；过程中受击/碰墙能清理猎人位移，猎虫按独立生命周期结束。
30. 地面 LT+Y+B 只集约范围内己方未预留粉尘；同一目标只结算一次聚合爆炸；取消时解除 Reserved 且不丢失粉尘。Demo 粉尘不被普通武器/猎虫接触引爆。
31. 空中 LT+Y+B 的降龙不消耗翔虫或精华；使用当前舞踏倍率后清层，连续碰撞、落地段和异常取消均无残留 RootMotionSource/攻击窗口。

### 木桩、伤害与稳定性

32. 自定义 EffectContext 保留真实 HitResult/AttackInstanceID；零 MotionValue 的有效命中通过四个 Meta+最终 HitSignal 产生一次零伤害反馈，正动作值按 Demo 简化公式结算。
33. 木桩三色 Hitzone 互不重叠且 ObjectType=Hitzone/QueryOnly；实体 Body 独立。武器用 Weapon Trace，猎虫用 Hitzone Object Sweep；Aim 用 Visibility 并验证 Hitzone，墙后部位不显示颜色。
34. 在 30/60/120 FPS 下，组合输入、四连印斩四段、回旋反击窗口、觉虫击贯通和粉尘集约结果一致。
35. 任意动作在受击、落地、死亡、收刀、换武器或 PIE End 时不残留 PendingTransition、Timer、Delegate、Reserved Powder、Loose Tag/GE、攻击窗口、Projectile 或 RootMotionSource；急袭突刺/降龙的 AbilityOwned Landing 不被通用落地重置提前截断。
36. Packaged Development Build 中完整执行 [编辑器接线指南](demo-setup.md) 第 10 节的分层验证，结果与 PIE 一致。
37. 动作 GA 为 InstancedPerExecution、协调器为 InstancedPerActor；连续激活同一 GA Class/Spec 时两个实例有不同 ActionToken，旧实例以 Superseded 结束。
38. 同时存在旧 BlendOut 与新 Montage 时，AttackCollision/Combo/Dodge Notify 只通过 Mesh+MontageInstanceID 调用所属 AbilityInstance；Notify 不遍历全部 Active Specs。旧 NotifyEnd 不关闭新窗口。
39. InputComponent 是唯一 IMC/Binding 所有者。连续两次 Setup、UnPossess→Possess、死亡重生后，按一次键只收到一次快照；ASC/PlayerController 不持有第二份 UInputAction 绑定。
40. `Input.Dodge` 只使用 InputSnapshot.Direction；缺 Montage/AnimInstance、Commit 失败、Montage Interrupted 时均无 BlockMovement/Invincible 残留，各碰撞通道恢复 NotifyBegin 前原响应而非固定 Block；最终 `PlayerCapsule` 必须恢复为 Weapon=Ignore、MonsterAttack=Block。
41. 护甲、饰品、镶嵌和同一 WeaponSnapshot 重复广播不重建 Runtime、不清空精华/舞踏/Pending；真正更换武器才执行一次完整 Shutdown/Initialize。
42. 动作拥有 Movement Token 时 Character locomotion 不覆盖旋转；正常完成、Superseded、受击、死亡、换装和失败早退后不存在本动作 RootMotionSource 或 WarpTarget。
43. 猎虫第一次回手交付 Red 后 Pending 为空，第二次命中 White 可正常交付；迟到 Return 回调不能重复交付 Red。
44. 猎虫耐力从正值降到 0 时警告音效和 ForceRecall 各一次；保持 0 或已 Returning/Attached 的后续 Tick 不重复触发。
45. Runtime Ready 同 Token 重复广播为 UI no-op；Viewport 只有一份 WBP_HUD，资源面板只作为 WeaponResourceSlot 的一个子控件。旧 Pawn Invalidated 不能删除新面板。
46. 旧 `DT_WeaponComboConfig`、旧最小 Combo、两个旧拼音 GA/Montage 和旧组合 InputAction 已按清单删除；最终 DataAsset/GA/Montage 均从最终类型新建，不引用 Attack 旧字段；冷启动、资产扫描和打包无 Missing Property/Class。
47. 觉虫击已有三灯但强制让 GAS Commit 因耐力/冷却失败：reservation 被释放，Triple Handle 与剩余时间不变，无 Montage/位移/状态提交；正常成功时 GAS 成本与三灯各消费一次，不存在部分提交。

## 后续全项目历史清单（不阻塞当前 Demo）

> 本节是早期全项目回归意图，尚未按本轮冻结接口逐项重写；它不能覆盖当前 Demo 计划，也不能直接作为后续实现规格。完成 M7 后再按届时源码和完整游戏范围重新评审。

1. 创建 r5 武器、r3 饰品 DataAsset，验证 RarityLevel 和 Tag 自动生成
2. 孔位 Lv3→镶 Lv2 饰品 ✅，孔位 Lv2→镶 Lv3 饰品 ❌
3. 背包/仓库：堆叠、转移、一键整理
4. 仓库分类：标签页切换正确，稀有度筛选 r1-r12
5. 快捷栏自动登记：放入药水→自动显示，用完→自动移除
6. 快捷栏切换使用：滚轮选中→按键触发→数量-1→归零清槽
7. 特殊动作：分配项目未来确定的非武器动作→选中→触发（不涉及物品；不包含钩爪）
8. 装备武器→ASC 获词条 GE；卸下→Tag 批量移除→属性回归基础值
9. ASC 属性初始值验证：Health/Stamina=100，耐力三速率=1.0，AttackPower/Defense=0
10. 装备 AttackPower=15 武器→ASC AttackPower=15；卸下→归零
11. CriticalRate 超出 [-100,100] 截断，StaminaDeductionRate 不低于 0
12. 装备太刀→资源条显示；切换大剑→资源条切换为大剑样式
13. DT_EntryCatalog 登记 AttackUp→装备引用→装备时查表→UExecCalc_EntryStat 读曲线算数值→Apply
14. **装备实例独立状态**：两把铁剑 A 和 B→A 镶火焰宝石、B 镶寒冰宝石→各自独立
15. 存档：ItemInstance 序列化 + EquipmentInstance 序列化 → 反序列化恢复
16. **输入绑定**：按键 A→InputAction→Tag→ASC 触发 GA；改 AbilityTag 后按键触发不同 GA
17. **武器切换**：太刀→大剑→旧连招 Ability 被移除→新连招 Ability 被授予→输入绑定自动切换
18. **AttackAbility**：创建 GA_Sword_Slash_01→只配 AttackSegments+Montage→不写蓝图逻辑→命中自动 Apply 伤害 GE
19. **连招窗口**：Montage 中拖拽 NotifyState 区间 → 窗口内按键触发下一招、窗口外被忽略
20. **连招安全兜底**：动画被打断超过 GlobalComboTimeout→协调器强切 Idle→清除临时 Tag
21. **虫棍无收尾招环**：DoubleSlash→△→DoubleSlash 自循环→窗口超时自然回 Idle
22. **Montage 完成回 Idle**：Montage 自然播完→GA EndAbility→协调器 OnActionFinished(ActionToken, Reason)→CurrentState="Idle"
23. **协调器 Infinite**：装备太刀期间 ASC→GetActiveAbilities() 始终包含 GA_WeaponComboCoordinator
24. **地面/空中态隔离**：被击飞后 Aerial+Hitstun→地面招式 RequiredTags 不满足→按 △ 无响应；落地恢复
25. **拔刀/纳刀态**：默认 Sheathed→按 △ 触发拔刀斩→后续地面招式可正常连招；按 R1 纳刀→攻击招式不可用
26. **统一派发路由**：按 Y→lambda 捕获 Input.Weapon.Y→OnInputActionTriggered→MatchesTag("Input.Weapon")=true→协调器→HandleWeaponInput
27. **部位命中顺序**：同一帧的所有 Region/采样点/时间子步汇总后，武器轨迹先掠过翅膀(防御 0.5)再命中身体(防御 1.0)→取归一化帧时间最早的翅膀→伤害按翅膀倍率
28. **部位去重**：同一斩击命中怪物头部→记录（怪物A, Head），后续同帧 Overlap 到身体→跳过
29. **多段判定**：双刀乱舞 Montage 中放 4 个不同 ConfigIndex 的 AttackCollision→每段独立 Sweep→各自记录 HitTargets；相邻窗口重叠时互不关闭、互不清空
30. **Montage 归属**：GA 内部指定 Montage→FComboTransition 仅引用 AbilityClass→连招表和动画零耦合
31. **空挥断连**：(a) 气刃斩1 空挥→SpiritBlade Tag 未授予→气刃斩2 RequiredTags 不满足→断连。(b) DoubleSlash 段1 空挥→EndAbility 提前。(c) 首次命中→Apply OnHitSelfEffect→后续命中不重复
32. **太刀 BlockedTags + 资源门控**：(a) 特殊纳刀 bMatchAnyState+RequiredTags={Unsheathed}→Idle 不可起手。(b) 登龙剑 BlockedTags={PostRoundslash}→大回旋后不可派生。(c) 登龙第一段命中→ShouldContinueAfterHit 检查气刃槽≥白。(d) 大回旋 GrantedTags={PostRoundslash}→Montage 完成后清除→登龙恢复可派生
33. **预输入缓冲**：(a) ComboWindow 开前 0.1s 按 Y→PreInputTag 写入→NotifyBegin 消费→纵斩发出。(b) 开前 0.3s 按 Y→超 PreInputLifetime→无响应。(c) 连续按 Y→B→Y→后覆盖前→缓冲仅保留最后的 Y
34. **翻滚窗口（纯 Tag）**：(a) 纵斩收招帧按 A→Attacking+DodgeAcceptOpen 均满足→翻滚发出。(b) Idle 按 A→翻滚始终可用
35. **bRequiresWindowOpen=false + DodgeAcceptOpen**：(a) 虫棍纵斩 DodgeAcceptOpen 内按 LT+B→收虫触发。(b) 窗口外按 LT+B→不触发。(c) 普通纵斩 DodgeAcceptOpen 内按 A→翻滚触发
36. **DataManager 全局查询**：(a) EquipmentComponent 查词条目录→正常返回。(b) ExecCalc 获取曲线表→得正确数值。(c) 资产未加载→FindEntryDefinition 返回 false→日志警告
37. **移速同步**：(a) 装备大剑→MoveSpeedMultiplier=0.6→CMC 同步。(b) 喝加速药水→叠加 0.8→CMC 同步。(c) 卸下大剑→回归 1.0
38. **持刀不可奔跑**：(a) 收刀态按 LS→GA_Sprint 激活。(b) 拔刀后按 LS→Unsheathed 阻塞。(c) 纳刀后按 LS→恢复可奔跑
39. **GameplayEvent 受击连打**：(a) 受击→GA_HitReaction 实例1 激活。(b) 连打中再次受击→实例2 激活（InstancedPerExecution）→实例1 被打断。(c) 实例2 播完→Hitstun Tag 移除
40. **StaminaRequired vs StaminaCost**：(a) Required=20, Cost=20→检查通过→扣 20。(b) Required=25, Cost=20→CurrentStamina=22→门槛不通过。(c) CurrentStamina=30→门槛通过→扣 20→剩余 10
41. **持续耗耐帧率无关**：(a) 60 FPS 奔跑 1s→累计扣除 = CostRate × 1.0。(b) 30 FPS 奔跑 1s→累计扣除一致。(c) 耐力扣到 0→Clamp→EndAbility
42. **蓄力攻击**：(a) 按住 Y→ChargeLevel 递增→Charging Tag 持有。(b) 松开 Y→读 ChargeLevel=Lv3→播放真蓄力斩。(c) 蓄力期间按 A→检查 Charging Tag 决定是否允许。(d) 真蓄力斩播完→ComboWindow 期间按 Y→匹配后续连招
43. **无敌帧碰撞穿透**：(a) NotifyBegin→Weapon 通道=Ignore+Invincible Tag。(b) 怪物攻击 Sweep→Ignore→不命中。(c) Pawn 通道 Block→无法穿过怪物。(d) NotifyEnd→恢复
44. **怪物攻击碰撞**：(a) NotifyBegin→部位 MonsterAttack 通道=Block。(b) NotifyTick Sweep 检测玩家→命中→Apply 伤害。(c) NotifyEnd→恢复 Ignore→收招不误伤。(d) 龙车高速冲撞→首帧 Sweep→命中
45. **怪物部位胶囊体常态**：(a) 头部始终 Block Weapon→玩家斩击能命中。(b) 头部始终 Block Pawn→玩家不能穿过。(c) MonsterAttack 常态 Ignore→待机不误伤
46. **方向修正**：(a) MaxCorrectionAngle=30°→摇杆前推 20°→角色朝向旋向目标。(b) 摇杆前推 50°→不修正。(c) 无摇杆输入→不修正
47. **蓄力释放方向修正**：(a) 蓄力期间摇杆推左→Completed 时读方向→Warp Target=左+60°→Montage 播放时旋向目标
48. **见切多段二次修正**：(a) 激活→段1 Warp Target=后方 180°→后撤。(b) 段1 NotifyEnd→段2 Warp Target=左方 120°→回砍修正。(c) 超出 120°→截断
49. **多段攻击不同 MotionValue**：(a) 横扫段 MotionValue=0.6、下劈段=1.2→两段分别读取。(b) 两段都命中→伤害分别为 Attack×0.6 和 Attack×1.2
50. **单碰撞多跳伤害**：(a) MultiHitCount=7, Interval=0.1→Sweep 命中→第 1 跳立即 Apply→Timer 每隔 0.1s 跳 2~7。(b) DisableCollision→Timer 清除。(c) 怪物死亡→后续跳跳过
51. **破坏值计算**：(a) BaseStaggerValue=8, StaggerMultiplier=1.0, HitzoneStaggerRate=0.8→硬直值=6.4。(b) 装备"破坏王"→StaggerMultiplier=1.3→硬直值=8.32。(c) BaseStaggerValue=0→不造成硬直
52. **武器基类分化**：(a) 太刀 GA 继承 UMHGZLongSwordAbility→持有 URes_LongSword*→覆写资源门控。(b) 虫棍 GA 继承 UMHGZInsectGlaiveAbility→覆写 ShouldContinueAfterHit。(c) 斩斧添加新武器基类→不碰现有武器代码
53. **武器资源子系统**：(a) 气刃斩命中→槽 Amount 增加→UI 刷新→Level 升黄。(b) 一段时间未命中→Decay Timer→Level 降级。(c) 虫棍红/白/橙→不可刷新的三灯，UI 与 CombatConfig 模式同步。(d) 词条"气刃槽回复+20%"→ApplyEntryModifier→ActiveModifiers 生效
54. **登龙招内派生**：(a) 段0 突刺命中+气刃≥白→段1 起跳→段2 下劈多跳。(b) 段0 突刺命中+气刃<白→特殊后摇→EndAbility。(c) CanActivateAbility 时资源<白→激活被拒
55. **见切双段异质判定**：(a) GA_LS_ForesightSlash 继承 UMHGZLongSwordAbility→协调器正常激活。(b) 段0 Damage={0,0}→不产生伤害。(c) DodgeWindow+ForesightJudge 并行→怪物攻击命中→Invincible 截伤害→GA 记录 bDodgeSuccessful。(d) 段1 走标准 AttackCollision→ApplyDamage→bDodgeSuccessful?回满气刃槽+授予 ForesightSuccess:仅伤害
56. **QuickBar 音效与分级阻塞**：(a) Idle 滚轮切道具→立即播切换音效。(b) 持刀态按使用键→立即播使用音效→触发收刀→收刀完成自动使用。(c) 受击中按使用键→立即播使用音效→Hitstun 阻塞。(d) 攻击动画中按使用键→立即播音效→触接收刀→使用。(e) 喝药动画中再按→立即播音效→GA_Heal Active 阻塞
57. **WeaponResource 词条链路**：(a) 装备含气刃槽回复+20%词条→识别 WeaponResource→找到 URes_LongSword→ApplyEntryModifier。(b) Tick 中 GetModifiedParam→返回 Base × 1.3。(c) DT_WeaponResourceConfig 未参与此流程。(d) 全量重建→ClearAllEntryModifiers→重新 Apply→正确恢复
58. **仓库与装备界面分离**：(a) 仓库仅显示 Status==InStorage 物品。(b) 装备太刀→Status=Equipped→仓库中消失。(c) 装备状态界面显示 EquippedItems+SocketedAccessories。(d) 卸下太刀→Status=InStorage→仓库中恢复
59. **EquippedItems 指针语义**：(a) 仓库 Slots[3] 有铁剑 InstanceA→EquipItem→EquippedItems.Add(Weapon, InstanceA)→本体仍在 Slots[3]。(b) UnequipItem→移除指针→SetStatus(InStorage)→仓库中恢复
60. **虫棍前后段轨迹**：(a) 前段招式仅配置 `Root→FrontTip`，后半棍接触木桩不结算。(b) 后段招式仅配置 `Root→RearTip`，前半棍接触不结算。(c) 整棍横扫同一 Segment 配置 Front+Rear 两个 Region，两侧均可命中但同一怪物每窗口只结算一次
61. **自适应采样与旋转子步**：(a) 增长 Region 长度→采样数自动增加且间距不超过 `min(MaxSampleSpacing, 2×Radius)`。(b) 单帧转角超过 `MaxAngularStepDegrees`→红色调试路径增加时间子步。(c) 所需采样数超过 MaxSampleCount→Output Log 输出缺口警告

## GameplayCue 系统

62. **Cue 触发链路**：伤害结算生成 HitFeedbackResult→Target HitFeedbackRouter 使用真实 HitResult 和最终伤害显式执行 Slash + DamageNumber Cue；不依赖 DynamicAssetTags 自动触发
63. **命中火花**：GC_Hit_Slash 在命中位置生成粒子+播放音效→距相机 > 3000 时不生成→并发 > 10 时最早的被回收
64. **伤害数字池**：对象池预分配 30 Widget→命中伤害 85→显示白色 "85"→1.5s 上浮淡出→回收→重复命中池不泄漏
65. **暴击**：ExecCalc 判定暴击→追加 `Hit.Crit`→同时出现 Slash + Crit + DamageNumber→Crit 火花更亮更大→伤害数字黄色放大 1.5x
66. **元素附魔**：火属性太刀命中→Slash + Fire 两个 GC 同时触发→两个粒子系统在命中位置合并显示
67. **Buff 光环**：攻击力 Buff GE Apply→GC_Buff_AttackUp OnActive→红色光环围绕角色→GE 到期→OnRemove 光环消散
68. **翻滚特效**：GA_Dodge 激活→Execute `Character.Dodge`→尘土粒子+音效→Montage 正常完结
69. **死亡特效**：HP=0→GA_Death→`Character.Death`→死亡粒子爆散+音效
70. **对象池压力**：双刀乱舞 4 段 × 7 跳 = 28 次命中/秒→伤害数字池始终 ≤ 30→无泄漏→命中 GC 池始终 ≤ 10 并发→无泄漏
71. **INI 扫描**：`+GameplayCueNotifyPaths=/Game/GameplayCues`→新建蓝图 GC 自动被 GameplayCueManager 扫描→无需额外注册
72. **物理表面路由（可选）**：斩击命中木桩（Wood 物理材质）→路由到 `Hit.Slash.Wood`→若不存在则回退到 `Hit.Slash`
