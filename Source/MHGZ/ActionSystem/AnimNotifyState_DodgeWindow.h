// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_DodgeWindow.generated.h"

/**
 * UAnimNotifyState_DodgeWindow — 翻滚无敌帧窗口
 * NotifyBegin → 玩家 Weapon/MonsterAttack 通道 = Ignore + Invincible Tag
 * NotifyEnd   → 恢复
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dodge Window"))
class UAnimNotifyState_DodgeWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
