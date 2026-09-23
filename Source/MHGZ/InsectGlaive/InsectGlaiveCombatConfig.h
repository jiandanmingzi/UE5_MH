// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "InsectGlaiveCombatConfig.generated.h"

class UGameplayEffect;
class UInsectGlaiveKinsectData;
class USoundBase;
class AIGMarkProjectile;
class UParticleSystem;
class UAnimMontage;

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
	float SendKinsectMotionValue = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float DrawSendKinsectMotionValue = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|MotionValues", meta = (ClampMin = "0.0"))
	float AwakenedKinsectMotionValue = 0.30f;

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
	int32 MaxDanceStacks = 0;

	/** 索引即层数；索引 0 必须为 1.0，长度必须为 MaxDanceStacks + 1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "0.0"))
	TArray<float> DanceDamageMultipliers = { 1.0f };

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

	/** 操虫斩最大飞行距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectSlashMaxDistance = 1200.0f;

	/** 猎虫滑翔追踪虫印的最大距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectGlideMarkMaxDistance = 5000.0f;

	/** 虫印弹最大射程（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectMarkMaxDistance = 6000.0f;

	/** 虫印持续时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Movement", meta = (ClampMin = "0.01"))
	float KinsectMarkDuration = 45.0f;

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

	/** 觉虫击猎虫贯通最大距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.01"))
	float AwakenedKinsectMaxDistance = 2000.0f;

	/** 觉虫击猎人位移最大距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightMaxDistance = 1500.0f;

	/** HunterFlight 位移在 Montage 中开始的时刻（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.01"))
	float AwakenedHunterFlightStartTime = 0.4f;

	/** 觉虫击相对角色 Forward 的瞄准修正角上限（度）；[0, 180]，超界 Clamp 到锥边缘 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened", meta = (ClampMin = "0.0", ClampMax = "180.0"))
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
	float PowderGatherRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder", meta = (ClampMin = "0.01"))
	float PowderGatherDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powder", meta = (ClampMin = "1"))
	int32 PowderGatherMaxCount = 5;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
