// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZAimComponent.generated.h"

class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAimTargetChanged,
	AActor*, Target, FGameplayTag, HitzoneTag, FGameplayTag, ExtractColor);

/**
 * UMHGZAimComponent — 瞄准检测组件（挂载到 Character）
 * LT 按下→ASC 持有 Combat.State.Aiming → Tick 中每帧射线检测 → Delegate 驱动 UI 准心
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType)
class UMHGZAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZAimComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ── 状态 ──
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Aim")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Aim")
	FGameplayTag GetCurrentExtractColor() const { return CurrentAimExtractColor; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Aim")
	FVector GetCameraForward() const { return CachedCameraDir; }

	/** 输入解析瞬间执行独立射线并返回不可变快照。 */
	FWeaponAimSnapshot CaptureAimSnapshot(EWeaponAimSnapshotContext Context) const;

	// ── 配置 ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim|Config")
	float AimMaxDistance = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim|Config")
	TEnumAsByte<ECollisionChannel> AimChannel = ECC_GameTraceChannel1;

	// ── Delegate ──
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Aim")
	FOnAimTargetChanged OnAimTargetChanged;

protected:
	void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void PerformAimTrace();

private:
	bool bIsAiming = false;

	UPROPERTY()
	TWeakObjectPtr<AActor> CurrentAimTarget;

	FGameplayTag CurrentAimHitzoneTag;
	FGameplayTag CurrentAimExtractColor;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;

	// 缓存相机朝向（供 GA_SendKinsect 读取）
	FVector CachedCameraDir = FVector::ForwardVector;

	FTimerHandle AimTagCheckTimer;

	FGameplayTag PreviousExtractColor;
	FGameplayTag PreviousHitzoneTag;
	TWeakObjectPtr<AActor> PreviousTarget;
};
