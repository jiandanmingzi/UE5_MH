// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class ACharacter;
class UAbilitySystemComponent;
class UCharacterMovementComponent;
class UMHGZWeaponRuntimeHostComponent;

/** 空中动作收尾的单步操作；是否执行及调用顺序由各GA按原所有权规则决定。 */
namespace MHGZAerialActionCleanup
{
/** 保持原顺序：先结束任务，再清空原字段；不提前清空，不改变任务委托。 */
template <typename TTask>
void EndTaskAndReset(TObjectPtr<TTask>& Task)
{
	if (Task)
	{
		Task->EndTask();
		Task = nullptr;
	}
}

/** 移除本动作保存的运行期失效委托，再重置句柄；Host已失效时仍清句柄。 */
MHGZ_API void UnbindRuntimeInvalidation(UMHGZWeaponRuntimeHostComponent* Host,
	FDelegateHandle& Handle);

/** 按原顺序移除本动作保存的状态标签委托，再清空映射；不修改标签本身。 */
MHGZ_API void UnbindStateTagDelegates(UAbilitySystemComponent* ASC,
	TMap<FGameplayTag, FDelegateHandle>& Handles);

/** 仅执行原地面探测和模式选择；调用方先确认旧动作可交还CMC且未被后继接管。 */
MHGZ_API void RestoreMovementModeFromFloor(ACharacter& Character,
	UCharacterMovementComponent& Movement);
}
