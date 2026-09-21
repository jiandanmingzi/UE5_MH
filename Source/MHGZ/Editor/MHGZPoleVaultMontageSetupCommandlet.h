// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZPoleVaultMontageSetupCommandlet.generated.h"

/**
 * 由实录 clip 烘出**十六条**撑杆跳蒙太奇：四向（前 / 左 / 右 / 后）× 两灯态
 * × **两半**（起手段 / 弧段）。
 *
 * **为什么是两半。** 一条蒙太奇内部的段与段之间**永远是硬切**：
 * `FAnimTrack::GetAnimationPose` 用 `GetSegmentAtTime` 只取一个段，而
 * `ValidateSegmentTimes` 又把段的 `StartPos` 重新首尾相接 —— 连手工做重叠窗口
 * 都不可能。于是「起手段末帧姿势」紧接「弧段首帧姿势」，接缝处会看到可见的姿势
 * 突跳（实测白灯左/右是正常帧落差的 4.0×/4.3×）。要一次混合只能**跨蒙太奇**：
 * `UAnimInstance::Montage_PlayInternal` 在 `bStopAllMontages` 为真时会调
 * `StopAllMontagesByGroupName(Group, BlendInSettings)`，拿入场蒙太奇的 blend-in
 * 设置把旧那条**按同样时长淡出**，那就是一次真正的交叉淡入。
 *
 * **命名**：起手段**沿用现有资产名**（`AM_IG_QianChengGanTiao` 等）—— GA 蓝图与
 * `DA_IG_Combo` 都按名字引用它们；弧段是新增的，带 `_Over` 后缀。
 *
 * **为什么不就地建**：与 `UMHGZWuTaMontageSetupCommandlet` 同一条理由 ——
 * UE 5.6 的 `UAnimMontage::CreateSlotAnimationAsDynamicMontage` 声明了 `InPlayRate`
 * 却从不引用它，段速率恒为 `FAnimSegment::AnimPlayRate` 的默认值 1.0。就地建的
 * 蒙太奇无法重定时，于是「移动窗口」与「姿势长度」只能靠两个数恰好相等。
 * 烘进资产才能把窗口换算成真实的段速率。
 *
 * **为什么不并进 WuTa 那个**：那个是单资产、报告字段写死；拧成多路由会毁掉既有
 * 报告与产物的对应关系，而它自己的验收还没走完。
 *
 * **数值来源**：本命令列**不另抄一份数据**，而是读
 * `UMHGZPoleVaultAbility` 各方向子类的 CDO（`VaultProfiles`）与
 * `UInsectGlaiveCombatConfig` 的 CDO（`VaultTunings`）—— 与运行时同一份来源。
 * 于是「烘出来的蒙太奇」与「能力实际用的窗口」不可能对不上。
 *
 * ⚠ **后撑杆跳那两条这次也烘**（上一版只做只读校验）。拆成双蒙太奇之后它必须走
 * 同一条路径，否则能力里要长期留两套代码。
 *
 * ⚠ **点通知不在本命令列写**，但它会把**起手段**那一半的 `Notifies` 清空 ——
 * 通知随拆分搬到了弧段蒙太奇上，起手段再挂着它就是孤儿（0.783 s 触发而它只有
 * 0.650 s 长，永远等不到，可 `bAerialHandoffAuthored` 却是真，那条变体的最早可
 * 操作帧就没了）。顺序：**先跑本命令列，再跑 `MHGZAerialHandoffSetup`**。
 *
 * 运行：
 *     UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=MHGZPoleVaultMontageSetup \
 *         -unattended -nop4 -nosplash -stdout
 *
 * 报告写 `Saved/_pole_vault_montage.json` —— commandlet 会吞掉全部 UE_LOG。
 */
UCLASS()
class MHGZ_API UMHGZPoleVaultMontageSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPoleVaultMontageSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
