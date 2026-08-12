// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"
#include "InsectGlaiveCombatConfig.generated.h"

class UGameplayEffect;
class UInsectGlaiveKinsectData;
class USoundBase;
class AIGMarkProjectile;

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
 * UInsectGlaiveCombatConfig —— 同一把虫棍动作规则与可调数值的唯一入口。
 * 由 DA_WeaponRuntime_IG 经 UWeaponRuntimeDefinition 引用；
 * 方向/组合键阈值不属于本资产，由通用 UWeaponInputProfile 提供。
 */
UCLASS(BlueprintType)
class MHGZ_API UInsectGlaiveCombatConfig : public UWeaponCombatConfigBase
{
	GENERATED_BODY()

public:
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement", meta = (ClampMin = "0.0"))
	float DanceVaultApexHeight = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement", meta = (ClampMin = "0.0"))
	float DanceVaultDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance|Movement")
	FVector DanceVaultLaunchVelocity = FVector::ZeroVector;

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
