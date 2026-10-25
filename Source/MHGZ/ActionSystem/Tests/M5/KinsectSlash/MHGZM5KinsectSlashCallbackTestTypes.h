// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZM5KinsectSlashCallbackTestTypes.generated.h"

class UAbilitySystemComponent;
class UMHGZKinsectSlashAbility;
class UMHGZWeaponRuntimeHostComponent;
class USkeletalMeshComponent;

/** Observes the real resource broadcast and optionally cancels its owning GA inline. */
UCLASS()
class UMHGZM5KinsectSlashDanceCancellationObserver : public UObject
{
	GENERATED_BODY()

public:
	/** 当前关联角色的能力系统组件，用于读取标签、属性和执行效果。 */
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;

	/** 测试中被观察的操虫斩能力实例。 */
	UPROPERTY()
	TObjectPtr<UMHGZKinsectSlashAbility> Ability;

	/** 持有武器运行期的宿主，用于验证当前代际与动作身份。 */
	UPROPERTY()
	TObjectPtr<UMHGZWeaponRuntimeHostComponent> Host;

	/** 测试中实际播放被观察蒙太奇的骨骼网格组件。 */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	bool bCancelOnDanceStateChanged = true;
	int32 KinsectSlashBroadcastCount = 0;
	int32 StacksAtDanceBroadcast = 0;
	int32 DanceMontageInstanceID = INDEX_NONE;
	bool bObservedActiveAbility = false;
	bool bObservedExactRegisteredDanceMontage = false;
	bool bAppliedHitstun = false;
	bool bCancelledSynchronously = false;

	UFUNCTION()
	void HandleDanceStateChanged(int32 Stacks, EIGDanceSource Source);
};
