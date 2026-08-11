// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "MHGZDummyConfig.generated.h"

class USkeletalMesh;
class UAnimMontage;

/** One spherical hitzone of the training dummy. */
USTRUCT(BlueprintType)
struct FDummyHitzoneConfig
{
	GENERATED_BODY()

	/** Bone to attach to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName BoneName;

	/** Part tag (Hitzone.Head / Hitzone.Torso / Hitzone.Leg ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitzoneTag;

	/** Extract color tag; the leaf segment must be Red / White / Orange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ExtractColorTag;

	/** Flesh quality: damage multiplier (0.2 = hard, 1.0 = weak spot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DefenseMultiplier = 1.0f;

	/** Stagger absorption rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StaggerRate = 0.f;

	/** Location relative to the attachment bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RelativeLocation = FVector::ZeroVector;

	/** Sphere radius in cm; must be > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Radius = 30.f;

	/**
	 * Deprecated serialization shell kept only for loading compatibility.
	 * Runtime generation uses RelativeLocation + Radius and never reads this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (ToolTip = "Deprecated serialization shell; runtime uses RelativeLocation/Radius."))
	FVector HalfExtent = FVector(30, 30, 30);
};

/** Deterministic counter-test attack definition for the training dummy. */
USTRUCT(BlueprintType)
struct FDummyCounterAttackConfig
{
	GENERATED_BODY()

	/** Periodic auto-attack switch; M2 does not implement timers (use SubmitCounterTestAttack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float InitialDelay = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Interval = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float TelegraphDuration = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float ActiveDuration = 1.f;

	/** Hit-point offset relative to the target capsule center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector HitOffset = FVector::ZeroVector;

	/** Damage settled on the player when the hit is not countered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	/** Whether the dummy attack can be consumed by a counter window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCounterable = true;

	/** Source attack tag carried into FIncomingHitContext. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag SourceActionTag;

	/** Stagger tag applied when the hit is not countered (Combat.Stagger.*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag StaggerTag;

	/** Base break value for the attack (planned SetByCaller source; see resolver default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float BaseStagger = 10.f;
};

/**
 * UMHGZDummyConfig - training dummy DataAsset.
 * M2 requires Red/White/Orange hitzones that do not overlap (RelativeLocation + Radius).
 */
UCLASS(BlueprintType)
class UMHGZDummyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> DisplayMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimMontage> LoopingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FDummyHitzoneConfig> Hitzones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FDummyCounterAttackConfig CounterTestAttack;

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
	 * - every HitzoneTag is valid and Radius > 0;
	 * - every ExtractColorTag leaf is Red/White/Orange and all three colors appear;
	 * - spheres do not overlap when compared by RelativeLocation + Radius.
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
			if (!HZ.HitzoneTag.IsValid())
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] has an invalid HitzoneTag."), Index);
				return false;
			}
			if (HZ.Radius <= 0.f || !FMath::IsFinite(HZ.Radius))
			{
				OutError = FString::Printf(TEXT("Hitzone[%d] Radius must be > 0."), Index);
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

		for (int32 A = 0; A < InHitzones.Num(); ++A)
		{
			for (int32 B = A + 1; B < InHitzones.Num(); ++B)
			{
				if (AreHitzoneSpheresOverlapping(
					InHitzones[A].RelativeLocation, InHitzones[A].Radius,
					InHitzones[B].RelativeLocation, InHitzones[B].Radius))
				{
					OutError = FString::Printf(
						TEXT("Hitzone[%d] and Hitzone[%d] spheres overlap."), A, B);
					return false;
				}
			}
		}

		return true;
	}

	/** Full-config runtime validation (hitzones + counter attack sanity). */
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
		return true;
	}

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override
	{
		EDataValidationResult Result = Super::IsDataValid(Context);
		FString Error;
		if (!ValidateHitzoneConfigs(Hitzones, Error))
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
