// Copyright MHGZ Project. All Rights Reserved.
#include "IGActionConfigTypes.h"
#include "Generated/MHGZVaultCurveTables.h"

TArray<FVaultTuning> MHGZ::IGActionConfig::MakeDefaultVaultTunings()
{
	TArray<FVaultTuning> VaultTunings;
	// 撑杆跳的默认值。前 / 左 / 右六条**不是手抄** —— 直接读
	// `Generated/MHGZVaultCurveTables.h`（由 `Scripts/MHRise/build_vault_curves.py`
	// 从 MHR 实机录制生成），所以「重跑生成器」就足以让配置跟上新录制。
	//
	// 后撑杆跳两条仍是本文件的手写常量：它的实录不在采集目录里、生成器覆盖不到，
	// 而迁移计划明确要求这一步不动它。值是迁移前那两个字段的值，逐字未变。
	auto Add = [&VaultTunings](EDirectionalInput Direction, bool bWhite, float VaultDuration,
		float ArcDuration, float Distance, float ApexHeight)
	{
		FVaultTuning& Tuning = VaultTunings.AddDefaulted_GetRef();
		Tuning.Direction = Direction;
		Tuning.bWhite = bWhite;
		Tuning.VaultDuration = VaultDuration;
		Tuning.ArcDuration = ArcDuration;
		Tuning.Distance = Distance;
		Tuning.ApexHeight = ApexHeight;
	};

	using namespace MHGZ::VaultCurves;

	// ── 后撑杆跳 ────────────────────────────────────────────────────────
	// 无白灯：ArcDuration 1.968 减掉下坠残差 0.2347 = 1.733333。那个残差就是
	// 「动作 id 146+147+143，24 次采样」里 143 那一段的实测 **0.234 s**。
	Add(EDirectionalInput::Back, false, 1.733333f, 1.968f, 583.87f, 579.7f);
	// 白灯：VaultDuration 取**飞行时长**而不是 clip 长度。曾用 2.233333
	// (= 0.650 + 1.5833，两条 clip 自然长度之和)，那是被 `HandoffProgress <= 1.0`
	// 的守卫逼出来的值，不是 MHR 说的值：159 的自然长度约 1.596 s，但众数只有
	// 1.486 s —— **它是被触地砍断的**。拿 clip 长度当窗口等于把 110 ms 本该不存在的
	// 飞行补出来，实录路径被拉伸 4.7%（实测白灯时长 +3.4%、水平位移 +2.1%）。
	//
	// 白灯**没有独立的下坠段**（159 一直覆盖到落地），所以 ArcDuration 与
	// VaultDuration 相等、HandoffProgress 恰为 1.0 —— 那是合法的「无自由落体窗口」，
	// 不是配置错误，不要再为过守卫凑数。
	Add(EDirectionalInput::Back, true, 2.137f, 2.137f, 706.05f, 748.9f);

	// ── 前 / 左 / 右六条：四个标量全部来自生成表 ──────────────────────────
	auto AddGenerated = [&Add](EDirectionalInput Direction, bool bWhite, const FMeta& Meta)
	{
		Add(Direction, bWhite, Meta.VaultDuration, Meta.ArcDuration,
			Meta.Distance, Meta.Apex);
	};
	AddGenerated(EDirectionalInput::Forward, false, Forward_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Left, false, Left_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Right, false, Right_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Forward, true, Forward_White_Meta);
	AddGenerated(EDirectionalInput::Left, true, Left_White_Meta);
	AddGenerated(EDirectionalInput::Right, true, Right_White_Meta);
	return VaultTunings;
}

float MHGZ::IGActionConfig::DanceDamageMultiplier(const TArray<float>& Multipliers, int32 Stacks, int32 MaxStacks)
{
    const int32 Index = FMath::Clamp(Stacks, 0, FMath::Max(0, MaxStacks));
    if (Index == 0 || !Multipliers.IsValidIndex(Index)) return 1.0f;
    const float Value = Multipliers[Index];
    return FMath::IsFinite(Value) && Value >= 1.0f ? Value : 1.0f;
}
