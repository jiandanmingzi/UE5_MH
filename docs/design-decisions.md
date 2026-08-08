# 一键整理排序表

> **状态说明：** 本文记录目标设计决策，不等同于实现清单。当前实施状态应查阅对应系统文档顶部的“当前实现”校准以及源码；尚未落地的决策继续保留，供后续实现使用。

| 优先级 | 品类 | 匹配标签 | 二级键 | 三级键 |
|:--:|------|------|:--:|------|
| 0 | 武器 | `Item.Type.Weapon` | RarityLevel 降序 | Name 升序 |
| 1 | 衣服 | `Item.Type.Armor` | RarityLevel 降序 | Name 升序 |
| 2 | 饰品 | `Item.Type.Accessory` | RarityLevel 降序 | Name 升序 |
| 3 | 可使用 | `Item.Type.Consumable` | RarityLevel 降序 | Name 升序 |
| 4 | 不可使用 | `Item.Type.Material` | RarityLevel 降序 | Name 升序 |
| 5 | 任务 | `Item.Type.Quest` | RarityLevel 降序 | Name 升序 |

---

# 设计决策汇总

| # | 决策 | 理由 |
|---|------|------|
| 1 | 五系统解耦 | 物品/属性/存储/使用/词条各自独立迭代 |
| 2 | bIsUsable 用成员 bool | 决定代码路径，高频判断，比 Tag 快 |
| 3 | 物品分类用 GameplayTag | 纯分类扩展，策划随时加子类型 |
| 4 | 装备槽位用类继承+Tag 双重标识 | 子类决定字段（编译器安全），Tag 决定运行时槽位匹配 |
| 5 | RarityLevel 用 int32 | 数值排序，自动生成 Tag 供筛选 |
| 6 | 镶嵌等级制（1-4） | 数值比较，简单可靠 |
| 7 | 背包各自堆叠 vs 仓库 99999 | 策略 vs 便利 |
| 8 | 背包不可分类 vs 仓库可分类 | 快操 vs 检索 |
| 9 | 快捷栏切换选中→触发 | 适配手柄，减少按键占用 |
| 10 | UseAction/SpecialAction 通过 GAS Ability | 与动作系统统一 |
| 11 | 词条目录 + SimpleStat/Complex 分流 | 集中管理 + 80% 词条无需各自 GE 蓝图 |
| 12 | 装备 GE 打 Tag 批量移除 | 当前使用 RemoveActiveEffectsWithAppliedTags，不存 Handle |
| 13 | EquipmentInstance::Status 替代 O(n) 查询 | 由 EquipmentComponent 维护，UI 直接读 O(1) |
| 14 | 全量重算非增量 | 不维护中间状态，正确性保证。装备变更仅在非战斗期触发 |
| 15 | CurveTable 驱动词条数值 | 支持非线性、多属性、跨级突变 |
| 16 | 武器专属资源由种类决定 | WeaponTypeTag→WeaponResourceComponent 子类，同种类共享资源逻辑 |
| 16b | 武器连招表由种类决定 | WeaponTypeTag→DT_WeaponComboConfig→WeaponComboData |
| 17 | 角色属性与装备完全解耦 | 装备字段→GE→ASC，中间无直接引用 |
| 18 | PrimaryDataAsset 做所有定义 | 策划编辑友好，异步加载 |
| 19 | Entries 放在 EquipmentDefinition | 词条是装备专属概念 |
| 20 | 客制化存于 EquipmentInstance | 客制化跟装备本身；卸下再装上不丢失 |
| 21 | GameplayTag 桥接输入与 Ability | EnhancedInput→InputAction→Tag→ASC.TryActivateByTag |
| 22 | Ability 基类统一处理耐力/冷却/资源 | 蓝图子类只需实现具体动作逻辑 |
| 23 | 攻击 Ability 统一继承 UMHGZAttackAbility | 碰撞检测+命中过滤+伤害 GE 构造全部由中间层处理；新增武器 0 行代码 |
| 24 | 连招系统采用出招表（FComboNode 有向图），辅以 RequiredTags/GrantedTags | 策划在一处可视化完整连招树；Tag 仅用于动态分支条件 |
| 25 | 回 Idle 主驱动 = Montage 完成，GlobalComboTimeout（协调器统一字段，默认 10s）仅唯一安全兜底 | Montage 自身长度即精确计时；统一兜底优于每节点单独配置 |
| 26 | FComboNode 支持 StateName 自指/前指（有向图允许环），用具体招式名 | 适应虫棍等无收尾招武器；招式名让多路径收敛显式可见 |
| 27 | UMHGZWeaponComboData 归属动作系统（§3）而非装备系统（§2） | 连招表定义的是"怎么做动作"而非"装备有什么属性" |
| 28 | bMatchAnyState 处理纳刀/起跳等通用招式 | 一行覆盖所有地面招式，不硬编码排除受击/击倒 |
| 29 | Montage 归 GA 蓝图所有，FComboNode 不持有动画引用 | 连招表只管"哪个状态接哪个 GA"，动画细节由 GA 内部决定 |
| 30 | 首帧 Sweep 判定部位命中顺序 | 解决 UE Overlap 事件不保证空间先后的问题 |
| 31 | 部位信息通过 SetByCaller + DynamicTag 双通道传递 | 攻击侧不耦合怪物防御力 |
| 32 | GA_WeaponComboCoordinator 为 Infinite 持续型 Ability | 协调器需在整个装备期间保持 Active |
| 33 | 出招表为平面数组 + StateIndex 索引，非树状嵌套 | 匹配时 O(1) 查候选行 → 遍历匹配条件 |
| 34 | 地面/空中/拔刀态由 GameplayTag 显式管理，协调器只读 ASC Tag 状态 | 协调器不主动判断状态——只做 Tag 匹配过滤 |
| 35 | 所有 IA 统一绑定到 ASC 的 OnInputActionTriggered，按 AbilityTag 类别分叉路由 | 单一 EnhancedInput 绑定点，Tag 做路由中介 |
| 36 | UI 由 GameplayTag/Attribute 变化驱动，Ability 不直接操作 UI | Character 输入维护 Combat.State.Aiming，AimComponent 订阅 Tag；Widget 接线仍待实现 |
| 37 | 翻滚不进连招表——独立 GA_Dodge + DT_WeaponDodgeConfig 参数化 | 翻滚是取消/中断动作，非连招的一环 |
| 38 | 受击判断 = GE Execute 时被动检查；霸体通过 GameplayTag + AnimNotifyState_PoiseWindow | 不需要 Tick 轮询、不需要"受击监听组件" |
| 39 | VFX/SFX/镜头三层分工——不设独立系统 | 帧同步（AnimNotify）/ 状态驱动（GameplayCue）/ 镜头（Ability 内 CameraModifier） |
| 40 | InputComponent 只管 IMC 生命周期，ASC 管 IA→Tag 绑定 | 消除双方各自持有绑定数组的冗余 |
| 41 | 协调器帧级输入批处理——Chord Trigger 优先级高于单键 | 确保 Y+B 超必杀不被 Y 普通攻击抢先消耗 |
| 42 | **当前版本仅单机，不考虑网络复制及多人化** | 所有 GAS、Attribute、装备状态同步方案不在本版本范围内。MoveSpeedMultiplier Tick 同步、挥刀风声 Montage 实例化、QuickBarComponent 持久性等均按单机设计，不预留多人扩展 |
| 43 | 武器专属资源不在 AttributeSet 中 | 各武器自行管理资源，通过 WeaponResourceComponent 子类实现 |
| 44 | Grounded/Aerial 在同一函数调用内完成切换 | 不跨帧，不存在"切换过程中输入被吞"的窗口 |
| 45 | bMatchAnyState 不硬编码排除任何状态 | 是否排除受击/击倒由 RequiredTags 显式声明 |
| 46 | QuickBar 使用后增量更新，不重建 | 仅当背包物品增删时才全量刷新 |
| 47 | AddItem 返回 int32 支持部分成功 | 拾取 5 瓶药水但背包只能放 3 瓶时返回 3 |
| 48 | FItemCustomization 增加 ModifiedEntries | 词条升级（Lv2→Lv3）用单次操作完成 |
| 49 | 所有 FScalableFloat 关联全局 DT_AbilityScalars | 统一 CurveTable，Ability 只指定行名 |
| 50 | FComboNode 增加 Priority 字段替代 RequiredTags 数量排序 | 策划显式指定数值，消除隐式排序歧义 |
| 51 | EquipmentInstance::SetStatus 为唯一状态修改入口 | 保证单一真相源 |
| 52 | 存/读档系统待后续实现 | InstanceID 通过 UPROPERTY 序列化保留 |
| 53 | 空挥断连三层机制 | 连招间+招式内+命中触发效果 |
| 54 | FComboNode.BlockedTags 反向排除 | NOR 逻辑——ASC 持有任一 BlockedTags 时节点不匹配 |
| 55 | ShouldContinueAfterHit / CheckWeaponResourceForAbility 虚函数钩子 | 资源门控衔接接口 |
| 56 | 预输入缓冲——FGameplayTag 存协调器，ComboWindow 打开时刷新 | "后覆盖前"单槽缓冲 |
| 57 | 翻滚窗口=ASC Tag 方案（Combat.State.Attacking/DodgeAcceptOpen） | AnimNotifyState_DodgeAcceptWindow 直接操作 Tag，不耦合协调器 |
| 58 | FComboNode.bRequiresWindowOpen=false + DodgeAcceptOpen Tag = 统一取消窗口 | 与翻滚共用取消窗口 |
| 59 | UMHGZDataManager (GameInstanceSubsystem) 集中管理全局 DataTable/CurveTable | 策划一处配置，所有系统通过 GetSubsystem 获取 |
| 60 | MoveSpeedMultiplier 已定义但尚未接入移动速度；CMC.MaxWalkSpeed 固定 1200 | 当前位移由 Motion Matching Root Motion 驱动，后续应在巡航速度层消费倍率 |
| 61 | 持刀不可奔跑由 Character 的 SprintPressed 检查 Unsheathed Tag | 当前没有 GA_Sprint，不新增专用阻塞 Tag |
| 62 | 受击硬直用 GameplayEvent 替代 Tag Trigger | 每次调用独立触发，InstancedPerExecution 支持受击连打 |
| 63 | FComboNode::StaminaRequired（门槛）与 GA::StaminaCost（消耗）分离 | 语义独立，策划可设 Required > Cost 保留耐力余量 |
| 64 | 持续耗耐当前使用 0.1s Timer + 固定 0.1s 步长 | 10Hz 调用 ApplyModToAttribute；改为真实 DeltaTime 属于后续优化 |
| 65 | 蓄力攻击在 GA 内部闭环，不进连招表路由 | 蓄力是 GA 内部多阶段状态机。Completed 事件通过检查 `Combat.State.Charging` Tag → 发送 `Combat.Event.ChargeReleased` GameplayEvent 触发释放 |
| 66 | EnhancedInput 同时绑定 Started + Completed | Started→连招匹配/标准 Ability 激活；Completed→检查 `Combat.State.Charging` Tag 后发送 `Combat.Event.ChargeReleased` |
| 67 | 无敌帧 = Weapon 通道 Ignore + Invincible Tag 双层保障 | Pawn 通道始终 Block——玩家不可穿过怪物身体 |
| 68 | 怪物部位胶囊体复用 + MonsterAttack 通道窗口切换 | 收招自动 Ignore——不误伤 |
| 69 | 怪物攻击首帧 Sweep 防高速穿透 | 龙车等高速攻击不会穿透玩家 |
| 70 | Combat.State.Charging Tag 统一表达蓄力态 | 翻滚/移动/交互等系统自行决定是否允许操作 |
| 71 | 协调器先激活再注入 ComboData | 解决 TryActivateAbilityByTag 不支持传参的问题 |
| 72 | 方向修正角度存在 GA 成员 MaxCorrectionAngle | 蓄力 GA 不进连招表也需此参数 |
| 73 | 多段招式用多个 MotionWarping AnimNotifyState 实现二次修正 | 不同段可不同角度，不需要创建多个 GA |
| 74 | FAttackSegmentConfig 统一管理碰撞+伤害+多跳 | 每段独立持有碰撞参数、动作值、破坏值、多跳配置 |
| 75 | 伤害公式 = AttackPower × MotionValue × HitzoneDefense | 三因素相乘；新增 StaggerMultiplier Attribute |
| 76 | 单碰撞多跳伤害（MultiHitCount>1） | 登龙剑下批：1 次 Sweep + Timer 驱动多次 Apply |
| 77 | 武器 Ability 基类分化——每种武器一个 C++ 中间类 | 约 50 行，持有 ResourceComponent 引用+覆写资源门控钩子 |
| 78 | 武器资源子系统——不统一但提供杠杆 | 各子类自行管理特有字段；词条加成走 ApplyEntryModifier（不走 GE） |
| 79 | 见切判定——bDodgeSuccessful + GameplayEvent 驱动 | 不拆成两个 GA——后撤+回砍是一个完整动作 |
| 80 | 登龙招内派生——ShouldContinueAfterHit 钩子 | 招内派生（同一 GA），不同于连招表的跨 GA 派生 |
| 81 | 见切继承 UMHGZAttackAbility——非攻击段用 Damage=0 + 独立 AnimNotifyState | 段0 用 GameplayEvent（怪物→玩家），段1 用 Sweep（玩家→怪物） |
| 82 | ASC 与核心组件挂载到 PlayerState | Character 可能因猫车/关卡切换重建，PlayerState 跨关卡特久存在 |
| 83 | 限时 Buff = Duration GE + GrantedTag（纯 Tag 方案） | 资源组件/AttributeSet Tick 中读 Tag 决定实际效果 |
| 84 | 翻滚取消攻击→立即回 Idle | GA_Dodge 激活时 GAS 取消当前 GA→EndAbility→OnAttackFinished()→立即 Idle |
| 85 | 预输入优先级高于帧批处理 | ComboWindow NotifyBegin 先消费 PreInputTag |
| 86 | 死亡保留装备 GE，清除 Buff GE | RemoveActiveEffectsWithGrantedTags(Combat.Buff) 批量清除 |
| 87 | MotionWarpingComponent 挂载到 Character | 需 SkeletalMeshComponent+AnimBP 管线，PlayerState 不具备 |
| 88 | 预输入 Chord Trigger 天然正确，单槽足够 | Chord 回调总是最后到达，后覆盖前 |
| 89 | QuickBar 挂载到 PlayerController | 输入/反馈层组件，通过 GetPlayerState() 一次跳转访问 Backpack |
| 90 | QuickBar 音效与执行分离 | 切换选中/使用选中时无条件播放音效 |
| 91 | DT_WeaponResourceConfig 映射 ResourceComponentClass + ResourceWidgetClass | 资源数值仍由各 WeaponResourceComponent 子类自行管理；当前资产尚未创建 |
| 92 | 仓库 UI 仅显示 InStorage 物品 | 仓库只管存储——已装备/已镶嵌物品不出现在仓库列表中 |
| 93 | EquippedItems/SocketedAccessories 存指针 | 对象本体始终在 WarehouseComponent::Slots 中 |
| 94 | WeaponComboData 当前同步加载并通过 InjectComboData 注入 | RequestID 异步竞态保护是保留的后续方案 |
| 95 | EDirectionalInput 字段已定义但协调器尚未消费 | 方向快照与象限匹配是保留的后续方案 |
| 96 | AnimNotifyState_ForesightJudge 协作模式 | NotifyState 管理窗口 + GA 管理事件委托 |
| 97 | 关卡切换——Seamless Travel 为主 | 据点↔任务地图，PlayerState 保留，ASC/背包/仓库无缝衔接 |
| 98 | MultiHitTimer 清理——三重保障 | `DisableCollision` 正常路径 + `EndAbility` 取消路径 + `BeginDestroy` 销毁兜底均有清除 |
| 99 | 伤害数字走 GameplayCue（`GameplayCue.Hit.DamageNumber`） | 触发入口统一；内部用 WorldSubsystem 对象池管理 Widget 生命周期 |
| 100 | 命中反馈全面走 GameplayCue（火花+音效+震屏） | 一个 GC 蓝图同时配置三者，策划替换武器时只改一个 Tag |
| 101 | C++ 基类按语义分类——`UMHGZCue_HitBase`（Burst）+ `UMHGZCue_BuffBase`（Latent） | 接口精准不混淆，各自独立优化 |
| 102 | 武器拖尾不入 GameplayCue | 拖尾需帧精确匹配动画（AnimNotify 管理 start/stop），属帧同步范畴 |
| 103 | 伤害数字内建独立对象池（WorldSubsystem） | Widget 生命周期与 GC Actor 池不同——Widget 1.5s 淡出，GC Actor 即刻回收 |
| 104 | FComboNode 去掉 ComboTimeout，协调器使用 GlobalComboTimeout 统一兜底 | 避免策划每行配置超时，减少遗漏和出错 |
| 105 | FComboNode 去掉 bResetsComboLevel | 连段计数系统未定义，无消费方——后续需要时由武器基类自行管理 |
| 106 | 蓄力释放走 GameplayEvent（Combat.Event.ChargeReleased） | 蓄力 GA 被打断后 Charging Tag 已移除→事件不触发，避免意外释放；不依赖查找 ActiveAbilities |
| 107 | MonsterAttack 通道强制恢复三重调用（EndAbility + Death + BeginDestroy） | 确保任何销毁路径都能恢复通道，不依赖 NotifyEnd 正常执行 |
| 108 | 猎虫耐力在 ResourceComponent 内用纯 float 管理（非 GAS Attribute） | 遵循决策 #43——武器专属资源不在 AttributeSet；词条通过 ApplyEntryModifier 修改倍率参数 |
| 109 | 萃取 Buff 用 Duration GE + GrantedTag（纯 Tag 方案） | 遵循决策 #83——限时 Buff 用 Duration GE；ASC Tag 供连招表/其他系统查询。各萃取灯有不同基础时长 |
| 110 | 三灯不可刷新——由 CheckAndActivateTripleUp 逻辑标志位保证 | UE GE 的 DurationPolicy 无法原生阻止刷新；代码层通过 bTripleUpActive 标志位拒绝重复 Apply。三灯到期后标志位复位 |
| 111 | 三灯到期全部灯消失——GE_IG_TripleUp 不携带单独灯 Tag | 符合 MHW/MHR 核心机制；到期后玩家需重新萃取三个灯 |
| 112 | 红灯改连招用 FComboNode::RequiredTags + Priority | 协调器零改动；同一输入两条节点由 ASC Tag 状态分流；策划在一张出招表中可视化全部红灯/非红灯分支 |
| 113 | 三灯特殊命中音效走 GameplayCue Tag 注入（非 AnimNotify） | 遵循决策 #100——命中反馈统一走 GameplayCue；UMHGZInsectGlaiveAbility 覆写 MakeDamageSpec 额外注入 Tag |
| 114 | 萃取颜色映射为虚函数 MapHitzoneToExtract | 支持不同猎虫品种覆写部位颜色规则——速度型/力量型/回复型猎虫可自定义部位→颜色映射 |
| 115 | 消耗灯→Apply 短时 Buff GE（非永久修改属性） | 消耗灯是战术爆发行为，Buff 应为临时增益。与装备 GE 走不同路径——不触发 OnEquipmentChanged |
| 116 | 三灯后被消耗单个灯→解除三灯、剩余灯继续各自计时 | 遵循 MHW/MHR 逻辑——三灯被破后回归单灯状态（白+黄继续有效），不全部清空。需要重新 Apply 剩余时长的单灯 GE |
| 117 | 猎虫召回时才萃取——不是命中瞬间立即萃取 | MHW/MHR 核心机制——猎虫必须在放出后回到猎人身上才传递萃取。飞行中途召回=萃取失败 |
| 118 | 空中招式按位移来源分 5 类（FBX 自带/准心固定距离/固定垂直+摇杆水平/纯惯性/惯性+摇杆） | 位移来源不同——FBX 有 K 帧 vs 代码驱动。统一方案会导致①类叠加冲突/②③⑤类原地踏步 |
| 119 | AerialVelocity 作为 CMC↔GA 速度交接的中间状态 | 解决 RootMotion/MotionWarping 覆盖 CMC Velocity 导致空中惯性丢失的问题。物理载体始终是 CMC→Velocity，GA 在入口快照、出口回灌 |
| 120 | 分类①②（FBX RootMotion / MotionWarping）在 EndAbility 手动从 GetRootMotionDelta 反算回灌速度 | Montage 结束和 EndAbility 之间可能差 1~数帧，CMC 已恢复 BrakingDecelerationFalling 衰减——直接读 CMC→Velocity 不准确 |
| 121 | 分类③⑤ 用 GAS RootMotion Task + SetVelocity 模式自动保留惯性 | Task 的 SetVelocity 模式在结束时原生的 CMC→Velocity 写入——无需手动 EndAbility 回灌。比 MotionWarping 更适合需要惯性叠加/覆盖的场景 |
| 122 | MotionWarping 不自动保留惯性——位移缩放≠速度控制 | Warp 结束时 CMC 恢复自主物理→速度衰减。需配合决策 #120 手动回灌 |
| 123 | 空中下落用 GameplayTag 区分 Pose（GA 结束，AnimBP 接管） | 下落是状态（state）非能力（ability）；AnimBP 天然适合状态驱动的动画选择；不阻塞其他空中 GA 激活 |
| 124 | 摇杆水平缩放公式 = 1.0 + StickInput.Y（非直接乘） | 不推摇杆=默认距离（非原地）；前推=1.8×/后推=0.2×，符合怪猎操作直觉 |
| 125 | 连招协调器本质是有限状态机（FSM）——FComboNode 定义转移图 | CurrentState=状态、InputAction=转移条件、NextState=转移目标、ActivateAbility=转移动作。bAutoTransition=ε 转移 |
| 126 | GA→GA 自动派生必须通过协调器的 OnAutoTransition（不走 TryActivateAbilityByTag） | 绕过协调器会导致：CurrentState 不同步、PreviousState 断裂、OnAttackFinished 误回 Idle、PendingGrantedTags 丢失 |
| 127 | FComboNode 新增 bAutoTransition 字段区分自动转移与输入转移 | 两条路径共享同一状态变更流程，输入转移走 HandleWeaponInput（四级排序匹配），自动转移走 OnAutoTransition（按 StateName 直接查找） |
| 127b | FComboNode 新增 BlockedStateNames（`TArray<FName>`），仅 `bMatchAnyState==true` 时生效 | `bMatchAnyState` 是"全匹配"开关，但现实中需要"匹配除了 X、Y 之外的所有状态"的语义（如太刀特殊纳刀不可从 Idle 起手、通用追击技排除特定招式）。黑名单模式——只需列出不允许的少数状态名，空数组=原行为。避免了为每个允许的源状态写一行 FComboNode 的爆炸 |
| 128 | 空中动作次数限制用 Cant 模型（CantDodge/CantAttack）而非数字计数器或 Can 模型 | 默认全部可用，用过才加锁。GAS 原生 BlockedTags 处理，零 GA 覆写。容错优于 Can——加锁失败最多多用一次，不会误锁。不需要 Exhausted 汇总标签 |
| 129 | 着陆重置协调器——`Coordinator→OnLanded()` 强制 `CurrentState="Idle"` | CMC OnLanded 是事件而非 GA——不产生伤害/消耗资源。落地后地面连招从 "Idle" 匹配起手 |
| 130 | 瞄准不走 GA——Character 绑定 EnhancedInput 并维护 `Combat.State.Aiming`，AimComponent 只订阅 | GA_Aim 不存在；Tag 仍可供连招条件与 UI 使用 |
| IG-17 | 猎虫移动用 `UProjectileMovementComponent`（`bAutoActivate=false`），不继承 APawn | 当前 Returning Tick 手动追踪 OwnerActor 并更新 Velocity，不使用 HomingTargetComponent |
| IG-18 | 送虫/收虫 GA 进连招表（`bMatchAnyState=true`），不设独立激活路径 | 单一输入路由消除双路径竞态。`bMatchAnyState` 使任意连招节点均可送/收虫。瞄准态分流由 `RequiredTags` + `Priority` 在出招表中可视化控制 |
| ED-0 | 单一 ExecCalc 处理全部伤害来源 | 武器攻击与猎虫伤害复用同一公式，通过 SetByCaller 覆写区分攻击力来源 |
| ED-4 | 硬直走 `HandleGameplayEvent` 而非 Tag Trigger | 每次调用独立触发，InstancedPerExecution 支持受击连打 |
