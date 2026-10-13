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
 * LT/Kinsect 与 RT/Action 瞄准时为准心显示执行同一条屏幕中心可见性射线；
 * 输入快照仍由 CaptureAimSnapshot 独立即时采集。
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

	AActor* GetCurrentAimTarget() const { return CurrentAimTarget.Get(); }
	FGameplayTag GetCurrentAimHitzoneTag() const { return CurrentAimHitzoneTag; }
	FGameplayTag GetCurrentAimExtractColor() const { return CurrentAimExtractColor; }

	/** 输入解析瞬间执行独立射线并返回不可变快照。 */
	FWeaponAimSnapshot CaptureAimSnapshot(EWeaponAimSnapshotContext Context) const;

	/**
	 * Sample the current screen-centre ray for an action's late AimLock point,
	 * resolve third-person parallax from LaunchOrigin, and clamp the resulting
	 * launch pitch to the active PlayerCameraManager limits. This is separate
	 * from CaptureAimSnapshot so existing input-time snapshot consumers retain
	 * their current semantics.
	 */
	bool CaptureAimLockSample(const FVector& LaunchOrigin, FWeaponAimLockSample& OutSample) const;

	// ── ASC 生命周期绑定 ──
	/** 绑定 ASC 并订阅 Action/Kinsect瞄准Tag；幂等；切换ASC时先解绑再同步当前Tag count。 */
	void BindToAbilitySystem(UAbilitySystemComponent* InASC);

	/** 解绑 ASC 并清空当前目标显示。 */
	void UnbindFromAbilitySystem();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ── 配置 ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim|Config")
	float AimMaxDistance = 3000.f;

	/** Visibility 通道：墙体 Block 遮挡；仅 ObjectType=ECC_GameTraceChannel3 的 Hitzone 视为有效目标。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim|Config")
	TEnumAsByte<ECollisionChannel> AimChannel = ECC_Visibility;

	// ── Delegate ──
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Aim")
	FOnAimTargetChanged OnAimTargetChanged;

protected:
	void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void PerformAimTrace();

private:
	void ClearTargetDisplay();

	bool bIsAiming = false;

	UPROPERTY()
	TWeakObjectPtr<AActor> CurrentAimTarget;

	FGameplayTag CurrentAimHitzoneTag;
	FGameplayTag CurrentAimExtractColor;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;

	FDelegateHandle ActionAimTagDelegateHandle;
	FDelegateHandle KinsectAimTagDelegateHandle;
	bool bActionAimActive = false;

	// 缓存相机朝向（供 GA_SendKinsect 读取）
	FVector CachedCameraDir = FVector::ForwardVector;

	FTimerHandle AimTagCheckTimer;

	FGameplayTag PreviousExtractColor;
	FGameplayTag PreviousHitzoneTag;
	TWeakObjectPtr<AActor> PreviousTarget;
};
