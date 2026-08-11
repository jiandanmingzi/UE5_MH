# M1 实施审计——Ability 生命周期、输入路由与 FSM

> 状态：M1 代码阶段已完成；基线为 `28d9c5e`（完成 E0/E1）。本文件随 M1 阶段提交保存。E2 编辑器接线尚需用户按 `docs/editor/demo-setup.md` 执行，M2 尚未开始。

## 1. 本阶段边界

M1 只建立通用战斗基底：成本/冷却与资源预留事务、唯一输入所有权、组合键快照、RuntimeHost/TagLedger、ActionToken/Montage Registry、数据驱动 FSM、基础 Dodge 和对应自动化测试。本阶段没有迁移完整 Equipment/Resource 生命周期，没有实现虫棍精华、猎虫、粉尘、舞踏或具体最终动作资产。

## 2. 已落地合同

- `UMHGZInputComponent` 独占 IMC 与 Enhanced Input Binding；重复 Setup、Shutdown、重新绑定不会叠加 Handle。PlayerController 和 ASC 不再保存第二套输入配置。
- `UMHGZWeaponInputRouterComponent` 解析必需成员可任意顺序补齐的组合键，冻结方向/姿态/瞄准上下文，并以 `SourceControlTag + SequenceID` 精确生成 Completed。LT/RT 作为 held modifier，不产生伪离散单键动作。
- `UMHGZGameplayAbility` 的动作实例固定 `InstancedPerExecution`；原生 Instant Stamina GE、Duration Cooldown GE、PerSecond Drain 与武器资源 reservation 使用统一事务。缺 Host、陈旧 RuntimeToken、依赖或 Commit 失败均在副作用边界内结束。
- `UGA_WeaponComboCoordinator` 固定 `InstancedPerActor`，统一执行输入边/自动边，使用 Pending→Commit→Confirm 两阶段交接；旧实例以 `Superseded` 结束后才释放旧转移 Tag，迟到回调按完整 ActionToken 被忽略。
- RuntimeHost 维护 Pawn 姿态、TagLedger、Active Action、资源预留透传和 `(Mesh, MontageInstanceID) → ActionToken` 精确注册表；玩家动作 Notify 不再扫描 Active Ability。
- 基础 Dodge 从输入快照选择方向，使用 AbilityTask 播放 Montage；DodgeWindow 通过精确 ActionToken 获取实例，以 Ledger 持有窗口，并逐通道缓存/恢复碰撞响应。

## 3. 自动化覆盖

`MHGZ.M1` 共 15 项，覆盖：

- Y+B/RT+Y+B 优先级、LT/RT 先按或最后补齐、快速点按、组合形成间无状态泄漏、Completed 身份和 modifier 无伪动作；
- 角色面朝画面左且摇杆向左判定 Forward；
- InputComponent 重复 Setup、Shutdown、重新绑定只有一组回调；
- Idle→A→B、非攻击动作正常回 Idle、同类 GA 连续重入、精确 Montage 身份、Superseded 与迟到回调隔离；
- 自动边门槛、重叠 ComboWindow 计数、TryActivate/Commit 失败保持原状态、reservation 恰好回滚一次、落地清理；
- RuntimeToken 世代隔离、Action/Montage Registry、姿态 Ledger、原生成本/冷却 GE 与 Dodge 缺 Montage 零副作用。

## 4. 验证记录

- `MHGZEditor Win64 Development`：通过。
- `Automation RunTests MHGZ.M1`：15/15 通过。
- `Automation RunTests MHGZ.M0`：5/5 回归通过。
- 独立只读审查发现的通用动作结束未归还 FSM、自动边跳过门槛、两阶段标签释放顺序、modifier 伪单键与 Pending 防御缺口均已修复并纳入测试或运行时校验。

## 5. 下一步

先执行 E2：编译并保存保留蓝图、确认原生 InputComponent/Router 只有一份、给 Character 添加唯一 RuntimeHost、清除旧蓝图输入逻辑并完成 GameMode 关系接线。E2 不创建最终 InputProfile/Combo/Combat 壳，不删除 E3/E4 才处理的旧动作资产。E2 完成并提交后再开始 M2。
