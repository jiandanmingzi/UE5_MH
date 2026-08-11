# M2 实施审计

> 状态：M2 代码阶段已完成；基线为用户完成 E2 后的 `f16c605`。本阶段没有创建、移动或删除 `.uasset/.umap`，也没有执行 Git 暂存或提交；版本保存由用户负责。下一步是按 [编辑器接线指南 §4.5/§5](../editor/demo-setup.md#45-m2-代码完成后的-e3-前检查) 完成 E3，之后进入 M3。

## 1. 本阶段合同

M2 只建立后续虫棍系统依赖的通用战斗基底，不实现虫棍精华、猎虫飞行、粉尘、舞踏或最终动作资产：

- Equipment 只广播不可变武器 Snapshot；Character RuntimeHost 独占 Pawn/世界运行时对象。
- 武器命中保留真实 `FHitResult`，伤害只由 ExecCalc 计算，AttributeSet 一次结算并生成反馈结果。
- 玩家 IncomingHit 在目标侧去重并经过绑定当前 ActionToken 的反击拦截链。
- Attack 只使用最终 `TraceRegions` 运行时路径；旧字段保留为不参与决策的序列化壳。
- 木桩 C++ 提供三个颜色部位与确定性测试攻击能力；实际 Content 配置延期到 E5。
- 退役旧 `DefaultGame.ini → DT_WeaponComboConfig → Equipment` 运行时链，为 E3 删除旧资产解除代码引用。

## 2. 已实现内容

### 2.1 Equipment 与 RuntimeHost

- `FEquippedWeaponSnapshot` 冻结 EquipmentInstance、WeaponDefinition、RuntimeDefinition 与单调修订号。
- `OnEquipmentStatsChanged` 与 `OnEquippedWeaponChanged` 分离；护甲、饰品、镶嵌或同一武器重复广播不会重建武器 Runtime。
- RuntimeHost 订阅 Snapshot，真实身份变化执行 `Teardown → Generation+1 → Rebuild`；Context 的 RuntimeToken 与 Host 当前 Token 同步。
- RuntimeDefinition 是 ResourceClass/InputProfile/CombatConfig 的唯一入口。Host 创建 Pawn-owned Resource、唯一收集并授予 Combo 中的 Ability、显式持有和回收 Coordinator SpecHandle。
- 清理顺序覆盖旧 Token 失效广播、Coordinator/Weapon Ability、Resource Shutdown、HitStop、TagLedger、Action/Montage Registry 与动态组件销毁；创建中途失败也先 Shutdown Resource 再销毁。
- `DefaultGame.ini` 的旧 WeaponComboConfig 行、DataManager Getter 和 Equipment DataTable 查询已删除。

### 2.2 EffectContext、伤害与反馈

- `FMHGZGameplayEffectContext` 携带真实 HitResult、AttackInstanceID、来源动作/类型、Hitzone、Cue、硬直、卡肉和镜头参数，并实现 Duplicate/NetSerialize 接口。
- ExecCalc 从真实 HitComponent 读取 Hitzone Defense/StaggerRate，从 SetByCaller 读取 MotionValue/BaseStagger 与可选覆盖，计算 Damage/Stagger/Critical 后按固定顺序写入四个 Meta，HitSignal 最后。
- `MotionValue <= 0` 不扣血；AttackPowerOverride=0 被视为有效零覆盖；只有正 RawDamage 才进行会心随机。
- AttributeSet 在 HitSignal 到达时一次读取并清零四个 Meta，按当前 Health 得到 ActualDamage，再生成一个 `FMHGZHitFeedbackResult`。首次 Health 归零会关停 Pawn RuntimeHost。
- `UMHGZHitFeedbackRouterComponent` 只消费已结算结果，显式执行 Hit/Element/DamageNumber Cue，并提交卡肉与镜头请求，不重算伤害。
- `UMHGZHitStopControllerComponent` 用独立 Token 合并重叠请求，保存并恢复进入前的 `CustomTimeDilation`；换武器、死亡和 EndPlay 清空全部请求。

### 2.3 Attack 与多跳

- `UMHGZAttackAbility` 的 Sweep 运行时只读 `TraceRegions`；同帧多个 Region 命中同一目标时按最早 Hit.Time 选择一次结算。
- 真实 `FHitResult` 全程进入伤害 Spec；只有 `ApplyGameplayEffectSpecToTarget` 实际成功才触发 FirstHit/OnHitSelfEffect。
- 一次 Ability 激活生成稳定 AttackInstanceID；默认 Contact 策略不会离开碰撞后继续跳伤。
- 只有显式 `LockedTargetTicks` 才启动每目标 Timer，并在每跳检查目标、Hitzone、距离、次数和间隔。
- 每次动作激活使用带 Runtime Generation/ActivationSequence 的独占 Motion Warp TargetName，End 时只移除自身目标。

### 2.4 IncomingHit 与木桩

- Character 原生持有 `IncomingHitResolver`、`HitFeedbackRouter`、`HitStopController`。
- Resolver 拒绝无效 ID、非真实 Hit 和 HitActor 不等于 Resolver Owner 的提交；Applied/Consumed 都进入有 TTL/容量的权威去重缓存，Apply 失败回滚。
- 反击拦截器必须绑定当前 RuntimeHost 的有效 ActionToken，按 Priority 降序、TokenID 升序调用，并支持 TTL、单个注销、全部注销和旧 Generation 自动失效。
- Monster ASC 正式注册 AttributeSet；Hitzone 组件携带 HitzoneTag、ExtractColorTag、DefenseMultiplier 与 StaggerRate。
- `UMHGZDummyConfig` 校验恰好一个 Red/White/Orange、正半径和不重叠球体；`AMHGZTrainingDummy::SubmitCounterTestAttack` 用固定 Capsule Hit 与外部 AttackInstanceID 提交可重复测试。

## 3. 验证证据

### 3.1 构建

使用 UE 5.6 Development Editor 目标完整编译：

```powershell
Build.bat MHGZEditor Win64 Development -Project=D:\study\MH\MHGZ\MHGZ.uproject -WaitMutex -NoHotReloadFromIDE -architecture=x64
```

结果：`Succeeded`，UHT 与 C++ 编译、链接均通过。

### 3.2 自动化测试

命令行编辑器在当前沙箱账户下使用内存 DDC：

```powershell
UnrealEditor-Cmd.exe MHGZ.uproject -unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache -ExecCmds="Automation RunTests MHGZ.M;Quit" -TestExit="Automation Test Queue Empty" -log
```

结果：发现并通过 30/30 项测试：

- M0：5 项（EffectContext 分配/复制、TagLedger、数据校验）。
- M1：15 项（输入、FSM、GA/成本、RuntimeHost、Dodge）。
- M2：10 项（Context/零伤害、木桩/配置、Equipment Snapshot、HitStop、IncomingHit、Runtime 重建）。

`git diff --check` 通过。静态扫描确认：

- Config/Equipment/DataManager 无 `WeaponComboConfig`/`GetWeaponComboConfig`/`GetAllRows` 运行时读取。
- Attack cpp 无旧 `TraceStartSocketName/ShapeExtent/TraceSampleCount` 决策路径。
- `CustomTimeDilation` 只有 HitStopController 负责写入；测试代码只设置原值用于恢复验证。

### 3.3 独立审查闭环

最终实现接受了一次独立只读审查。审查发现的行为问题已在重新构建和 30 项联合回归前修复：

- 虫棍 Resource 的 `Kinsect.Active` Loose Tag 在重复 Deploy、换武器、死亡和卸装时统一把计数归一/清零，并加入换武器回归断言。
- `bUseHitzoneDefense=false` 通过自定义 EffectContext 传入 ExecCalc，硬直肉质仍独立生效；测试同时覆盖启用/禁用两条伤害路径。
- RuntimeHost 在 Teardown 同步回调期间缓存最后一个装备 Snapshot，当前重建结束后重放，避免 Equipment/Host 因重入长期分歧。
- 一次 GA 激活共享稳定 AttackInstanceID 的合同已在代码注释与 `actions.md` 统一；每次命中仍携带各自真实 HitResult。
- 更新 PlayerState/Resource 所有权注释，并在 E5 明确不同骨骼 Hitzone 必须通过实际动画与 Debug Draw 人工核对重叠。

独立审查仍无法以纯静态方式证明最终 Content 中的 Socket Sweep、Montage Notify、木桩骨骼姿态和 GameplayCue 表现；这些分别保留在 E4～E7 的编辑器/PIE 验收中。

## 4. 明确未完成与下一步

- Content 中的旧 DT/Combo/GA/Montage/零引用 WeaponDefinition 和四个旧组合 InputAction 尚未删除；必须由 E3 使用 Reference Viewer 后按引用顺序删除，不可 Force Delete。
- 最终 `DA_IG_InputProfile`、`DA_IG_Combo`、`DA_IG_Combat`、`DA_WeaponRuntime_IG`、`DA_IG_HuoLongGun` 尚未创建；E3 只建最终数据壳，不创建最终动作 GA/Montage。
- GameplayCue Router 已有代码，但 Cue/伤害数字 Content 资产待 E6。
- `URes_InsectGlaive` 内旧精华/三灯/猎虫硬编码仍属于 M3 重写范围，当前不能作为最终虫棍资源实现验收。
- 木桩三部位的实际骨骼、半径、相对位置和确定性攻击触发方式待 E5 配置。
- 自定义 EffectContext 已实现 NetSerialize，但本阶段自动化覆盖的是分配与深复制；真实网络 PackageMap 往返不在单机 Demo 验收范围内。
