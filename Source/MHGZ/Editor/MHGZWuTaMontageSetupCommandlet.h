// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZWuTaMontageSetupCommandlet.generated.h"

/**
 * 由 AS_Unsh_TuJinHuiXuanWuTa 生成常驻的 AM_IG_WuTa 单段蒙太奇。
 *
 * 存在的理由不是"混合"（交叉淡化由 UAnimInstance 按 slot 的 stop-group 完成，
 * 与资产类型无关），而是**速率**：UE 5.6 的
 * UAnimMontage::CreateSlotAnimationAsDynamicMontage 声明了 InPlayRate 却从不引用它
 * （AnimMontage.cpp:3093-3154），段速率恒为 FAnimSegment::AnimPlayRate 的默认值
 * 1.0。于是运行时就地建的蒙太奇**无法重定时**，DanceVaultDuration 一旦复测就会
 * 与姿势脱钩，而看起来能补救的 DanceVaultAnimationPlayRate 什么也不做。
 *
 * 本命令链把 DanceVaultDuration 换算成真实的段速率烘焙进资产；配置与资产因此
 * 在构造上一致，而不是靠"两个数恰好相等"。
 *
 * 运行：
 *     UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=MHGZWuTaMontageSetup \
 *         -unattended -nop4 -nosplash -stdout
 *
 * 与脚本型探针同样把结果写进 Saved/_wuta_montage.json —— commandlet 会吞掉
 * 全部 print/UE_LOG 输出。
 */
UCLASS()
class MHGZ_API UMHGZWuTaMontageSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZWuTaMontageSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
