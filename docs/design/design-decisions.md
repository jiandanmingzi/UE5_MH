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

> 后序决策可明确取代早期决策。尤其 #143 将早期记录中的 `FComboNode/ComboTable/StateName/NextState` 统一迁移为 `FComboTransition/Transitions/SourceState/TargetState`；旧名称只用于解释当前源码，不再作为目标接口。

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
| 16b | （已被 #155 取代）早期按 WeaponTypeTag 查 DT_WeaponComboConfig | 保留为当前源码历史，不再作为目标运行时入口 |
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
| 36 | UI 订阅玩法状态，但 UI 展示状态不写回 ASC | Character 只维护 `Combat.State.Aiming.Kinsect` 等玩法 Tag；AimComponent 用 Delegate 传目标/颜色，Widget 不创建 `UI.Aim.*` Loose Tag |
| 37 | 翻滚不进连招表——独立 GA_Dodge（配置入口由 #167/#168 更新） | 翻滚是取消/中断动作，非连招的一环；旧 DT_WeaponDodgeConfig 不再作为最终运行时入口 |
| 38 | 受击判断 = GE Execute 时被动检查；霸体通过 GameplayTag + AnimNotifyState_PoiseWindow | 不需要 Tick 轮询、不需要"受击监听组件" |
| 39 | VFX/SFX/镜头三层分工——不设独立系统 | 帧同步（AnimNotify）/ 状态驱动（GameplayCue）/ 镜头（Ability 内 CameraModifier） |
| 40 | ~~InputComponent 只管 IMC、ASC 管 IA→Tag~~（由 #142/#162 取代） | 复审发现 ASC 跨 Avatar 的 `bInputBound` 无法可靠重绑；最终由 InputComponent 独占 IMC/Binding，Router 解析快照 |
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
| 75 | Demo 伤害基线 = AttackPower × MotionValue × HitzoneDefense × Crit | 只用于木桩闭环；完整斩味、属性、异常等在同一 DamageContext/ExecCalc 扩展，不阻塞动作 Demo |
| 76 | 单碰撞多跳伤害（MultiHitCount>1） | 登龙剑下批：1 次 Sweep + Timer 驱动多次 Apply |
| 77 | 武器 Ability 基类分化——每种武器一个 C++ 中间类 | 约 50 行，持有 ResourceComponent 引用+覆写资源门控钩子 |
| 78 | 武器资源子系统不统一具体数值模型；词条入口原方案由 #170 延期 | 各子类自行管理特有字段；当前 ApplyEntryModifier 还不能可靠处理多来源和目标参数 |
| 79 | 见切判定——bDodgeSuccessful + GameplayEvent 驱动 | 不拆成两个 GA——后撤+回砍是一个完整动作 |
| 80 | 登龙招内派生——ShouldContinueAfterHit 钩子 | 招内派生（同一 GA），不同于连招表的跨 GA 派生 |
| 81 | 见切继承 UMHGZAttackAbility——非攻击段用 Damage=0 + 独立 AnimNotifyState | 段0 用 GameplayEvent（怪物→玩家），段1 用 Sweep（玩家→怪物） |
| 82 | ASC/持久角色数据在 PlayerState；WeaponRuntimeHost/武器资源在 Character | 猎虫、粉尘、舞踏、动画和位移属于 Pawn 生命周期；通过接口访问 ASC，不把世界 Actor 引用塞入持久层 |
| 83 | 限时 Buff = Duration GE + GrantedTag（纯 Tag 方案） | 资源组件/AttributeSet Tick 中读 Tag 决定实际效果 |
| 84 | （已被 #143/#158 取代）翻滚取消攻击后回 Idle | 目标必须用被取消动作的 Handle+Sequence 完成回执并释放其 Token；不调用无身份 OnAttackFinished() |
| 85 | 预输入优先级高于帧批处理 | ComboWindow NotifyBegin 先消费 PreInputTag |
| 86 | 死亡保留装备 GE，清除 Buff GE | RemoveActiveEffectsWithGrantedTags(Combat.Buff) 批量清除 |
| 87 | MotionWarpingComponent 挂载到 Character | 需 SkeletalMeshComponent+AnimBP 管线，PlayerState 不具备 |
| 88 | 预输入 Chord Trigger 天然正确，单槽足够 | Chord 回调总是最后到达，后覆盖前 |
| 89 | QuickBar 挂载到 PlayerController | 输入/反馈层组件，通过 GetPlayerState() 一次跳转访问 Backpack |
| 90 | QuickBar 音效与执行分离 | 切换选中/使用选中时无条件播放音效 |
| 91 | （已被 #155 取代）早期 DT_WeaponResourceConfig 映射 Resource/Widget | 目标由 WeaponRuntimeDefinition 统一接线，不创建第二张运行时表 |
| 92 | 仓库 UI 仅显示 InStorage 物品 | 仓库只管存储——已装备/已镶嵌物品不出现在仓库列表中 |
| 93 | EquippedItems/SocketedAccessories 存指针 | 对象本体始终在 WarehouseComponent::Slots 中 |
| 94 | WeaponComboData 当前同步加载并通过 InjectComboData 注入 | RequestID 异步竞态保护是保留的后续方案 |
| 95 | EDirectionalInput 字段已定义但协调器尚未消费 | 方向快照与象限匹配是保留的后续方案 |
| 96 | AnimNotifyState_ForesightJudge 协作模式 | NotifyState 管理窗口 + GA 管理事件委托 |
| 97 | 关卡切换需显式复制持久 DTO 并重建 Pawn 运行时 | Seamless Travel 不保证组件/UObject/Actor 引用图自动保留；具体旅行与存档不阻塞当前单地图 Demo |
| 98 | MultiHitTimer 清理——三重保障 | `DisableCollision` 正常路径 + `EndAbility` 取消路径 + `BeginDestroy` 销毁兜底均有清除 |
| 99 | 最终伤害经 HitFeedbackRouter 显式执行 DamageNumber Cue | DynamicAssetTag 不会自动触发 Cue；Router 负责传真实 HitResult 和 RawMagnitude |
| 100 | 命中表现使用 GameplayCue，但由结算结果显式路由 | 伤害权威与表现分离；物理/元素/暴击/数字可组合，失败不影响伤害 |
| 101 | 一次命中用 `UGameplayCueNotify_Burst`，持久 Buff 用 `AGameplayCueNotify_Actor`/Looping | Burst 是非实例化一次性 Notify；不把 BurstLatent 当通用持久 Buff |
| 102 | 武器拖尾不入 GameplayCue | 拖尾需帧精确匹配动画（AnimNotify 管理 start/stop），属帧同步范畴 |
| 103 | 伤害数字 WorldSubsystem 池化拥有 WidgetComponent 的 Actor | WidgetComponent 需要 Actor/世界所有权；Pool 记录 Peak/Drop，池耗尽不阻塞伤害 |
| 104 | FComboNode 去掉 ComboTimeout，协调器使用 GlobalComboTimeout 统一兜底 | 避免策划每行配置超时，减少遗漏和出错 |
| 105 | FComboNode 去掉 bResetsComboLevel | 连段计数系统未定义，无消费方——后续需要时由武器基类自行管理 |
| 106 | 蓄力释放走 GameplayEvent（Combat.Event.ChargeReleased） | 蓄力 GA 被打断后 Charging Tag 已移除→事件不触发，避免意外释放；不依赖查找 ActiveAbilities |
| 107 | MonsterAttack 通道强制恢复三重调用（EndAbility + Death + BeginDestroy） | 确保任何销毁路径都能恢复通道，不依赖 NotifyEnd 正常执行 |
| 108 | 猎虫耐力在 ResourceComponent 内用纯 float 管理（非 GAS Attribute） | 遵循决策 #43；Demo 直接读取 CombatConfig，WeaponResource 词条由 #170 延期 |
| 109 | 红/白/橙精华 Buff 用 Duration GE + GrantedTag | 限时 Buff 由 GE 管理；颜色直接配置在怪物 Hitzone，Aim/猎虫/操虫斩共享 |
| 110 | （已被 #149 取代）早期方案用 bTripleUpActive 防刷新 | 保留为历史记录；目标不再维护第二个 bool，而查询 ASC 中对应 Triple Handle 的 ActiveEffect |
| 111 | 三灯 GE 不携带三个单灯状态 Tag，但携带 `Combat.Branch.Extract.Red` 动作权限 | Classic 三灯期间继续使用完整动作；Triple 到期后该权限一起移除，玩家重新萃取 |
| 112 | 红灯支持 ClassicMovesetGate 与 NumericOnly，默认 Classic | 模式保存在虫棍 CombatConfig；经典模式用 Required/BlockedTags 分流弱化/完整动作，数值模式动作不分流 |
| 113 | 三灯特殊命中音效走 GameplayCue Tag 注入（非 AnimNotify） | 遵循决策 #100——命中反馈统一走 GameplayCue；UMHGZInsectGlaiveAbility 覆写 MakeDamageSpec 额外注入 Tag |
| 114 | 萃取颜色由怪物 Hitzone 的 ExtractColorTag 决定 | 不同怪物同名部位可以有不同颜色；猎虫品种不能改写怪物部位颜色 |
| 115 | Demo 只有觉虫击原子消耗三灯；降龙不消耗精华 | 旧 Extract Surge/Triple Burst 和“降龙消耗红灯”方案废弃 |
| 116 | 三灯期间所有吸收路径统一吞掉单色精华 | 不创建单灯 GE、不缓存颜色、不刷新三灯；操虫斩和普通猎虫使用同一 ApplyExtract |
| 117 | 普通猎虫回手时交付 PendingExtract，主动召回/耐力归零不使其失败 | 飞行命中记录颜色；从未命中有效颜色才是萃取失败 |
| 118 | 空中位移按动画根运动、受限定向、弹跳、惯性叠加、自由下落分类 | 零根位移动画不用 MotionWarping 生成任意距离；运行时位移由统一 Task 输出 FinalVelocity |
| 119 | AerialVelocity 作为 CMC↔GA 速度交接的中间状态 | 解决 RootMotion/MotionWarping 覆盖 CMC Velocity 导致空中惯性丢失的问题。物理载体始终是 CMC→Velocity，GA 在入口快照、出口回灌 |
| 120 | 需要惯性的动作由位移任务或经验证的动画提取器显式输出 FinalVelocity | `GetRootMotionDelta` 只代表查询帧，不能在 EndAbility 猜测整段动作末速度 |
| 121 | 受限定向、弹跳和惯性叠加使用带碰撞/距离/时长终止的通用位移 Task | 每次位移只有一个所有者；正常、取消、碰墙、落地都走同一清理协议 |
| 122 | MotionWarping 不自动保留惯性——位移缩放≠速度控制 | Warp 结束时 CMC 恢复自主物理→速度衰减。需配合决策 #120 手动回灌 |
| 123 | 空中下落用 GameplayTag 区分 Pose（GA 结束，AnimBP 接管） | 下落是状态（state）非能力（ability）；AnimBP 天然适合状态驱动的动画选择；不阻塞其他空中 GA 激活 |
| 124 | 方向和惯性参数由动作/武器 Config 提供，不使用全武器固定公式 | 不同武器和动作需要不同空中控制；通用 Task 只执行参数 |
| 125 | FComboNode 实际是一条 FSM 转移边 | StateName=SourceState、NextState=TargetState、AbilityClass=边动作；多个同源出边可以激活不同 GA |
| 126 | GA→GA 自动派生通过唯一 TransitionID 进入协调器 | 不能按 TargetState 取第一条出边；AbilityHandle 所有权避免旧 GA 结束重置新状态 |
| 127 | FComboNode 新增 bAutoTransition 字段区分自动转移与输入转移 | 两条路径共享同一状态变更流程，输入转移走 HandleWeaponInput（四级排序匹配），自动转移走 OnAutoTransition（按 StateName 直接查找） |
| 127b | FComboNode 新增 BlockedStateNames（`TArray<FName>`），仅 `bMatchAnyState==true` 时生效 | `bMatchAnyState` 是"全匹配"开关，但现实中需要"匹配除了 X、Y 之外的所有状态"的语义（如太刀特殊纳刀不可从 Idle 起手、通用追击技排除特定招式）。黑名单模式——只需列出不允许的少数状态名，空数组=原行为。避免了为每个允许的源状态写一行 FComboNode 的爆炸 |
| 128 | 空中许可继续用 CantDodge/CantAttack；舞踏用虫棍资源中的显式层数 | 只有操虫斩命中和突进回旋斩反击触发舞踏；层数上限与逐层倍率数据化 |
| 129 | 着陆重置协调器——`Coordinator→OnLanded()` 强制 `CurrentState="Idle"` | CMC OnLanded 是事件而非 GA——不产生伤害/消耗资源。落地后地面连招从 "Idle" 匹配起手 |
| 130 | 瞄准不走 GA，但拆分 Kinsect/Action/Slinger 三种上下文 | LT/RT 在收刀、持刀、空中含义不同；AimComponent 只订阅 `Aiming.Kinsect`，不写 UI Tag 到 ASC |
| IG-17 | 猎虫移动用 `UProjectileMovementComponent`（`bAutoActivate=false`），不继承 APawn | 当前 Returning Tick 手动追踪 OwnerActor 并更新 Velocity，不使用 HomingTargetComponent |
| IG-18 | 持刀 LT+Y/LT+B 放收虫，收刀只有 RT 可拔刀直飞 | 普通 Y/B 留给武器连招；地面/空中状态解决同一组合输入的动作分流 |
| ED-0 | Demo 单一 ExecCalc 处理全部伤害来源 | 武器、猎虫和粉尘复用简化公式；DamageContext 保留后续完整伤害模型扩展位 |
| ED-4 | 硬直走 `HandleGameplayEvent` 而非 Tag Trigger | 每次调用独立触发，InstancedPerExecution 支持受击连打 |
| 131 | 长武器碰撞按有效区建模为多个 `FWeaponTraceRegion`，不固定为单一整棍线段 | 握持点在模型中心时可直接用 Root Bone 作为前后 Region 的共同边界；前段、后段和整棍招式只需组合不同 Region |
| 132 | 每个 AttackCollision ConfigIndex 拥有独立运行时窗口 | Notify 可重叠；轨迹历史、目标去重和多跳 Timer 不再被另一个段的 Begin/End 覆盖 |
| 133 | 新 Region 使用自适应多球 Sweep + 旋转时间子步 | 空间采样间距不超过球直径，帧间高速旋转按角度细分，兼顾长棍覆盖率和性能上限 |
| 134 | 同帧命中先汇总，再按归一化帧时间为每个 Actor 选择最早 Hitzone | 结果不依赖 Region/采样循环顺序，肉质结算更接近武器首次接触部位 |
| 135 | 项目以《世界》地面/猎虫为基底，选择性改造三部曲动作 | 不逐项复刻单作；动作来源与最终玩法规则分离 |
| 136 | Demo 不包含翔虫资源、集中模式或钩爪 | 移植动作不自动继承原作资源系统；当前只实现木桩战斗闭环 |
| 137 | 虫棍专属 CombatConfig 保存红灯模式、舞踏和特殊动作参数 | 通用 GA/协调器不增加虫棍分支，便于以后添加其他武器 |
| 138 | 舞踏只由操虫斩命中和突进回旋斩反击触发 | 层数和倍率可调；落地、受击、急袭突刺、降龙等统一清空 |
| 139 | 虫棍特殊/位移动作单独维护在 insect-glaive-actions.md | 普通地面招式继续用 ComboData；避免在资源与通用动作文档重复规则 |
| 140 | 持刀 LT+RT 发射虫印弹；每名玩家只维护一个虫印 | 虫印附着 Hitzone、时长可调，新印替换旧印；目标失效/卸装清除，收刀不清除 |
| 141 | 动代码前先冻结 [Demo 实施计划](demo-implementation-plan.md)，按 M0～M7 逐阶段验收 | 防止公共接口未定时批量创建 GA/蓝图，造成重复迁移和遗留清理路径 |
| 142 | 武器物理输入由 PlayerController 上的 InputComponent 绑定、WeaponInputRouter 解析，ASC 只接收快照/激活上下文 | Chord、方向和释放身份必须原子快照；删除 ASC 对任意 Completed 的全局解释；唯一绑定所有权见 #162 |
| 143 | 目标连招结构正式更名 `FComboTransition/Transitions/SourceState/TargetState` | #24～#127 中的 `FComboNode` 仅代表旧源码名称；数据实际是转移边，不永久维护双结构兼容层 |
| 144 | Ability 成本策略固定为 None/Instant/PerSecond，持续存在与持续扣耐分离 | Instant/Cooldown 使用有效 GE；PerSecond 由带所有权 AbilityTask 结算；废弃空 Spec 和永久 Loose Cooldown |
| 145 | 碰撞固定为 Hitzone Object Channel + 猎虫显式前后帧 Capsule Sweep | 不新增 Kinsect Object Channel；Weapon 保持攻击 Trace，Hitzone 提供对象身份，猎虫物理阻挡与部位命中分离 |
| 146 | 真实 HitResult/AttackInstanceID 存入自定义 GameplayEffectContext | ExecCalc 只计算 Meta 输出，AttributeSet 结算，HitFeedbackRouter 显式执行 Cue/数字/卡肉；不靠 DynamicAssetTag 自动触发 |
| 147 | RuntimeHost 是 Pawn 武器资源唯一创建/销毁者，并按固定顺序 Shutdown | 输入→Ability→位移/Timer→虫棍 Actor→GE/Tag→Resource 的顺序避免换装、死亡和 PIE End 残留 |
| 148 | 非纯动画位移统一使用带 Token 与 Result 的 MovementTask 族 | CMC 是唯一物理载体；Bounded/Ballistic/Additive 共享碰撞、取消和 FinalVelocity 合同 |
| 149 | 三灯以 ASC 中仍 Active 的 TripleUp GE Handle 为唯一真相源 | Handle.IsValid 不能单独证明 GE 存活；不维护失步布尔，ApplyExtract 入口先吞灯，觉虫击调用原子消费接口 |
| 150 | 公共结构重命名与资产迁移在对应里程碑一次完成 | 不用永久兼容层掩盖旧字段；每个阶段通过 Data Validation、自动化测试和 PIE 退出条件后再继续 |
| 151 | 武器成本使用 `FWeaponResourceCostSpec{CostType, Amount}` 数组；事务顺序由 #166 补充 | 单一 float 无法统一三灯、瓶、色阶等离散资源；通用 GA 只透传 Tag，不解释武器语义 |
| 152 | 怪物/木桩对玩家命中先进入 IncomingHitResolver，反击用有所有权 Token 前置消费 | Counter 不等于无敌；同一 AttackInstanceID 权威去重后再决定 Consume 或 Apply GE，可复用于其他武器 |
| 153 | 虫棍只使用一个 CombatConfig 和一个 ComboData，红灯模式由互斥配置 Tag 分流 | 不复制精华调参或整张出招表；默认 Classic，切换时 RuntimeHost 原子替换 Mode Tag 并 ResetCombo，不重授 Ability |
| 154 | Aim 使用 Visibility 并验证 Hitzone ObjectType；猎虫才使用 Hitzone Object Sweep | 单纯 Object Query 会穿过 WorldStatic；木桩 Body 对 Visibility Ignore、Hitzone/墙 Block，既能选部位又受墙遮挡 |
| 155 | 每个武器类型用一个 WeaponRuntimeDefinition 统一 Resource/Input/Combat/Widget 接线 | 具体武器物品只引用该资产；M2 后不再运行时读取 DT_WeaponComboConfig/ResourceConfig 两套全局桥接 |
| 156 | 武器运行时身份使用 `FWeaponRuntimeToken{Host, Generation}`，不能只比较 Generation 数字 | 两个不同 Character 的本地世代都可能从 1 开始；所有延迟回调同时验证 Host 与世代才能拒绝旧 Pawn |
| 157 | ~~武器资源 UI 工厂迁移为 LocalPlayerSubsystem~~（由 #165 取代） | 复审发现 WBP_HUD 资源插槽与 Subsystem AddToViewport 会形成双 Widget 所有者 |
| 158 | 当前 Pawn 的临时 Loose Tag 统一经 RuntimeHost TagLedger 返回有所有权 Token | 窗口重叠、Montage 中断和旧 Ability 迟到 End 时只释放自己的计数；精华/成本/冷却等持续状态仍由 Active GE Handle 管理 |
| 159 | 普通送虫 SingleHit 命中后固定 Hover，主动召回或耐力归零才 Return 并交付精华 | 不因取得 PendingExtract 自动回手；悬停阶段关闭伤害与 Hitzone Sweep |
| 160 | 觉虫击每次成功贯通伤害立即按 Hitzone ApplyExtract，并一一对应生成粉尘 | Commit 先消费旧三灯；贯穿可重新凑三灯，形成后三灯继续吞灯且不刷新；伤害失败不点灯、不产粉尘 |
| 161 | 离散武器动作 GA 固定 InstancedPerExecution，并由 ActionToken+MontageInstanceID 精确解析 Notify | 同一 Spec/类连续重入时旧 BlendOut、NotifyEnd 和 EndAbility 不能污染新实例；协调器保持 InstancedPerActor |
| 162 | InputComponent 独占 IMC 与 Enhanced Input Binding；ASC 和 PlayerController 不建立第二套绑定 | 重复 Possess/Avatar 替换必须可解绑重建；Router 只解析快照，通用动作和武器动作共用 ResolvedInput 入口 |
| 163 | 装备统计变化与武器身份变化分流；RuntimeHost 只对 WeaponSnapshot 身份变化重建 | 护甲、饰品、镶嵌或重复广播不能清空精华、舞踏和猎虫状态 |
| 164 | Movement Token 同时拥有平移、旋转、steering 与唯一 WarpTarget | Character Tick 和动作任务不能并发写旋转；每个结束路径只移除本动作 WarpTarget |
| 165 | AMHGZHUD 是本地 Widget 树唯一所有者；资源面板只插入 WBP_HUD，删除空壳 UISubsystem | 每个 PlayerController/HUD 天然隔离本地玩家；消除 AddToViewport 与资源插槽双所有者 |
| 166 | 武器成本使用 reservation 跨越 GAS Commit：Reserve→Commit→Release/Consume→Confirm | GAS Commit 失败不丢三灯；成功后的 Consume 保证不失败，避免部分耐力/冷却与部分武器资源状态 |
| 167 | 基础 Dodge 纳入 M1 重写 | 它是战斗基底；必须先验证输入快照、AbilityTask、ActionToken、TagLedger 和碰撞响应恢复 |
| 168 | （已被 #171 细化）以 Keep/Rewrite/Delete/Defer 表决定旧资产处置 | 仍保留“不覆盖用户改动、不保留双运行时”的原则；具体旧虫棍原型不再走重存迁移 |
| 169 | PendingExtract 回手时原子取出并清空；耐力归零只在阈值边沿触发一次 | 支持连续取得不同颜色，避免 0 耐力每帧重复召回和音效 |
| 170 | Demo 禁用 WeaponResource EntryModifier，完整词条阶段重新设计多来源句柄与参数过滤 | 当前 FindOrAdd/遍历语义会丢来源或把修饰应用到错误参数；木桩 Demo 不应建立在已知错误路径上 |
| 171 | 旧虫棍 DT→最小 Combo→两个 GA→两个无自定义 Notify Montage 与零引用武器定义采用删除重建，不迁移其字段 | UE 5.6 引用审计证明这批资产只含最小原型数据；地图、核心 BP、AnimBP、原始 AnimSequence、美术与输入基础继续保留。M2 先解除旧读取，E3/E4 再按精确清单删除并从最终类型新建 |
