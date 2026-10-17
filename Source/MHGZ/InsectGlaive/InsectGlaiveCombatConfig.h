// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "InsectGlaiveCombatConfig.generated.h"

class UGameplayEffect;
class UCurveFloat;
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
 * 一个撑杆跳变体的可调标量：**方向 × 灯态**。
 *
 * 「一个变体一条」而不是「两个灯态各一片平铺字段」：四个方向的字段名本来就该一样，
 * 平铺会得到八套同构字段，而它们之间的差异**只有收成一条一条才校验得了**
 * （`ArcDuration >= VaultDuration`、非白灯的残余自由落体必须落在实测 0.18~0.30 s 内）。
 *
 * 曲线、弦向、clip 路径**不在这里** —— 那些在 GA 的 `FVaultProfile` 上。
 * 这里只放「动作数值」，因为它是这张资产的面相：改一个数就能调手感，
 * 而曲线要重跑 `Scripts/MHRise/build_vault_curves.py`。
 */
USTRUCT(BlueprintType)
struct FVaultTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault")
	EDirectionalInput Direction = EDirectionalInput::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault")
	bool bWhite = false;

	/**
	 * **移动窗口** = `Jump + JumpOver`，即 CurvedVault 源的 `Duration`，**不含下坠段**。
	 *
	 * 与 `ArcDuration` 是两个语义不同的量，混用会出事：拿弧时长当窗口等于把
	 * 本该不存在的飞行补出来，实录路径被拉伸（白灯那次实测 +4.7%）。交棒点
	 * 由 `(JumpDuration + JumpOverDuration) / ArcDuration` 算出，所以窗口一变，
	 * 交棒进度与残余自由落体一起变。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "0.01"))
	float VaultDuration = 0.0f;

	/**
	 * **整条离地前缀**的时长（含下坠段），**只用于把实录轨迹归一化**。
	 *
	 * 用错这个量会把 Jump 边界从 0.330 挪到 0.375，把本该属于 JumpOver 的约 30 cm
	 * 位移划给 Jump —— 这正是它当初被从 `VaultDuration` 里拆出来的原因。
	 *
	 * 白灯**没有独立的下坠段**（一直覆盖到落地），所以两值相等、`HandoffProgress`
	 * 恰为 1.0，那是合法的「无自由落体窗口」，不是配置错误。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "0.01"))
	float ArcDuration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "0.01"))
	float Distance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "0.01"))
	float ApexHeight = 0.0f;
};

/**
 * UInsectGlaiveCombatConfig —— 同一把虫棍动作规则与可调数值的唯一入口。
 * 由 DA_WeaponRuntime_IG 经 UWeaponRuntimeDefinition 引用；
 * 方向/组合键阈值不属于本资产，由通用 UWeaponInputProfile 提供。
 */
UCLASS(BlueprintType)
class MHGZ_API UInsectGlaiveCombatConfig : public UWeaponCombatConfigBase
{
	GENERATED_BODY()

public:
	/**
	 * 灌撑杆跳的默认值。前 / 左 / 右六条直接读生成表，不手抄；
	 * 后撑杆跳两条是本文件的手写常量 —— 见 .cpp 里的理由。
	 */
	UInsectGlaiveCombatConfig();

	/** 红灯动作模式；RuntimeHost 的 ActiveRedExtractMode 初始化自该默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Red Extract")
	EIGRedExtractMode RedExtractMode = EIGRedExtractMode::ClassicMovesetGate;

	/** 精华 GE 类：负责固定 GrantedTags/Modifier 形态；持续时间与倍率由本资产注入 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Effects")
	TSubclassOf<UGameplayEffect> WhiteEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Effects")
	TSubclassOf<UGameplayEffect> RedEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Effects")
	TSubclassOf<UGameplayEffect> OrangeEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Effects")
	TSubclassOf<UGameplayEffect> TripleUpEffectClass;

	/** 资源状态音效与精华 GE 一样由唯一 CombatConfig 提供，运行时 Resource 不保存第二份默认值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Audio")
	TObjectPtr<USoundBase> ExtractCollectedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Audio")
	TObjectPtr<USoundBase> TripleUpActivatedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Audio")
	TObjectPtr<USoundBase> TripleUpExpiredSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Audio")
	TObjectPtr<USoundBase> KinsectDepletedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Tuning", meta = (ClampMin = "0.01"))
	float WhiteExtractDuration = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Tuning", meta = (ClampMin = "0.01"))
	float RedExtractDuration = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Tuning", meta = (ClampMin = "0.01"))
	float OrangeExtractDuration = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Tuning", meta = (ClampMin = "0.01"))
	float TripleUpDuration = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float WhiteMoveSpeedMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float RedAttackMultiplier = 1.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float OrangeDefenseMultiplier = 1.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float TripleAttackMultiplier = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float TripleMoveSpeedMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Extract|Multipliers", meta = (ClampMin = "0.0"))
	float TripleDefenseMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float SendKinsectMotionValue = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float DrawSendKinsectMotionValue = 20.0f;

	/** 觉虫击未消耗红灯时，猎虫每次成功命中的动作值；白/黄灯不改变本档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float AwakenedKinsectMotionValue = 15.0f;

	/** 消耗红灯但非三灯时的猎虫单次动作值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float AwakenedRedKinsectMotionValue = 30.0f;

	/** 原子消耗完整三灯时的猎虫单次动作值；不再与红灯档叠乘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float AwakenedTripleKinsectMotionValue = 50.0f;

	/** 觉虫击贯通对同一 Hitzone 重复命中的最小间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.01"))
	float AwakenedPierceHitInterval = 0.25f;

	/** Demo 唯一猎虫品种；飞行速度、距离、耐力和攻击力归该品种资产所有。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Data")
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	/** 猎虫附着到角色手臂的独立 Socket；不得与虫棍本体的 Weapon_L 共用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Data")
	FName KinsectAttachSocket = TEXT("Kinsect_Arm_Socket");

	/** ToPoint 终点与回手附着共用的到达半径（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectArrivalRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "0"))
	int32 MaxDanceStacks = 3;

	/** 仅为旧资产加载兼容保留；运行时不再读取通用倍率。 */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the three action-specific Dance multiplier arrays."))
	TArray<float> DanceDamageMultipliers = { 1.0f };

	/** 索引为舞踏层数（0/1/2/3）；仅武器段，出招时快照。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Damage", meta = (ClampMin = "1.0"))
	TArray<float> KinsectSlashDanceDamageMultipliers = { 1.0f, 1.1f, 1.2f, 1.3f };

	/** 四段武器伤害共用 Commit 清层前的快照，包括地面段。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Damage", meta = (ClampMin = "1.0"))
	TArray<float> DescendingThrustDanceDamageMultipliers = { 1.0f, 1.1f, 1.3f, 1.5f };

	/** 整个强化操虫穿刺 GA 的全部武器伤害段，包括203触地后；出招时快照。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Damage", meta = (ClampMin = "1.0"))
	TArray<float> KinsectPiercerDanceDamageMultipliers = { 1.0f, 1.3f, 1.5f, 2.0f };

	/** 按招式显式选择倍率；未列入的招式或无效配置安全回退为 1。 */
	float GetDanceDamageMultiplier(EIGDanceDamageAction Action, int32 Stacks) const;

	/** 舞踏弹跳的 BallisticVault 参数组；两种参数来源必须且只能启用一组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement")
	EBallisticParameterMode DanceVaultBallisticMode = EBallisticParameterMode::ApexHeightAndDuration;

	/** MHR 实录（动作 id 154，8 个完整段）: 升高恒为 564 cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement", meta = (ClampMin = "0.0"))
	float DanceVaultApexHeight = 564.0f;

	/** 与动画有效时长一致（1.6167 s），实录完整段为 1.617–1.619 s。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement", meta = (ClampMin = "0.0"))
	float DanceVaultDuration = 1.6167f;

	/**
	 * 舞踏接近原地但不是原地：实录完整段水平位移 0.30–1.21 m。缺这个分量时
	 * 落点会比 Rise 更贴脚下。ApexHeightAndDuration 模式不会自行推导它。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement", meta = (ClampMin = "0.0"))
	float DanceVaultDistance = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement")
	FVector DanceVaultLaunchVelocity = FVector::ZeroVector;

	/**
	 * 撑杆跳（四个方向 × 两种灯态）的**动作数值**，一个变体一条。
	 *
	 * 后撑杆跳两条就是迁移前的那四个字段，值逐字未变；前 / 左 / 右六条由
	 * `Scripts/MHRise/build_vault_curves.py` 从 MHR 实机录制算出（构造里灌默认值）。
	 *
	 * 由 MHR 实录轨迹重建。Jump 段保留 Montage Root Motion；从 Jump_Over 的段边界起
	 * CurvedVault 接管，走完整条弧后交给 CMC。重力、碰撞与接地都不再由 GA 的路径接管。
	 *
	 * 交棒点在哪儿**不是配置**：它由 `(JumpDuration + JumpOverDuration) / ArcDuration`
	 * 在运行时算出，残余自由落体时长即 `(1 − 它) × ArcDuration`。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault|Movement")
	TArray<FVaultTuning> VaultTunings;

	/**
	 * 按 (方向, 灯态) 取一条。
	 *
	 * **没有就返回空**，调用方一律显式拒绝（拒激活）——不回退到某个方向的默认值。
	 * 回退会让「少配了一个方向」表现成「跳出去但用的是别人的数值」，没有任何报错。
	 */
	const FVaultTuning* FindVaultTuning(EDirectionalInput Direction, bool bWhite) const;

	/**
	 * MHR world-transform captures of the back-vault free-fall portion.  These
	 * scales are applied only while RuntimeHost owns Combat.State.Aerial.Falling:
	 * normal = 2376 cm/s^2 / UE default 980; white = 2523 / 980.  The profile
	 * is restored at landing, cancellation, weapon swap, or runtime shutdown.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Physics",
		meta = (ClampMin = "0.01"))
	float AerialFallGravityScale = 2.4246f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Physics",
		meta = (ClampMin = "0.01"))
	float WhiteAerialFallGravityScale = 2.5740f;

	/** Preserve the recorded planar tangent during free fall; no CMC drag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Physics",
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial", meta = (ClampMin = "0.1"))
	float AerialFallMaxSeconds = 4.0f;

	/** CMC-owned free-fall visual after an ordinary rear vault.  It must be in-place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Presentation")
	TObjectPtr<UAnimMontage> AerialFallMontage;

	// ----------------------------------------------------------------------
	// 空中回避（MHR id 137）
	//
	// 它是**纯抛体**：142 帧 / 1.185 s 里 g 恒为 −2378 cm/s²（R² 中位 0.9991），
	// 水平 900 cm/s 沿摇杆方向、垂直 +1182 cm/s 起，净位移水平 1065 cm / 垂直 −268 cm。
	// 无白灯与有白灯**逐值相同** ⇒ 没有白灯变体，`bEnhancedVariant` 恒 false。
	// ----------------------------------------------------------------------

	/**
	 * 实录反推的水平初速（cm/s）；方向取输入快照里的世界系摇杆方向（八向连续角，不量化）。
	 *
	 * 蒙太奇刻意**不在**这里：按项目分工，动作蒙太奇由 GA 蓝图指派
	 * （`UMHGZAttackAbility::AttackMontage`），配置只放运行时数值 —— 与撑杆跳同构。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|AirDodge",
		meta = (ClampMin = "0.01"))
	float AirDodgeHorizontalSpeed = 900.0f;

	/**
	 * 实录反推的垂直初速（cm/s，向上）。它与水平初速一起决定整条弧：
	 * 源时长 = 2·vz/g、顶点 = vz²/(2g)（g 取 CMC 当前重力 = AerialFallGravityScale × 980）。
	 *
	 * ⚠ 顶点**不在**时长中点（上升 0.497 s、下降 0.688 s），所以**不能**改用
	 * 「顶点 + 时长」那组参数：那条路由引擎按 4H/D 反算 v0，代入会得到 989 而不是 1182。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|AirDodge",
		meta = (ClampMin = "0.01"))
	float AirDodgeVerticalSpeed = 1182.0f;

	// ⚠ 自身最早可操作帧（0.650 s = 78 帧）刻意**不在这里**：它是**资产里那条点通知**
	// 的时间，归属 `MHGZAerialHandoffSetupCommandlet` 的路由表（那里本来就逐条写着
	// montage 名与 handoff_time，并带出处）。放两份会立刻互漂。

	/** CMC-owned free-fall visual after a White Extract launch or dance vault. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Presentation")
	TObjectPtr<UAnimMontage> WhiteAerialFallMontage;

	/** CMC landing visual for any free insect-glaive aerial state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Presentation")
	TObjectPtr<UAnimMontage> AerialLandingMontage;

	/**
	 * 落地水平重设（cm/s）。真值 `148` 首帧水平恒 **337±6**、与入速无关（入速 306
	 * 也变 337、898 也变 337，r=+0.09）—— 触地是唯一速度**重置**点，不是摩擦衰减
	 * （下落物理.md §六 / 操虫斩曲线.md §六）。方向保持；近零速取朝向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerial|Presentation", meta = (ClampMin = "0.0"))
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

	/** Measured 187 hold/straight/glide; both B and RT+B use this shared profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Movement")
	FSegmentedDirectionalMovementProfile KinsectSlashDashProfile;

	/** Compatibility fallback only; new assets/defaults execute the measured profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy")
	bool bUseCapturedKinsectSlashDash = true;

	/** Legacy scalar-distance path; unused when bUseCapturedKinsectSlashDash is true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (ClampMin = "0.01"))
	float KinsectSlashMaxDistance = 840.0f;

	/** Legacy sample-span duration; the new profile totals 0.250 game seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (ClampMin = "0.01"))
	float KinsectSlashDashDuration = 0.242f;

	/** Captured maximum upward 187 launch pitch; higher RT+B aim is clamped to the Rise data limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Aim",
		meta = (ClampMin = "0.0", ClampMax = "89.0", ForceUnits = "deg"))
	float KinsectSlashMaxUpwardLaunchPitchDegrees = 53.74f;

	/** Legacy normalized distance curve; not the new pitch-dependent glide trajectory. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy")
	TObjectPtr<UCurveFloat> KinsectSlashDistanceCurve;

	/** 186/187 临时挂接的武器 Socket；该 Socket Transform 是附着偏移的唯一来源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Attachment")
	FName KinsectSlashAttachSocket = TEXT("IG_Kinsect");

	/** Legacy serialized field. Runtime collision is configured by AttackSegments[0].Collision.TraceRegions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use AttackSegments[0].Collision.TraceRegions."))
	float KinsectSlashProbeRadius = 20.0f;

	/** Legacy serialized field; the dedicated attachment Socket owns its position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use KinsectSlashAttachSocket."))
	float KinsectSlashProbeForwardOffset = 60.0f;

	/** Legacy serialized field; socket sampling is configured on AttackSegments[0].Collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use AttackSegments[0].Collision.TraceRegions."))
	float KinsectSlashProbeMaxStep = 20.0f;

	/** Legacy serialized field; AttackCollision notify source-frame bounds own this timing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectSlash|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use the AttackCollision notify-state source-frame range."))
	float KinsectSlashLateHitWindow = 0.12f;

	// ── 强化操虫穿刺（M5 本招）的三份录制曲线 ─────────────────────────────
	//
	// 三份 profile 与 §5.4 的三份规范 CSV 一一对应：200 起手、201 下潜、203 落地。
	// 曲线本体（UCurveVector）由 M3 的命令行生成器从 CSV 写出，这里只放引用与
	// 同一模型的尾段参数；数值全部来自 `Saved/_mhr_curves/强化操虫穿刺_生成审计.json`，
	// 不是手调值。关键阈值、尾段与看门狗都在 `.cpp` 的 IsDataValid 里复核。

	/** 200 起手段：保护飞行，曲线跑完即转 201。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectPiercer|Movement")
	FRecordedLocalTrajectoryProfile KinsectPiercerStartProfile;

	/**
	 * 201 下潜段：允许真实触地，覆盖之外按同模型续行。
	 *
	 * 它的覆盖时长（实测 0.3665 s）是**被触地截断**的结果，不是动作本身的长度 ——
	 * 所以这一段永远不会按时长结束，只由真实触地或看门狗结束。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectPiercer|Movement")
	FRecordedLocalTrajectoryProfile KinsectPiercerDiveProfile;

	/** 203 录制真值对照；运行时落地改用 Montage Root Motion，不读取此 profile。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectPiercer|Reference")
	FRecordedLocalTrajectoryProfile KinsectPiercerLandingProfile;

	/**
	 * 201 的独立看门狗（秒）。
	 *
	 * 只处理「既没触地、也没结束」的异常会话 —— 正常落地走真实 `ProcessLanded`，
	 * 一秒都不会用到它。它绝不是 201→203 的定时出口。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectPiercer|Movement",
		meta = (ClampMin = "0.1", ForceUnits = "s"))
	float KinsectPiercerDiveWatchdogSeconds = 12.0f;

	/** 203 的初始视觉混入；主 Montage 换 `_Over` 那一下用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KinsectPiercer|Presentation",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float KinsectPiercerLandingBlendInTime = 0.1f;

	// ── 急袭突刺（M2 运行时）──

	/**
	 * 380 的参数化基曲线：X=水平位移时间积分 H(t)（秒），Y=0，Z=世界竖直位移（cm）。
	 * 运行时仅按 LockedPlanarVelocity 大小缩放 X；保护飞行转下潜时不重置此曲线时钟。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DescendingThrust|Movement")
	FRecordedLocalTrajectoryProfile DescendingThrustAirProfile;

	/** 曲线尾段仍未真实接地时的异常看门狗；不作为正常落地出口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DescendingThrust|Movement",
		meta = (ClampMin = "0.1", ForceUnits = "s"))
	float DescendingThrustAirWatchdogSeconds = 12.0f;

	/** 真实落地切到常驻381 Over Montage时的视觉混入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DescendingThrust|Presentation",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DescendingThrustLandingBlendInTime = 0.1f;

	/** 猎虫滑翔追踪虫印的最大距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectGlideMarkMaxDistance = 5000.0f;

	/** 虫印弹最大射程（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectMarkMaxDistance = 6000.0f;

	/** 虫印持续时间（秒）；M6.3合同固定为90秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectMarkDuration = 90.0f;

	/** 虫印在表面切向的半宽/半高（cm）；与约1m视觉云团对齐，100cm×100cm斑块对应50cm。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Mark", meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float KinsectMarkPatchHalfExtent = 50.0f;

	/** 命中点距离虫印表面的最大法向厚度（cm）；初始工程容差，E6.2用实际碰撞调校。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Mark", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float KinsectMarkSurfaceNormalTolerance = 5.0f;

	/** 自动寻印起点到虫印的最大距离（cm）；0表示未调校/关闭自动寻印，不能借用粉尘聚爆半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Mark|Automation", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float KinsectAutoMarkSearchRadius = 3000.0f;

	/** 到达虫印后的自动攻击间隔（世界游戏秒）；0表示未调校/关闭自动攻击。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Mark|Automation", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float KinsectAutoMarkAttackInterval = 2.0f;

	/** 命中虫印后两次可产粉的最小间隔；觉虫击贯通产粉不受此冷却约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float MarkedHitPowderCooldown = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkProjectile", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkProjectile", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkProjectile", meta = (ClampMin = "0.01"))
	float KinsectMarkProjectileLifetime = 5.0f;

	/** M3 原生虫印弹行为类；E5 可填只负责表现配置的蓝图子类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkProjectile")
	TSubclassOf<AIGMarkProjectile> KinsectMarkProjectileClass;

	/** 虫印弹从带 WeaponTrace ComponentTag 的武器 Mesh 上该 Socket 发射。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkProjectile")
	FName KinsectMarkLaunchSocket = TEXT("IG_FrontTip");

	/** 虫印成功附着后的持续视觉；应是一团附着在 Hitzone 上的黄色粉末。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MarkEffect")
	TObjectPtr<UParticleSystem> KinsectMarkEstablishedEffect;

	/** 无虫印时滑翔沿角色 Forward 的兜底飞行距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Glide", meta = (ClampMin = "0.01"))
	float KinsectGlideFallbackDistance = 900.0f;

	/** 兜底滑翔附加的抬升高度（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Glide", meta = (ClampMin = "0.01"))
	float KinsectGlideFallbackLiftHeight = 150.0f;

	/** Legacy tuning field; 199 now uses a time-bounded CMC flight, so this is not consumed by the runtime path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightMaxDistance = 1500.0f;

	/** Legacy timer field; HunterFlightStart Notify is authoritative and this value is not scheduled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightStartTime = 0.4f;

	/** 20261012 F9：20段199均持续0.375–0.383s，采用0.38s保护飞行曲线，不由初始接地提前结束。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight",
		meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightDurationSeconds = 0.38f;

	/** 方向取松键时冻结的准心目标；20261012干净空中样本初速4809/4842cm/s，保留4826。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight",
		meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightSpeed = 4826.0f;

	/** 199 线性拟合重力候选约 1190cm/s²（UE Z-up）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight",
		meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightGravityMagnitude = 1190.0f;

	/** 近处地面准心的最短199路程；约550cm来自低角度短程样本，不按相机角度分档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight", meta = (ClampMin = "0.01"))
	float AwakenedGroundTargetMinimumDistance = 550.0f;

	/** 地面目标199的小抛弧初速：约140cm/s、1190重力对应约8cm抬升。不是向地板硬冲。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight", meta = (ClampMin = "0.01"))
	float AwakenedGroundTargetVerticalLaunchSpeed = 140.0f;

	/** 206 回归速度候选；入口原始行常为零，默认仅采用拟合出的垂直分量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight")
	FVector AwakenedHunterFallHandoffVelocity = FVector(0.0f, 0.0f, 900.0f);

	/** 206 线性拟合重力候选约 2885cm/s²（UE Z-up）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight",
		meta = (ClampMin = "0.01"))
	float AwakenedHunterFallGravityMagnitude = 2885.0f;

	/** 常驻 206 下坠表现；编辑器阶段指定正式 Montage，Root Motion 必须关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Hunter Flight")
	TObjectPtr<UAnimMontage> AwakenedHunterFallMontage;

	/** Release/throw horizontal yaw correction limit around the character's release-frame facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Aim", meta = (ClampMin = "0.0", ClampMax = "180.0", ForceUnits = "deg"))
	float AwakenedAimHorizontalCorrectionAngle = 90.0f;

	/** Release/throw vertical pitch correction limit around the character's release-frame facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened|Aim", meta = (ClampMin = "0.0", ClampMax = "90.0", ForceUnits = "deg"))
	float AwakenedAimCorrectionAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DivingWyvern", meta = (ClampMin = "0.01"))
	float DivingWyvernDistance = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DivingWyvern", meta = (ClampMin = "0.01"))
	float DivingWyvernHeight = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DivingWyvern", meta = (ClampMin = "0.01"))
	float DivingWyvernDuration = 0.8f;

	/** 急袭突刺 AdditiveInertia 的空气输入修正缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DescendingThrust", meta = (ClampMin = "0.0"))
	float DescendingThrustAirControl = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder", meta = (ClampMin = "0.01"))
	float PowderGatherRadius = 3000.0f;

	/** Resource-owned gathering delay after the Kinsect reaches the aimed point; default 1 second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Vortex", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float PowderVortexDelay = 1.0f;

	/** Direct first-stage damage selected by powder-count tiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Vortex", meta = (ClampMin = "0.0"))
	float PowderVortexDamageAt1 = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Vortex", meta = (ClampMin = "0.0"))
	float PowderVortexDamageAt5 = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Vortex", meta = (ClampMin = "0.0"))
	float PowderVortexDamageAt10 = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Legacy", meta = (DeprecatedProperty, ClampMin = "0.01"))
	float PowderGatherDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Legacy", meta = (DeprecatedProperty, ClampMin = "1"))
	int32 PowderGatherMaxCount = 5;

	/** Non-colliding persistent powder actor class. Assign BP_IG_PowderCloud to enable the powder runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Actor")
	TSubclassOf<AIGPowderActor> PowderActorClass;

	/** Powder lifetime in world game seconds. The current design fixes this at 180 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float PowderLifetime = 180.0f;

	/** Maximum live powder actors owned by one weapon Runtime; Available and Reserved both count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "1"))
	int32 PowderMaxCount = 100;

	/** Natural expiry explosion radius (default 150 cm); damage only, not visible particle size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float PowderNaturalExplosionRadius = 150.0f;

	/** Legacy fixed test contribution; production powder creation uses kinsect power and PowderMotionValue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.0"))
	float PowderDamageContribution = 0.0f;

	/** Powder action value as a raw percent; 100 means one full Kinsect attack-power unit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.0"))
	float PowderMotionValue = 100.0f;

	/** Deprecated normalized multiplier retained for loading old DA data; runtime no longer reads it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Legacy", meta = (DeprecatedProperty, ClampMin = "0.0"))
	float PowderDamageMultiplier = 1.0f;

	/** Distance beyond which this Runtime's powder is removed without damage; E6.2 default is 100m (10000cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Tuning", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float PowderDespawnDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Legacy", meta = (DeprecatedProperty, ClampMin = "0.0"))
	float PowderVortexBaseDamage = 0.0f;

	/** Radius shared by the two Powder Vortex damage stages; E6.2 default is 300cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Vortex", meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float PowderVortexExplosionRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Legacy", meta = (DeprecatedProperty, ClampMin = "0.0"))
	float PowderVortexBaseStagger = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Feedback")
	FGameplayTag PowderVortexHitCueTag;

	/** One spatial cue at the committed gather center; target hit cues remain per-target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder|Feedback")
	FGameplayTag PowderVortexExplosionCueTag;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
