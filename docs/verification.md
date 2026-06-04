# 验证方案

1. 创建 r5 武器、r3 饰品 DataAsset，验证 RarityLevel 和 Tag 自动生成
2. 孔位 Lv3→镶 Lv2 饰品 ✅，孔位 Lv2→镶 Lv3 饰品 ❌
3. 背包/仓库：堆叠、转移、一键整理
4. 仓库分类：标签页切换正确，稀有度筛选 r1-r12
5. 快捷栏自动登记：放入药水→自动显示，用完→自动移除
6. 快捷栏切换使用：滚轮选中→按键触发→数量-1→归零清槽
7. 特殊动作：分配钩爪→选中→触发（不涉及物品）
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
20. **连招安全兜底**：动画被打断超过 ComboTimeout→协调器强切 Idle→清除临时 Tag
21. **虫棍无收尾招环**：DoubleSlash→△→DoubleSlash 自循环→窗口超时自然回 Idle
22. **Montage 完成回 Idle**：Montage 自然播完→GA EndAbility→协调器 OnAttackFinished()→CurrentState="Idle"
23. **协调器 Infinite**：装备太刀期间 ASC→GetActiveAbilities() 始终包含 GA_WeaponComboCoordinator
24. **地面/空中态隔离**：被击飞后 Aerial+Hitstun→地面招式 RequiredTags 不满足→按 △ 无响应；落地恢复
25. **拔刀/纳刀态**：默认 Sheathed→按 △ 触发拔刀斩→后续地面招式可正常连招；按 R1 纳刀→攻击招式不可用
26. **统一派发路由**：按 Y→lambda 捕获 Input.Weapon.Y→OnInputActionTriggered→MatchesTag("Input.Weapon")=true→协调器→HandleWeaponInput
27. **部位命中顺序**：武器轨迹先掠过翅膀(防御 0.5)再命中身体(防御 1.0)→Sweep 取首个命中=翅膀→伤害按翅膀倍率
28. **部位去重**：同一斩击命中怪物头部→记录（怪物A, Head），后续同帧 Overlap 到身体→跳过
29. **多段判定**：双刀乱舞 Montage 中放 4 个独立 AttackCollision→每段独立 Sweep→各自记录 HitTargets
30. **Montage 归属**：GA 内部指定 Montage→FComboNode 仅引用 AbilityClass→连招表和动画零耦合
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
53. **武器资源子系统**：(a) 气刃斩命中→槽 Amount 增加→UI 刷新→Level 升黄。(b) 一段时间未命中→Decay Timer→Level 降级。(c) 虫棍红灯+白灯→组合 Buff GE Apply→三星全 Buff 刷新计时器。(d) 词条"气刃槽回复+20%"→ApplyEntryModifier→ActiveModifiers 生效
54. **登龙招内派生**：(a) 段0 突刺命中+气刃≥白→段1 起跳→段2 下劈多跳。(b) 段0 突刺命中+气刃<白→特殊后摇→EndAbility。(c) CanActivateAbility 时资源<白→激活被拒
55. **见切双段异质判定**：(a) GA_LS_ForesightSlash 继承 UMHGZLongSwordAbility→协调器正常激活。(b) 段0 Damage={0,0}→不产生伤害。(c) DodgeWindow+ForesightJudge 并行→怪物攻击命中→Invincible 截伤害→GA 记录 bDodgeSuccessful。(d) 段1 走标准 AttackCollision→ApplyDamage→bDodgeSuccessful?回满气刃槽+授予 ForesightSuccess:仅伤害
56. **QuickBar 音效与分级阻塞**：(a) Idle 滚轮切道具→立即播切换音效。(b) 持刀态按使用键→立即播使用音效→触发收刀→收刀完成自动使用。(c) 受击中按使用键→立即播使用音效→Hitstun 阻塞。(d) 攻击动画中按使用键→立即播音效→触接收刀→使用。(e) 喝药动画中再按→立即播音效→GA_Heal Active 阻塞
57. **WeaponResource 词条链路**：(a) 装备含气刃槽回复+20%词条→识别 WeaponResource→找到 URes_LongSword→ApplyEntryModifier。(b) Tick 中 GetModifiedParam→返回 Base × 1.3。(c) DT_WeaponResourceConfig 未参与此流程。(d) 全量重建→ClearAllEntryModifiers→重新 Apply→正确恢复
58. **仓库与装备界面分离**：(a) 仓库仅显示 Status==InStorage 物品。(b) 装备太刀→Status=Equipped→仓库中消失。(c) 装备状态界面显示 EquippedItems+SocketedAccessories。(d) 卸下太刀→Status=InStorage→仓库中恢复
59. **EquippedItems 指针语义**：(a) 仓库 Slots[3] 有铁剑 InstanceA→EquipItem→EquippedItems.Add(Weapon, InstanceA)→本体仍在 Slots[3]。(b) UnequipItem→移除指针→SetStatus(InStorage)→仓库中恢复

## GameplayCue 系统

60. **Tag 触发链路**：太刀攻击命中→GE Spec 的 DynamicGameplayCueTags 含 `Hit.Slash` + `Hit.DamageNumber`→GC 管理器正确路由到对应 Notify
61. **命中火花**：GC_Hit_Slash 在命中位置生成粒子+播放音效→距相机 > 3000 时不生成→并发 > 10 时最早的被回收
62. **伤害数字池**：对象池预分配 30 Widget→命中伤害 85→显示白色 "85"→1.5s 上浮淡出→回收→重复命中池不泄漏
63. **暴击**：ExecCalc 判定暴击→追加 `Hit.Crit`→同时出现 Slash + Crit + DamageNumber→Crit 火花更亮更大→伤害数字黄色放大 1.5x
64. **元素附魔**：火属性太刀命中→Slash + Fire 两个 GC 同时触发→两个粒子系统在命中位置合并显示
65. **Buff 光环**：攻击力 Buff GE Apply→GC_Buff_AttackUp OnActive→红色光环围绕角色→GE 到期→OnRemove 光环消散
66. **翻滚特效**：GA_Dodge 激活→Execute `Character.Dodge`→尘土粒子+音效→Montage 正常完结
67. **死亡特效**：HP=0→GA_Death→`Character.Death`→死亡粒子爆散+音效
68. **对象池压力**：双刀乱舞 4 段 × 7 跳 = 28 次命中/秒→伤害数字池始终 ≤ 30→无泄漏→命中 GC 池始终 ≤ 10 并发→无泄漏
69. **INI 扫描**：`+GameplayCueNotifyPaths=/Game/GameplayCues`→新建蓝图 GC 自动被 GameplayCueManager 扫描→无需额外注册
70. **物理表面路由（可选）**：斩击命中木桩（Wood 物理材质）→路由到 `Hit.Slash.Wood`→若不存在则回退到 `Hit.Slash`
