// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ActionSystem/MHGZActionDirectionCorrectionTypes.h"
#include "AnimNotify_ActionDirectionCorrection.generated.h"

/** Exact active-attack notify that samples a direction and applies a capped yaw correction. */
UCLASS(BlueprintType, meta = (DisplayName = "Action Direction Correction"))
class MHGZ_API UAnimNotify_ActionDirectionCorrection : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** -1 使用攻击 GA 的 MaxCorrectionAngle；0 禁用。 */
	UPROPERTY(EditAnywhere, Category = "Direction Correction",
		meta = (ClampMin = "-1.0", ClampMax = "180.0", UIMin = "-1.0", UIMax = "180.0",
		ToolTip = "-1 使用攻击 GA 的 MaxCorrectionAngle；0 禁用。"))
	float MaxCorrectionAngleOverride = -1.f;

	/** 0 保持旧行为（在 Notify 帧瞬间转到修正目标）；正值开启时间插值。 */
	UPROPERTY(EditAnywhere, Category = "Direction Correction",
		meta = (ClampMin = "0.0", ForceUnits = "s",
		ToolTip = "0 表示瞬时修正；正值表示插值时长。最大角速度可能使实际时长延长。"))
	float CorrectionDuration = 0.0f;

	/** 0 表示不限速；正值限速仅在 CorrectionDuration > 0 时生效。 */
	UPROPERTY(EditAnywhere, Category = "Direction Correction",
		meta = (ClampMin = "0.0", ForceUnits = "deg/s",
		ToolTip = "0 表示不限速。CorrectionDuration 大于 0 时，正值是角速度上限；若曲线无法满足该上限，会自动延长实际时长。"))
	float MaxYawSpeedDegreesPerSecond = 0.0f;

	/** 转向角在 CorrectionDuration 中的归一化进度曲线。 */
	UPROPERTY(EditAnywhere, Category = "Direction Correction")
	EActionDirectionCorrectionEasing Easing = EActionDirectionCorrectionEasing::Linear;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
