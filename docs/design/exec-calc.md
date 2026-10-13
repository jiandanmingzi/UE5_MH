# 伤害执行计算（ExecCalc）

> **当前合同：2026-10-13。** 已把普通武器的物理与属性伤害接入同一个 `UMHGZDamageExecCalc`。猎虫、粉尘、聚爆与木桩入站攻击保留已验收的专用简化算法，不因为共用 GE 而继承玩家武器参数。本文取代早期 M2“属性/斩味未接入、Cue 尚未存在”的旧说明；不自动改变 M6/E6 门禁。

## 1. 边界与数据单位

- 实现普通武器两条伤害通道、独立肉质/斩味/修正、正负会心，以及分别取整后合并。
- `AttackPower` 是装备/技能 GE 修正后的实际攻击力，不是《世界》的武器显示倍率数值。
- 新增 Source ASC `ElementPower`，是实际属性值；《世界》面板属性300对应这里30，不能传入300。
- 旧 `MotionValue` 保持归一化倍率：`0.20`对应原作动作值20，不再除100。
- 肉质也归一化：`0.45`对应肉质45，`0.20`对应肉质20，不再除100。
- 不自动重新平衡所有招式/武器，不凭空给现有武器属性。`ElementPower=0`、`ElementType=None`、斩味倍率1保持无属性 Demo。
- 本轮未实现斩味条消耗/磨刀、完整装备技能与属性上限、异常积蓄、软化、任务倍率、眩晕、耐力伤害和部位破坏。这些未来设计仍保留，不能称为完整原作数值复刻。

数值基准为 MoonBunnie / Deathcream 的《世界／冰原》普通近战研究；《崛起》招式各段动作值仍由现有 GA 资产提供，不混入《世界》的面板显示倍率。

- [原始公式](https://docs.google.com/spreadsheets/d/e/2PACX-1vSuFIUfe5Sp9k6sqKvPSPbO2xcClt1WaYMf_xGocWDIkgaSDi0nBLbOKAd8GScLQRjfxbljhPO4Bjf7/pubhtml/sheet?gid=703654261)：Raw / Element / Total Damage 与分别取整。
- [原始系数表](https://docs.google.com/spreadsheets/d/e/2PACX-1vQ5HFkHnEP74gD-SCVta9syb9GaF1_nSmMFgV4hxvZt9iu4HmfhGlP2KbnIbC-cAx5kkvsd8L7oB0Uy/pubhtml/sheet?gid=634609634)：斩味、正负会心与会心属性倍率。2026-10-13 已直接读取核验。

## 2. 同一命中的唯一结算链

```text
攻击判定得到真实 HitResult
  -> AttackAbility.MakeDamageSpec
     Context 快照武器参数/伤害类型，Spec 快照 Source ASC 属性
  -> ASC.ApplyGameplayEffectSpecToTarget（原生 UMHGZDamageGameplayEffect）
  -> UMHGZDamageExecCalc
     Weapon：物理/属性 -> 分别取整 -> 合并一次 IncomingDamage
     非 Weapon：保留原算法
     依次输出 Damage -> Stagger -> CriticalFlag -> HitSignal（最后）
  -> AttributeSet 在 HitSignal 到达时原子读出/清零 Meta
     普通目标扣血；无限血木桩保留血量但报告本次实际伤害
  -> HitFeedbackRouter 显式执行一次反馈/伤害数字/卡肉/震屏
```

不增加第二次 GE、第二次扣血或两条伤害数字。表现层只消费最终总伤害，不重新计算公式。现有常驻伤害数字资产为 `/Game/UI/Feedback/WBP_DamageNumber`、`/Game/Blueprints/Feedback/BP_DamageNumberActor`、`/Game/GameplayCues/GCN_Hit_DamageNumber`。

## 3. 公式与会心

```text
物理浮点伤害 = AttackPower × MotionValue × PhysicalSharpnessMultiplier
             × PhysicalHitzone × PhysicalDamageMultiplier × PhysicalCrit × DanceMultiplier

有效属性值 = RoundChannel(ElementPower × ElementCrit)
属性浮点伤害 = 有效属性值 × ElementMotionValue × ElementSharpnessMultiplier
             × ElementHitzone × ElementDamageMultiplier

最终武器伤害 = RoundChannel(物理浮点伤害) + RoundChannel(属性浮点伤害)
硬直值 = BaseStagger × StaggerMultiplier × Hitzone.StaggerRate
```

`RoundChannel`：真正为零、负值或非有限值返回0；正值四舍五入，正的不足1的值保留1。零肉质/免疫不会因为“最低1”产生伤害。物理1.5、属性1.5得到2+2=4，而不是合计后取整得到3。

`MotionValue<=0`仍表示整个段为合法的**无伤害接触**，包括属性通道，保持纯位移/反击/无伤害 Notify 合同。需要属性伤害时配置正物理动作值与属性参数，不用零动作值制造伤害。

会心率按单次命中随机判定，不使用平均会心期望倍率代替随机结果：

- 正会心：物理默认1.25，可配置超会心倍率；属性默认1，只有显式配置 `CriticalElementMultiplier>1`才增幅。
- 负会心：仅武器物理乘0.75；属性不减伤，不显示为正会心数字。
- 已有 `Damage.CritOverride>=0`优先；负值沿用 Source ASC `CriticalRate`。需要负会心装备/测试时设置属性的负会心率。
- 舞踏倍率继续快照，只放大武器物理，不额外放大属性。
- `bUseHitzoneDefense=false`只绕过物理肉质；属性仍受自身肉质影响。

## 4. 蓝图/资产配置入口

| 数据 | 位置 | 默认与含义 |
|---|---|---|
| 实际攻击力/属性值/会心率 | Source ASC 的 `AttackPower` / `ElementPower` / `CriticalRate`；装备/GE设置 | 武器定义新增`Weapon > Damage > ElementPower`，装备重算会写入ASC，基础属性只取武器槽；属性默认0 |
| 属性类型与武器倍率 | `DA_IG_Combat`（基类 `UWeaponCombatConfigBase`）的 `Combat > Damage > DamageProfile` | None；物理/属性斩味1；物理会心1.25；会心属性1 |
| 物理类型 | GA 的 `AttackSegments[i].Damage.PhysicalDamageType` | Sever / Blunt / Shot，默认Sever |
| 物理动作值 | 同段 `MotionValue` | 0无伤害；0.20=动作值20 |
| 属性动作倍率 | 同段 `ElementMotionValue` | 默认1；0禁用属性；不乘物理动作值 |
| 额外修正 | 同段 `PhysicalDamageMultiplier` / `ElementDamageMultiplier` | 各默认1；不重复填写已由属性GE处理的加成 |
| 物理肉质 | `DA_TrainingDummy.Hitzones[i].PhysicalHitzoneMultipliers`，或通用 Hitzone 同名Map | 缺项沿用 `DefenseMultiplier`；显式0免疫 |
| 属性肉质 | 同部位 `ElementHitzoneMultipliers` | Fire/Water/Thunder/Ice/Dragon；缺项0，不从物理肉质推导属性弱点 |

`MonsterBase.GenerateHitzonesFromConfig`把两张肉质表复制到运行时组件，不是仅测试直写组件生效。Sphere与Box查询经 `Resolve(Hit.Component)`读取同一部位元数据；不更改碰撞、外表面或虫印坐标。

斩味参考（手动配置倍率，不代表已实现斩味消耗）：

| 斩味 | 物理 | 属性 |
|---|---:|---:|
| 红 | 0.50 | 0.25 |
| 橙 | 0.75 | 0.50 |
| 黄 | 1.00 | 0.75 |
| 绿 | 1.05 | 1.00 |
| 蓝 | 1.20 | 1.0625 |
| 白 | 1.32 | 1.15 |
| 紫 | 1.39 | 1.25 |

无斩味Demo默认 `1/1`是中性兼容值，不称为黄斩。远程等无斩味招式用1，不套近战斩味表。

示例：AttackPower=200、MotionValue=0.20、白斩、物理肉质0.45；ElementPower=20、属性动作倍率2、属性肉质0.20、无会心：物理23.76→24，属性9.2→9，总计33。把物理动作值改0.40时总计57，属性仍为9。

## 5. Spec / Context 快照

`FMHGZGameplayEffectContext`保存本次 `PhysicalDamageType`及 `WeaponDamageProfile`。`MakeDamageSpec`从 RuntimeHost 的 CombatConfig 拷贝；旧Spec不受后来装备/配置变更影响。新增字段参与深拷贝与NetSerialize；本轮仍只验收单机，不声称完成网络预测。

Source ASC 的 AttackPower / ElementPower / CriticalRate / StaggerMultiplier 均为 `bSnapshot=true`捕获。

| SetByCaller Tag | 默认 | 用途 |
|---|---:|---|
| Damage.MotionValue | 0 | 归一化物理动作值及全段无伤害开关 |
| Damage.ElementMotionValue | 1 | 属性动作倍率；仅Weapon消费 |
| Damage.PhysicalMultiplier | 1 | 段内额外物理修正；仅Weapon消费 |
| Damage.ElementMultiplier | 1 | 段内额外属性修正；仅Weapon消费 |
| Damage.AttackPower | -1 | 可选实际攻击力覆写，猎虫/固定伤害使用 |
| Damage.CritOverride | -1 | 可选会心率覆写，-1沿用属性 |
| Damage.DanceMultiplier | 1 | 已有舞踏物理倍率 |
| Damage.BaseStagger | 0 | 基础破坏值 |

## 6. 来源隔离与未来扩展

- `Weapon`：上述双通道公式；物理类型由段配置，不从表现Cue猜测。
- `Kinsect`：保留独立AttackPower覆写、原DefenseMultiplier和Floor取整；不继承玩家属性/斩味。未来切断猎虫继承属性需另定专用合同。
- `Powder`：保留Resource快照的固定伤害、既有免肉质/零会心/舞踏1参数；聚爆仍为既有两段独立事务。
- `DummyAttack`：保留入站测试算法，不套玩家武器参数。

通用GE/Meta/Router所有权不变。未来在Context/ExecCalc或独立数值帮助函数扩展，不在每个招式GA复制公式。

## 7. 验证与交接

新增 `MHGZ.M2.Damage.Formula.*`自动化覆盖实际GE/扣血：双通道、独立动作值、属性免疫、正负会心、会心属性、舞踏隔离、分别取整、无伤害段、来源兼容和生产MakeDamageSpec快照。现有Context深拷贝测试覆盖新增字段。

2026-10-13最终验证（已包含武器定义→装备重算→ASC属性值接入）：

- 构建成功：`Saved/Logs/DualDamageFormula20261013-equipment-build.log`，退出码0、`Result: Succeeded`。
- 相关回归185/185成功（59普通成功、126成功伴随测试场景警告；0失败、0未运行）：`Saved/M6Audit/DualDamageFormula20261013EquipmentVerified/index.json`。范围M2/M3、M4翻滚/预输入、M5及M6。新增6项公式/集成测试全部成功。
- Content验证636资产，0错误、0警告：`Saved/Logs/DualDamageFormula20261013-equipment-validation.log`。
- 已通过的关键实际链路：装备20属性值写入ASC并可清零；生产Spec冻结武器参数/实际属性；Config生成Box部位后读取正确肉质；物理+属性只产生一次总伤害数字。
- 首轮失败测试报告保留在`DualDamageFormula20261013Targeted`，其会心用例直接Init属性绕过ASC聚合器缓存，已改用SetNumericAttributeBase并在最终报告重验成功。不将UE进程退出码0当作测试通过证据。

本次不保存或修改现有CombatConfig、GA、木桩Content数值资产。启用属性武器时需配置武器定义的ElementPower、CombatConfig的属性类型及部位属性肉质；代码接入不等于给当前武器设定属性平衡。可用上述33点样例做PIE检查，不要求重新录制全部动作验收。
