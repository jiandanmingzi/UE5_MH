// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "InsectGlaive/Kinsect/IGCoAttackTypes.h"
#include "InsectGlaive/IGActionConfigTypes.h"
#include "InsectGlaiveCombatConfig.generated.h"

class UGameplayEffect;
class UInsectGlaiveKinsectData;
class USoundBase;
class AIGMarkProjectile;
class AIGPowderActor;
class UParticleSystem;
class UAnimMontage;

/** 只有明确列出的招式读取舞踏增伤；None 不提供任何加成。 */
UENUM(BlueprintType)
enum class EIGDanceDamageAction : uint8
{
	None,
	KinsectSlash UMETA(DisplayName = "操虫斩"),
	DescendingThrust UMETA(DisplayName = "急袭突刺"),
	KinsectPiercer UMETA(DisplayName = "强化操虫穿刺")
};

/** 红灯（Red Extract）动作模式 */
UENUM(BlueprintType)
enum class EIGRedExtractMode : uint8
{
	/** 默认：无红灯使用弱化动作组，有红灯使用完整动作组 */
	ClassicMovesetGate UMETA(DisplayName = "Classic Moveset Gate"),
	/** 红灯不改变动作，只保留可配置数值 Buff */
	NumericOnly UMETA(DisplayName = "Numeric Only")
};

/**
 * UInsectGlaiveCombatConfig —— 虫棍共享资源/猎虫/粉尘/空中规则。
 * 单招专属参数由对应GA持有；旧字段已删除，虫印弹仍在本资产。
 * 由 DA_WeaponRuntime_IG 经 UWeaponRuntimeDefinition 引用；
 * 方向/组合键阈值不属于本资产，由通用 UWeaponInputProfile 提供。
 */
UCLASS(BlueprintType)
class MHGZ_API UInsectGlaiveCombatConfig : public UWeaponCombatConfigBase
{
	GENERATED_BODY()

public:
	/** 地面协同轨迹：只被显式启用协同的GA读取，旧资产默认关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|协同")
	FIGCoAttackTuning CoAttackTuning;

	/** 红灯动作模式；RuntimeHost 的 ActiveRedExtractMode 初始化自该默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华")
	EIGRedExtractMode RedExtractMode = EIGRedExtractMode::ClassicMovesetGate;

	/** 精华 GE 类：负责固定 GrantedTags/Modifier 形态；持续时间与倍率由本资产注入 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|效果类")
	TSubclassOf<UGameplayEffect> WhiteEffectClass;

	/** 红灯精华的GE类；负责标签和攻击增益，时长与倍率由共享精华参数注入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|效果类")
	TSubclassOf<UGameplayEffect> RedEffectClass;

	/** 黄灯（代码Orange）精华的GE类；负责标签和防御增益。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|效果类")
	TSubclassOf<UGameplayEffect> OrangeEffectClass;

	/** 三灯合一的GE类；成功激活后替换三种单灯，不叠加单灯倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|效果类")
	TSubclassOf<UGameplayEffect> TripleUpEffectClass;

	/** 资源状态音效与精华 GE 一样由唯一 CombatConfig 提供，运行时 Resource 不保存第二份默认值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|音效")
	TObjectPtr<USoundBase> ExtractCollectedSound;

	/** 成功获得三灯时播放的资源提示音；不会按每个单灯重复播放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|音效")
	TObjectPtr<USoundBase> TripleUpActivatedSound;

	/** 三灯自然结束时播放的资源提示音。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|音效")
	TObjectPtr<USoundBase> TripleUpExpiredSound;

	/** 猎虫耐力耗尽并触发系统召回时的提示音；持续零耐不会每帧播放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|音效")
	TObjectPtr<USoundBase> KinsectDepletedSound;

	/** 离手移动的短音；只有飞行请求提交成功才播放，不绑定猎人蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|音效")
	TObjectPtr<USoundBase> KinsectLaunchSound;

	/** 自由飞行/悬停/返航期间的循环；回手、武器附着、失效或销毁时停止。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|音效")
	TObjectPtr<USoundBase> KinsectFlightLoopSound;

	/** 单独白灯的持续时间（游戏秒）；与三灯统一时长分开。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|时长", meta = (ClampMin = "0.01"))
	float WhiteExtractDuration = 90.0f;

	/** 单独红灯的持续时间（游戏秒），重复交付按现有精华刷新规则处理。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|时长", meta = (ClampMin = "0.01"))
	float RedExtractDuration = 60.0f;

	/** 单独黄灯的持续时间（游戏秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|时长", meta = (ClampMin = "0.01"))
	float OrangeExtractDuration = 120.0f;

	/** 三灯合一的持续时间（游戏秒）；三灯期间普通交付不延长此时间。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|时长", meta = (ClampMin = "0.01"))
	float TripleUpDuration = 90.0f;

	/** 白灯对猎人移动速度的倍率；不改变猎虫飞速。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float WhiteMoveSpeedMultiplier = 1.15f;

	/** 红灯对猎人攻击力的倍率；不是猎虫攻击力或动作值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float RedAttackMultiplier = 1.20f;

	/** 有红灯（非三灯）时的猎虫耗耐倍率，相对于无灯；不改变回耐或出招门槛。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RedKinsectStaminaDrainMultiplier = 0.75f;

	/** 三灯的猎虫耗耐倍率；覆盖红灯倍率，不与其叠乘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TripleKinsectStaminaDrainMultiplier = 0.5f;

	/** 黄灯对猎人防御力的倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float OrangeDefenseMultiplier = 1.10f;

	/** 三灯对猎人攻击力的倍率；覆盖单红灯效果，不重复叠乘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float TripleAttackMultiplier = 1.25f;

	/** 三灯对猎人移动速度的倍率；覆盖单白灯效果。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float TripleMoveSpeedMultiplier = 1.15f;

	/** 三灯对猎人防御力的倍率；覆盖单黄灯效果。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|精华|倍率", meta = (ClampMin = "0.0"))
	float TripleDefenseMultiplier = 1.15f;

	/** 普通操虫与自动寻印接触攻击的猎虫动作值，公式中仍除以100。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|操虫", meta = (ClampMin = "0.0"))
	float SendKinsectMotionValue = 20.0f;

	/** 觉虫击贯通对同一 Hitzone 重复命中的最小间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|贯通", meta = (ClampMin = "0.01"))
	float AwakenedPierceHitInterval = 0.25f;

	/** Demo 唯一猎虫品种；飞行速度、距离、耐力和攻击力归该品种资产所有。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|品种与附着")
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	/** 猎虫附着到角色手臂的独立 Socket；不得与虫棍本体的 Weapon_L 共用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|品种与附着")
	FName KinsectAttachSocket = TEXT("Kinsect_Arm_Socket");

	/** ToPoint 终点与回手附着共用的到达半径（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|移动与虫印", meta = (ClampMin = "0.01"))
	float KinsectArrivalRadius = 50.0f;

	/** 舞踏层数上限；到达上限后仍可更新最后舞踏来源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏", meta = (ClampMin = "0"))
	int32 MaxDanceStacks = 3;

	/** 舞踏弹跳的 BallisticVault 参数组；两种参数来源必须且只能启用一组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏|位移")
	EBallisticParameterMode DanceVaultBallisticMode = EBallisticParameterMode::ApexHeightAndDuration;

	/** MHR 实录（动作 id 154，8 个完整段）: 升高恒为 564 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏|位移", meta = (ClampMin = "0.0"))
	float DanceVaultApexHeight = 564.0f;

	/** 与动画有效时长一致（1.6167 s），实录完整段为 1.617–1.619 s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏|位移", meta = (ClampMin = "0.0"))
	float DanceVaultDuration = 1.6167f;

	/**
	 * 舞踏接近原地但不是原地：实录完整段水平位移 0.30–1.21 m。缺这个分量时
	 * 落点会比 Rise 更贴脚下。ApexHeightAndDuration 模式不会自行推导它。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏|位移", meta = (ClampMin = "0.0"))
	float DanceVaultDistance = 80.0f;

	/** 舞踏使用初速度参数模式时的发射速度（cm/s）；另一模式不读取此值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源|舞踏|位移")
	FVector DanceVaultLaunchVelocity = FVector::ZeroVector;

	/**
	 * MHR world-transform captures of the back-vault free-fall portion.  These
	 * scales are applied only while RuntimeHost owns Combat.State.Aerial.Falling:
	 * normal = 2376 cm/s^2 / UE default 980; white = 2523 / 980.  The profile
	 * is restored at landing, cancellation, weapon swap, or runtime shutdown.
	 */
	/** 普通托管下落的重力倍率；落地、取消或卸装时恢复原CMC物理。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|物理",
		meta = (ClampMin = "0.01"))
	float AerialFallGravityScale = 2.4246f;

	/** 白灯/强化托管下落的重力倍率；不改变曲线阶段已有位移。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|物理",
		meta = (ClampMin = "0.01"))
	float WhiteAerialFallGravityScale = 2.5740f;

	/** Preserve the recorded planar tangent during free fall; no CMC drag. */
	/** 托管自由下落的水平制动减速度（cm/s²）；默认0以保留录制惯性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|物理",
		meta = (ClampMin = "0.0"))
	float AerialFallBrakingDeceleration = 0.0f;

	/**
	 * 一次系统托管下落的最长时长；超过就由看门狗收尾并告警。
	 *
	 * 坠落此前**完全没有生命周期管理**：视觉蒙太奇没有结束委托、没有循环计数、
	 * 没有超时、没有出口。实测两种后果 —— 一次录制里坠落片在 0.85 s 处按模回绕
	 * （`Position 0.8499 → 0.0249`，权重恒 1.0，姿势硬切），而那次录制结束时角色
	 * 已降到 **Z = −1939 cm 仍在下降**、tag 仍挂 `Aerial.Falling.IG_BackVault`；
	 * 另一次角色在木桩圆顶上以 `MovementMode 3` 悬停 **3.2 秒**无任何接管。
	 *
	 * 取 4.0 s 而不是贴着实测值：合法的长坠落实测有 1.25 s，阈值必须留足余量，
	 * 看门狗是兜底而不是常规出口。它只做「恢复物理 + 释放 tag + 强制回到默认移动
	 * 模式 + 告警」，不做任何位移修正。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|生命周期", meta = (ClampMin = "0.1"))
	float AerialFallMaxSeconds = 4.0f;

	/** CMC-owned free-fall visual after an ordinary rear vault.  It must be in-place. */
	/** 普通托管下落的原地蒙太奇，位移仍由CMC负责。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|表现")
	TObjectPtr<UAnimMontage> AerialFallMontage;

	/** CMC-owned free-fall visual after a White Extract launch or dance vault. */
	/** 强化下落及空回后下落的原地表现蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|表现")
	TObjectPtr<UAnimMontage> WhiteAerialFallMontage;

	/** CMC landing visual for any free insect-glaive aerial state. */
	/** 虫棍各空中动作共用的落地蒙太奇，不在此裁断动作。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|表现")
	TObjectPtr<UAnimMontage> AerialLandingMontage;

	/**
	 * 落地水平重设（cm/s）。真值 `148` 首帧水平恒 **337±6**、与入速无关（入速 306
	 * 也变 337、898 也变 337，r=+0.09）—— 触地是唯一速度**重置**点，不是摩擦衰减
	 * （下落物理.md §六 / 操虫斩曲线.md §六）。方向保持；近零速取朝向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "空中共享|表现", meta = (ClampMin = "0.0"))
	float AerialLandingHorizontalSpeed = 337.0f;

	virtual UAnimMontage* GetAerialFallingMontage(bool bEnhancedVariant) const override
	{
		return bEnhancedVariant ? WhiteAerialFallMontage : AerialFallMontage;
	}

	virtual UAnimMontage* GetAerialLandingMontage() const override
	{
		return AerialLandingMontage;
	}

	virtual UAnimMontage* GetAerialDodgeFallMontage() const override
	{
		// 空回后坠 = id `157` 的 clip = `AS_Unsh_Fall_W_Jump`（WhiteAerialFallMontage）。
		// 真值语义（撑杆跳曲线.md §3.3）：157 是「回避后（任何灯态）/ 白灯弧后」的下坠，
		// 与灯态无关 —— 所以这里固定取 157，不随 bEnhancedVariant 摇摆。
		return WhiteAerialFallMontage;
	}

	virtual float GetAerialLandingHorizontalSpeed() const override
	{
		return AerialLandingHorizontalSpeed;
	}

	virtual bool ResolveAerialFallingPhysics(bool bEnhancedVariant,
		float& OutGravityScale, float& OutBrakingDecelerationFalling) const override
	{
		OutGravityScale = bEnhancedVariant
			? WhiteAerialFallGravityScale : AerialFallGravityScale;
		OutBrakingDecelerationFalling = AerialFallBrakingDeceleration;
		return FMath::IsFinite(OutGravityScale) && OutGravityScale > 0.0f
			&& FMath::IsFinite(OutBrakingDecelerationFalling)
			&& OutBrakingDecelerationFalling >= 0.0f;
	}

	virtual float GetAerialFallMaxSeconds() const override
	{
		return AerialFallMaxSeconds;
	}

	/** 186/187 临时挂接的武器 Socket；该 Socket Transform 是附着偏移的唯一来源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|操虫斩|附着")
	FName KinsectSlashAttachSocket = TEXT("IG_Kinsect");

	/** 虫印弹最大射程（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|移动与虫印", meta = (ClampMin = "0.01"))
	float KinsectMarkMaxDistance = 6000.0f;

	/** 虫印持续时间（秒）；M6.3合同固定为90秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|移动与虫印", meta = (ClampMin = "0.01"))
	float KinsectMarkDuration = 90.0f;

	/** 自动寻印起点到虫印的最大距离（cm）；0表示未调校/关闭自动寻印，不能借用粉尘聚爆半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|虫印|自动攻击", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float KinsectAutoMarkSearchRadius = 3000.0f;

	/** 到达虫印后的自动攻击间隔（世界游戏秒）；0表示未调校/关闭自动攻击。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|虫印|自动攻击", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float KinsectAutoMarkAttackInterval = 2.0f;

	/** 普通手动/招式猎虫命中虫印的产粉间隔；自动寻印攻击和觉虫击黄灯逐击产粉不检查或占用此冷却。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float MarkedHitPowderCooldown = 10.0f;

	/** 虫印弹飞行速度（cm/s），不影响自动寻印的猎虫飞速。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|虫印弹", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileSpeed = 3000.0f;

	/** 虫印弹扫掠判定半径（cm），不是虫印Debuff的部位覆盖范围。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|虫印弹", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileRadius = 30.0f;

	/** 虫印弹未命中时的最长生存时间（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|虫印弹", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileLifetime = 5.0f;

	/** M3 原生虫印弹行为类；E5 可填只负责表现配置的蓝图子类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|虫印弹")
	TSubclassOf<AIGMarkProjectile> KinsectMarkProjectileClass;

	/** 虫印弹从带 WeaponTrace ComponentTag 的武器 Mesh 上该 Socket 发射。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "招式参数|虫印弹")
	FName KinsectMarkLaunchSocket = TEXT("IG_FrontTip");

	/** 虫印成功附着后的持续视觉；应是一团附着在 Hitzone 上的黄色粉末。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "猎虫共享|虫印|特效")
	TObjectPtr<UParticleSystem> KinsectMarkEstablishedEffect;

	/** Non-colliding persistent powder actor class. Assign BP_IG_PowderCloud to enable the powder runtime. */
	/** 常驻的无碰撞粉尘Actor类；未配置时不生成粉尘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|表现")
	TSubclassOf<AIGPowderActor> PowderActorClass;

	/** Powder lifetime in world game seconds. The current design fixes this at 180 seconds. */
	/** 每团粉尘的世界游戏秒寿命；到期自然引爆，聚爆预留时按现有规则刷新。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float PowderLifetime = 180.0f;

	/** Maximum live powder actors owned by one weapon Runtime; Available and Reserved both count. */
	/** 本玩家运行期可持有的粉尘总数上限；已预留与未预留均计数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "1"))
	int32 PowderMaxCount = 100;

	/** Natural expiry explosion radius (default 150 cm); damage only, not visible particle size. */
	/** 普通粉尘自然爆炸的伤害球半径（cm），不是聚爆半径或粒子视觉尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float PowderNaturalExplosionRadius = 150.0f;

	/** Powder action value as a raw percent; 100 means one full Kinsect attack-power unit. */
	/** 每团粉尘的共享动作值；伤害=生成时猎虫攻击力×动作值/100，不吃肉质。修改不追溯已有粉尘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "0.0"))
	float PowderMotionValue = 100.0f;

	/** Distance beyond which this Runtime's powder is removed without damage; E6.2 default is 100m (10000cm). */
	/** 玩家与粉尘相距超过此值（cm）时移除粉尘，不引爆造成伤害；默认100m。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|规则", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float PowderDespawnDistance = 10000.0f;

	/** 普通粉尘真正引爆时在爆点播放；聚爆复用一次此基础层，不按粉尘数/目标/伤害段重复。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "粉尘共享|音效")
	TObjectPtr<USoundBase> PowderExplosionSound;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
