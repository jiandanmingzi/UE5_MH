# M3 实施审计——虫棍资源、基础猎虫、精华与瞄准

> 审计基线：E3 提交 `eeead0d` 之后的 M3 工作树。用户负责阶段性 Git 提交。

## 1. 实施结果

- `URes_InsectGlaive` 由 RuntimeHost 注入唯一 `UInsectGlaiveCombatConfig`，管理猎虫 Actor、耐力、三种单灯 Active GE Handle、Triple Handle/Reservation、唯一虫印和资源标签所有权；不再硬编码 `/Game/...` 路径，也不维护 `bTripleUpActive/bKinsectDeployed` 第二真相源。
- 单灯采用“新 GE 确认 Active 后才替换旧 Handle”；三色齐全时先确认 Triple GE Active，再原子移除三单灯。三灯期间所有 `ApplyExtract` 入口吞灯且不刷新；觉虫击成本以当前 Triple Handle 预留并在 GAS Commit 成功后消费。
- `AKinsect` 使用 Collision Root、ProjectileMovement UpdatedComponent、Previous→Current Hitzone Object Capsule Sweep 和真实 `FHitResult`；普通送虫命中后 Hover，主动/耐力召回时追踪 `Kinsect_Arm_Socket`，到达后原子取出 Pending 并交付一次。
- 贯通 Flight 以一个 FlightInstanceID 追踪请求，但每次成功命中生成独立 HitInstanceID 写入 GameplayEffectContext，避免目标侧 `IncomingHitResolver` 把后续贯通 Tick 误判为重复攻击。
- `AIGMarkProjectile` 从带 `WeaponTrace` ComponentTag 的武器 Mesh `IG_FrontTip` 发射，按冻结的 Aim TargetPoint 计算弹道；代码 Sweep Hitzone，WorldStatic 阻挡，Resource 维护唯一虫印与时长/目标/卸装清理。
- 新增四个原生 GA 父类：送虫、召回、拔刀直飞、虫印弹。它们只从不可变 ActivationContext 构造请求，Validate 在 GAS Commit 前完成方向/姿态/资源预检；E4 蓝图子类只负责数据与表现接线。
- `FWeaponChordDefinition` 新增通用 Required/Blocked ContextTags。收刀 RT 单成员 Chord 可立即触发；持刀 LT+RT 支持任意顺序；收刀 RB 不产生 Router 离散快照，仅由 Character 的 0.1 秒长按逻辑处理。
- RuntimeHost 现在在武器建立/卸载时向 PlayerController Router 注入/清空 RuntimeDefinition.InputProfile；Character/Aim 按 ASC 生命周期幂等绑定，Aim 子标签由 Router 的 TagLedger Token 拥有并在 Runtime Generation 变化后重建。
- 虫棍本体的通用 AttachSocket 默认改为左手 `Weapon_L`。猎虫不与武器共用该 Socket，而使用独立 `Kinsect_Arm_Socket`。

## 2. 自动化覆盖

新增 `MHGZ.M3` 测试：

1. `Resource.SingleExtractsEstablish`
2. `Resource.TripleApplyFailureKeepsSingles`
3. `Resource.TripleUpActivatesSwallowsAndDoesNotRefresh`
4. `Resource.TripleAtomicConsumeAndReservations`
5. `Kinsect.FlightRequestValidationIsAtomic`
6. `Kinsect.PendingExtractAtomicTakeAndReturnDelivery`
7. `Abilities.LTYSendAimSnapshotConstruction`
8. `Abilities.RTDrawSendActorForwardSnapshot`
9. `Input.ContextChordsRouteRTLTRTAndSheathe`

测试使用 transient World/ASC/Equipment/RuntimeHost/CombatConfig/KinsectData 与原生测试 GE，不依赖 E4 蓝图资产。高速 Sweep 捕获失败时测试明确失败，不再降级为 Info 后通过。

## 3. 构建与审查证据

- UnrealHeaderTool：通过。
- `MHGZEditor Win64 DebugGame` 完整构建通过，包括 `UnrealEditor-MHGZ-Win64-DebugGame.dll` 链接。
- `Automation RunTests MHGZ.M3`：9/9 通过，命令行退出码 0。
- `Automation RunTests MHGZ`：39/39 通过，覆盖 M0～M3，命令行退出码 0。测试命令在当前受限环境使用 `-DDC-ForceMemoryCache`；全局 DDC 不可写的启动日志不影响测试结果。
- 关闭编辑器后，`MHGZEditor Win64 Development` 已以最终源码完成 UHT、全部编译与 `UnrealEditor-MHGZ.dll` 链接。
- 使用正式 Development DLL 再运行 `Automation RunTests MHGZ.M3`：9/9 通过，命令行退出码 0。
- 独立只读审查未发现 Critical；发现并已修复：能力测试姿态必红、Validate 迟于 Commit、Sweep 伪覆盖、陈旧 Aim Token、拔刀直飞部分提交、悬停碰撞残留、回手先清 Pending、无伤害萃取顺序。
- 父级额外修复：RuntimeDefinition.InputProfile 未注入 Router、RT/LTRT Chord 上下文时序、贯通伤害身份被 Resolver 全局去重、虫印弹错误从相机原点生成、猎虫/武器 Socket 所有权混淆、GE 移除回调修改遍历中容器、换武器时旧版 `Kinsect.Active` 计数残留、Hitzone 组件单独销毁后虫印迟延清理、Input Release 回调重入修改 ActiveActions，并统一 FlightInstanceID/HitInstanceID 身份命名。

## 4. M3 代码阶段状态

M3 生产代码、DebugGame/Development 构建、M3 双配置测试、M0～M3 全量回归与独立审查均已通过。M3 已完成，可用新反射类重开编辑器并进入 E4 资产接线。

## 5. E4/E5 编辑器交接

严格按 [编辑器接线指南](../editor/demo-setup.md) 第 5.4、6、7.1 节执行：

- 给现有 `DA_IG_InputProfile` 增加 RT 单成员 Chord，并为 RT、LTRT、Sheathe 填写文档列出的 ContextTags。
- 创建四个精华 GE 与四个基础猎虫 GA 蓝图子类，回填 Combo/Combat 引用。
- E4 先创建数据型 `DA_IG_Kinsect_Speed` 并回填必需 KinsectData；E5 再接正式外观/动画。
- 角色武器继续挂 `Weapon_L`；另建并校准 `Kinsect_Arm_Socket`；虫印发射点校准武器 `IG_FrontTip`。
