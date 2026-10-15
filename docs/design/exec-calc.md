# 伤害执行计算（ExecCalc）

> **当前合同：2026-10-13（百分数单位修订）。** 普通武器、猎虫、虫粉共用 `UMHGZDamageExecCalc`，但使用各自明确的公式。物理动作值与物理肉质都按原始 `0–100` 数值保存并在公式中各除以100；属性动作倍率/属性肉质仍保持独立倍率单位。本文取代此前“MotionValue/肉质归一化”的公式单位描述；不自动改变 M6/E6 门禁。

## 1. 边界与数据单位

- 实现普通武器两条伤害通道、独立肉质/斩味/修正、正负会心，以及分别取整后合并。
- `AttackPower` 是装备/技能 GE 修正后的实际攻击力，不是《世界》的武器显示倍率数值。
- 新增 Source ASC `ElementPower`，是实际属性值；《世界》面板属性300对应这里30，不能传入300。
- `MotionValue` 保存原始动作值百分数：`10`就是动作值10，公式使用 `MotionValue/100`。
- 物理肉质保存 `0–100`：`45`就是肉质45，公式使用 `PhysicalHitzone/100`；`DefenseMultiplier` 是物理肉质类型表缺项时的同单位兼容值。
- 属性肉质保持既有归一化倍率：`0.20`表示属性肉质20；属性动作倍率默认1。此单位不随物理肉质迁移。
- 所有虫棍 GA 中有伤害 GE 的攻击段动作值为10；零动作值仍只用于合法无伤害命中。普通送虫/拔刀送虫的猎虫动作值从归一化`.20`迁移为`20`，觉虫击贯通猎虫动作值从`.30`迁移为`30`，以保持原伤害比例。
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
     Weapon：AttackPower × MotionValue/100 × PhysicalHitzone/100，再独立结算属性
     Kinsect：KinsectAttackPower × MotionValue/100 × 对应物理肉质/100
     Powder：生成时快照 KinsectAttackPower × PowderMotionValue/100，不乘肉质
     DummyAttack：保持单独的入站伤害兼容合同
     依次输出 Damage -> Stagger -> CriticalFlag -> HitSignal（最后）
  -> AttributeSet 在 HitSignal 到达时原子读出/清零 Meta
     普通目标扣血；无限血木桩保留血量但报告本次实际伤害
  -> HitFeedbackRouter 显式执行一次反馈/伤害数字/卡肉/震屏
```

不增加第二次 GE、第二次扣血或两条伤害数字。表现层只消费最终总伤害，不重新计算公式。现有常驻伤害数字资产为 `/Game/UI/Feedback/WBP_DamageNumber`、`/Game/Blueprints/Feedback/BP_DamageNumberActor`、`/Game/GameplayCues/GCN_Hit_DamageNumber`。

## 3. 公式与会心

```text
物理浮点伤害 = AttackPower × (MotionValue / 100) × (PhysicalHitzone / 100)
             × PhysicalSharpnessMultiplier × PhysicalDamageMultiplier
             × PhysicalCrit × DanceMultiplier

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
- `bUseHitzoneDefense=false`只绕过物理肉质，等效肉质100；属性仍受自身肉质影响。

## 4. 蓝图/资产配置入口

| 数据 | 位置 | 默认与含义 |
|---|---|---|
| 实际攻击力/属性值/会心率 | Source ASC 的 `AttackPower` / `ElementPower` / `CriticalRate`；装备/GE设置 | 武器定义新增`Weapon > Damage > ElementPower`，装备重算会写入ASC，基础属性只取武器槽；属性默认0 |
| 属性类型与武器倍率 | `DA_IG_Combat`（基类 `UWeaponCombatConfigBase`）的 `Combat > Damage > DamageProfile` | None；物理/属性斩味1；物理会心1.25；会心属性1 |
| 物理类型 | GA 的 `AttackSegments[i].Damage.PhysicalDamageType` | Sever / Blunt / Shot，默认Sever |
| 物理动作值 | 同段 `MotionValue` | 原始百分数；0无伤害，10=动作值10 |
| 属性动作倍率 | 同段 `ElementMotionValue` | 默认1；0禁用属性；不乘物理动作值 |
| 额外修正 | 同段 `PhysicalDamageMultiplier` / `ElementDamageMultiplier` | 各默认1；不重复填写已由属性GE处理的加成 |
| 物理肉质 | `DA_TrainingDummy.Hitzones[i].PhysicalHitzoneMultipliers`，或通用 Hitzone 同名Map | 0–100原始百分数；缺项沿用 `DefenseMultiplier`；显式0免疫 |
| 属性肉质 | 同部位 `ElementHitzoneMultipliers` | 仍为归一化倍率（0.20=20）；缺项0，不从物理肉质推导属性弱点 |
| 猎虫动作值 | `DA_IG_Combat.SendKinsectMotionValue` / `DrawSendKinsectMotionValue` / `AwakenedKinsectMotionValue` | 原始百分数；20/20/30分别表示MV20/MV20/MV30 |
| 虫粉动作值 | `DA_IG_Combat.PowderMotionValue` | 原始百分数，默认100；生成时快照 `KinsectAttackPower × PowderMotionValue/100` |

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

示例：AttackPower=200、MotionValue=10、白斩、物理肉质45；ElementPower=20、属性动作倍率2、属性肉质0.20、无会心：物理11.88→12，属性9.2→9，总计21。把物理动作值改20时总计33，属性仍为9。

## 5. Spec / Context 快照

`FMHGZGameplayEffectContext`保存本次 `PhysicalDamageType`及 `WeaponDamageProfile`。`MakeDamageSpec`从 RuntimeHost 的 CombatConfig 拷贝；旧Spec不受后来装备/配置变更影响。新增字段参与深拷贝与NetSerialize；本轮仍只验收单机，不声称完成网络预测。

Source ASC 的 AttackPower / ElementPower / CriticalRate / StaggerMultiplier 均为 `bSnapshot=true`捕获。

| SetByCaller Tag | 默认 | 用途 |
|---|---:|---|
| Damage.MotionValue | 0 | 原始物理动作值百分数；武器/猎虫/粉尘按来源公式消费 |
| Damage.ElementMotionValue | 1 | 属性动作倍率；仅Weapon消费 |
| Damage.PhysicalMultiplier | 1 | 段内额外物理修正；仅Weapon消费 |
| Damage.ElementMultiplier | 1 | 段内额外属性修正；仅Weapon消费 |
| Damage.AttackPower | -1 | 可选实际攻击力覆写，猎虫/固定伤害使用 |
| Damage.CritOverride | -1 | 可选会心率覆写，-1沿用属性 |
| Damage.DanceMultiplier | 1 | 已有舞踏物理倍率 |
| Damage.BaseStagger | 0 | 基础破坏值 |

## 6. 来源隔离与未来扩展

- `Weapon`：上述双通道公式；物理类型由段配置，不从表现Cue猜测。
- `Kinsect`：`KinsectAttackPower × MotionValue/100 × PhysicalHitzone/100`；按`PhysicalDamageType`查物理肉质表（当前默认斩），Floor取整；不继承猎人会心/斩味/属性/舞踏。
- `Powder`：每团生成时快照`KinsectAttackPower × PowderMotionValue/100`；爆炸时不再乘物理肉质、猎人攻击力、会心或舞踏。聚爆阶段2汇总被消耗粉尘各自快照的伤害；阶段1的1/5/10数量档位奖励仍是独立固定配置。
- `DummyAttack`：保留入站测试算法，不套玩家武器参数。

通用GE/Meta/Router所有权不变。未来在Context/ExecCalc或独立数值帮助函数扩展，不在每个招式GA复制公式。

## 7. 验证与交接

新增 `MHGZ.M2.Damage.Formula.*`自动化覆盖实际GE/扣血：双通道、独立动作值、属性免疫、正负会心、会心属性、舞踏隔离、分别取整、无伤害段、来源兼容和生产MakeDamageSpec快照。现有Context深拷贝测试覆盖新增字段。

2026-10-13单位迁移前基线（已包含武器定义→装备重算→ASC属性值接入；不代表本次百分数单位迁移的验证）：

- 构建成功：`Saved/Logs/DualDamageFormula20261013-equipment-build.log`，退出码0、`Result: Succeeded`。
- 相关回归185/185成功（59普通成功、126成功伴随测试场景警告；0失败、0未运行）：`Saved/M6Audit/DualDamageFormula20261013EquipmentVerified/index.json`。范围M2/M3、M4翻滚/预输入、M5及M6。新增6项公式/集成测试全部成功。
- Content验证636资产，0错误、0警告：`Saved/Logs/DualDamageFormula20261013-equipment-validation.log`。
- 已通过的关键实际链路：装备20属性值写入ASC并可清零；生产Spec冻结武器参数/实际属性；Config生成Box部位后读取正确肉质；物理+属性只产生一次总伤害数字。
- 首轮失败测试报告保留在`DualDamageFormula20261013Targeted`，其会心用例直接Init属性绕过ASC聚合器缓存，已改用SetNumericAttributeBase并在最终报告重验成功。不将UE进程退出码0当作测试通过证据。

上述验证记录是单位迁移前的历史基线。此次实施将保存更新CombatConfig、虫棍GA与训练木桩肉质资产；不自动修改武器属性平衡。依照`docs/using/README.md`，默认不重复全量验证或 PIE。
