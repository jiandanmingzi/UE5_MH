// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "MHGZMonsterHitzoneComponent.h"
#include "MHGZDummyConfig.generated.h"

class USkeletalMesh;
class UAnimMontage;

/** Query geometry and identity of one training-dummy part. */
USTRUCT(BlueprintType)
struct FDummyHitzoneConfig
{
	GENERATED_BODY()

	/** 部位查询绑定的骨骼名；None表示挂到角色根组件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName BoneName;

	/** 受击部位的语义标签，如头部、躯干或腿部。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitzoneTag;

	/** 猎虫命中该部位可采集的精华颜色，末级标签须为Red、White或Orange。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ExtractColorTag;

	/** 物理肉质百分数；按伤害类型的肉质表缺项时使用，100表示100%吸收。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DefenseMultiplier = 100.0f;

	/** 斩/打/弹物理肉质百分数；缺项兼容 DefenseMultiplier，0免疫。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "100.0"))
	TMap<EMHGZPhysicalDamageType, float> PhysicalHitzoneMultipliers;

	/** 火/水/雷/冰/龙肉质倍率；缺项=0。0.20表示肉质20。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<EMHGZElementType, float> ElementHitzoneMultipliers;

	/** 该部位吸收基础硬直量时使用的倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StaggerRate = 0.f;

	/** 相对挂载骨骼或根组件的位置偏移，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RelativeLocation = FVector::ZeroVector;

	/** 球形受击查询半径，单位为厘米；仅Sphere形状读取，必须大于零。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Radius = 30.f;

	/** 该部位的受击查询形状；可选球、盒或实体凸形网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMHGZHitzoneShape Shape = EMHGZHitzoneShape::Sphere;

	/** 盒形查询的三轴半尺寸，单位为厘米；训练木桩应用配置时仍乘历史1.5倍尺寸倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	FVector BoxHalfExtent = FVector(51.0f, 51.0f, 38.5f);

	/** 贴合外观的实体凸形查询网格；仅Mesh形状读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UStaticMesh> QueryMesh;

	/** 查询网格缩放，不再乘训练木桩历史尺寸倍率；位置仍沿用RelativeLocation约定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector QueryMeshScale = FVector::OneVector;

};

/** Deterministic counter-test attack definition for the training dummy. */
USTRUCT(BlueprintType)
struct FDummyCounterAttackConfig
{
	GENERATED_BODY()

	/** 周期自动反击测试攻击的预留开关；当前没有自动攻击计时器，需显式调用SubmitCounterTestAttack。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	/** 周期自动攻击预留的首次等待时长，单位为秒；当前手动测试入口不读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float InitialDelay = 3.f;

	/** 周期自动攻击预留的间隔，单位为秒；当前手动测试入口不读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Interval = 8.f;

	/** 自动攻击预留的预警时长，单位为秒；当前手动测试入口不读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 1.f;

	/** 自动攻击预留的判定有效时长，单位为秒；当前手动测试入口不读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float ActiveDuration = 1.f;

	/** 相对被测目标胶囊中心的命中点偏移，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector HitOffset = FVector::ZeroVector;

	/** 该次成功命中需要结算的伤害量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	/** 是否允许反击窗口拦截本次命中；关闭时跳过反击消费。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCounterable = true;

	/** 写入玩家受击上下文的来源攻击标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag SourceActionTag;

	/** 未被拦截时施加的受击等级标签；无效标签不触发对应硬直。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag StaggerTag;

	/** 预留基础硬直量；当前手动反击测试不读取，实际使用受击解析器的默认基础硬直。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float BaseStagger = 10.f;
};

/** Configurable ground-fire ring emitted at the white/orange boundary of the demo dummy. */
USTRUCT(BlueprintType)
struct FDummyFireRingConfig
{
	GENERATED_BODY()

	/** 是否开启木桩周期火圈；蓝图也可在运行时独立开关。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	bool bEnabled = true;

	/** 开始运行后到首次释放火圈的等待时间，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.0"))
	float InitialDelay = 5.0f;

	/** 两次火圈释放之间的时间间隔，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.01"))
	float EmissionInterval = 5.0f;

	/** 每次火圈可见并允许命中的持续时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.01"))
	float ActiveDuration = 1.0f;

	/** 同一火圈持续期间两次伤害检查的时间间隔，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.01"))
	float HitInterval = 0.25f;

	/** 火圈中心相对木桩根的位置，单位为厘米；默认位于白色与橙色区域交界。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	FVector RelativeLocation = FVector(0.f, 0.f, -37.5f);

	/** 火圈中心线半径，单位为厘米；100厘米对应2米直径。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "1.0"))
	float Radius = 100.0f;

	/** 火圈中心线半径两侧的水平判定容差，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.0"))
	float RingHalfThickness = 18.0f;

	/** 相对火圈中心的竖直判定半高，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.0"))
	float VerticalHalfHeight = 75.0f;

	/** 环绕火圈分布的可视引导网格段数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "3", ClampMax = "64"))
	int32 VisualSegmentCount = 24;

	/** 每次成功命中检查施加的伤害量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring", meta = (ClampMin = "0.0"))
	float Damage = 10.0f;

	/** 是否允许反击拦截火圈命中；普通地面火圈默认不可反击，仅用于显式测试。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	bool bCounterable = false;

	/** 传递给玩家受击解析器的火圈来源标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	FGameplayTag SourceActionTag;

	/** 未被拦截时施加的受击等级标签；无效标签不触发对应硬直。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	FGameplayTag StaggerTag;
};

/**
 * UMHGZDummyConfig - training dummy DataAsset.
 * Requires Red/White/Orange identities. Query regions may overlap at boundaries
 * to cover visual/physical surfaces; each attack owns its normal target deduplication.
 */
UCLASS(BlueprintType)
class UMHGZDummyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 木桩可选的显示骨骼网格资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> DisplayMesh;

	/** 木桩需要循环播放时使用的蒙太奇资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimMontage> LoopingMontage;

	/** 木桩受击部位配置列表，包含几何、肉质和精华身份。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FDummyHitzoneConfig> Hitzones;

	/** 木桩用于验证反击窗口的攻击配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FDummyCounterAttackConfig CounterTestAttack;

	/** 圆柱木桩在白橙区域交界释放的周期火圈配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fire Ring")
	FDummyFireRingConfig FireRing;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("DummyConfig"), GetFName());
	}

	/** True when the two spheres overlap (distance < RadiusA + RadiusB; touching is allowed). */
	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	static bool AreHitzoneSpheresOverlapping(
		const FVector& LocationA, float RadiusA,
		const FVector& LocationB, float RadiusB)
	{
		const float MinDistance = RadiusA + RadiusB;
		return FVector::DistSquared(LocationA, LocationB)
			< MinDistance * MinDistance - KINDA_SMALL_NUMBER;
	}

	/**
	 * Runtime/editor validation of a hitzone list:
	 * - every HitzoneTag is valid and shape dimensions are finite and positive;
	 * - every ExtractColorTag leaf is Red/White/Orange and all three colors appear;
	 * - overlap is allowed for complete surface coverage, not an invalid configuration.
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	static bool ValidateHitzoneConfigs(
		const TArray<FDummyHitzoneConfig>& InHitzones, FString& OutError)
	{
		OutError.Reset();

		int32 RedCount = 0;
		int32 WhiteCount = 0;
		int32 OrangeCount = 0;

		auto LeafSegment = [](const FGameplayTag& Tag) -> FString
		{
			const FString TagString = Tag.GetTagName().ToString();
			int32 DotIndex = INDEX_NONE;
			if (TagString.FindLastChar(TEXT('.'), DotIndex))
			{
				return TagString.RightChop(DotIndex + 1);
			}
			return TagString;
		};

		for (int32 Index = 0; Index < InHitzones.Num(); ++Index)
		{
			const FDummyHitzoneConfig& HZ = InHitzones[Index];
			if (!FMath::IsFinite(HZ.DefenseMultiplier)
				|| HZ.DefenseMultiplier < 0.0f || HZ.DefenseMultiplier > 100.0f)
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] DefenseMultiplier must be a physical hitzone percent in [0, 100]."), Index);
				return false;
			}
			for (const auto& Entry : HZ.PhysicalHitzoneMultipliers)
			{
				if (static_cast<uint8>(Entry.Key) > static_cast<uint8>(EMHGZPhysicalDamageType::Shot)
					|| !FMath::IsFinite(Entry.Value) || Entry.Value < 0.0f || Entry.Value > 100.0f)
				{
					OutError = FString::Printf(TEXT("Hitzone[%d] physical hitzone percents must use valid types and [0, 100]."), Index);
					return false;
				}
			}
			for (const auto& Entry : HZ.ElementHitzoneMultipliers)
			{
				if (Entry.Key == EMHGZElementType::None
					|| static_cast<uint8>(Entry.Key) > static_cast<uint8>(EMHGZElementType::Dragon)
					|| !FMath::IsFinite(Entry.Value) || Entry.Value < 0.0f)
				{
					OutError = FString::Printf(TEXT("Hitzone[%d] elemental flesh values require Fire/Water/Thunder/Ice/Dragon and finite >= 0."), Index);
					return false;
				}
			}
			if (!HZ.HitzoneTag.IsValid())
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] has an invalid HitzoneTag."), Index);
				return false;
			}
			if (HZ.Shape != EMHGZHitzoneShape::Sphere && HZ.Shape != EMHGZHitzoneShape::Box
				&& HZ.Shape != EMHGZHitzoneShape::Mesh)
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] has an unsupported query shape."), Index);
				return false;
			}
			if (HZ.RelativeLocation.ContainsNaN())
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] location must be finite."), Index);
				return false;
			}
			if (HZ.Shape == EMHGZHitzoneShape::Mesh && (HZ.QueryMesh.IsNull()
				|| HZ.QueryMeshScale.ContainsNaN() || HZ.QueryMeshScale.GetMin() <= 0.f))
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] requires a query mesh and positive finite scale."), Index);
				return false;
			}
			if (HZ.Shape == EMHGZHitzoneShape::Sphere && (HZ.Radius <= 0.f || !FMath::IsFinite(HZ.Radius)))
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] Radius must be > 0."), Index);
				return false;
			}
			if (HZ.Shape == EMHGZHitzoneShape::Box && (HZ.BoxHalfExtent.ContainsNaN()
				|| HZ.BoxHalfExtent.X <= 0.f || HZ.BoxHalfExtent.Y <= 0.f || HZ.BoxHalfExtent.Z <= 0.f))
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] box half extent must be finite and > 0."), Index);
				return false;
			}

			const FString Leaf = LeafSegment(HZ.ExtractColorTag);
			if (Leaf == TEXT("Red"))
			{
				++RedCount;
			}
			else if (Leaf == TEXT("White"))
			{
				++WhiteCount;
			}
			else if (Leaf == TEXT("Orange"))
			{
				++OrangeCount;
			}
			else
			{
				OutError = FString::Printf(
					TEXT("Hitzone[%d] ExtractColorTag must end with Red/White/Orange (got '%s')."),
					Index, *TagStringOf(HZ.ExtractColorTag));
				return false;
			}
		}

		if (InHitzones.Num() != 3 || RedCount != 1 || WhiteCount != 1 || OrangeCount != 1)
		{
			OutError = TEXT("Hitzones must contain exactly one Red, one White and one Orange part.");
			return false;
		}

		return true;
	}

	/** Full-config runtime validation (hitzones + dummy attack and fire-ring sanity). */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	bool ValidateConfig(FString& OutError) const
	{
		if (!ValidateHitzoneConfigs(Hitzones, OutError))
		{
			return false;
		}
		if (!FMath::IsFinite(CounterTestAttack.Damage) || CounterTestAttack.Damage < 0.f)
		{
			OutError = TEXT("CounterTestAttack.Damage must be finite and >= 0.");
			return false;
		}
		if (!FMath::IsFinite(CounterTestAttack.BaseStagger) || CounterTestAttack.BaseStagger < 0.f)
		{
			OutError = TEXT("CounterTestAttack.BaseStagger must be finite and >= 0.");
			return false;
		}
		if (!FMath::IsFinite(FireRing.Damage) || FireRing.Damage < 0.f)
		{
			OutError = TEXT("FireRing.Damage must be finite and >= 0.");
			return false;
		}
		if (!FMath::IsFinite(FireRing.InitialDelay) || FireRing.InitialDelay < 0.f
			|| !FMath::IsFinite(FireRing.EmissionInterval) || FireRing.EmissionInterval <= 0.f
			|| !FMath::IsFinite(FireRing.ActiveDuration) || FireRing.ActiveDuration <= 0.f
			|| !FMath::IsFinite(FireRing.HitInterval) || FireRing.HitInterval <= 0.f)
		{
			OutError = TEXT("FireRing delay/interval/duration values must be finite; interval, duration and hit interval must be > 0.");
			return false;
		}
		if (FireRing.ActiveDuration > FireRing.EmissionInterval)
		{
			OutError = TEXT("FireRing.ActiveDuration must not exceed FireRing.EmissionInterval.");
			return false;
		}
		if (!FMath::IsFinite(FireRing.Radius) || FireRing.Radius <= 0.f
			|| !FMath::IsFinite(FireRing.RingHalfThickness) || FireRing.RingHalfThickness < 0.f
			|| !FMath::IsFinite(FireRing.VerticalHalfHeight) || FireRing.VerticalHalfHeight < 0.f
			|| FireRing.VisualSegmentCount < 3 || FireRing.VisualSegmentCount > 64)
		{
			OutError = TEXT("FireRing radius/extent/visual segment count is invalid.");
			return false;
		}
		return true;
	}

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override
	{
		EDataValidationResult Result = Super::IsDataValid(Context);
		FString Error;
		if (!ValidateConfig(Error))
		{
			Context.AddError(FText::FromString(Error));
			Result = EDataValidationResult::Invalid;
		}
		else if (Result == EDataValidationResult::NotValidated)
		{
			Result = EDataValidationResult::Valid;
		}
		return Result;
	}
#endif

private:
	static FString TagStringOf(const FGameplayTag& Tag)
	{
		return Tag.GetTagName().ToString();
	}
};
