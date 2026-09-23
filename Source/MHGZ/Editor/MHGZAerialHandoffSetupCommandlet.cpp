// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZAerialHandoffSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_IG_AerialHandoff.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimationBlueprintLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::AerialHandoff
{
/** 点通知写在自己的轨道上，和蒙太奇原有的通知互不干扰。 */
const FName NotifyTrackName(TEXT("IGAerialHandoff"));

/** 重读校验的时间容差（秒）。 */
constexpr float TimeTolerance = 0.002f;

/** 旧 NotifyState 的类名与 NotifyName —— 用字符串匹配，见头文件说明。 */
const TCHAR* RetiredStateClassName = TEXT("AnimNotifyState_IG_AerialWindow");
const TCHAR* RetiredNotifyName = TEXT("IGAerialWindow");

struct FMontageHandoffRoute
{
	const TCHAR* Label;
	const TCHAR* MontagePath;
	/** 最早可操作帧，**在本条蒙太奇自己的时间轴上**（秒）。 */
	float HandoffTime;
	const TCHAR* Provenance;
};

/**
 * 起手段那一半的长度（秒）。撑杆跳拆成双蒙太奇之后，全部实测墙都从「整条链的
 * 绝对时间」变成「弧段蒙太奇自己的时间」—— 减掉这个数即可。
 *
 * 与 `UMHGZPoleVaultAbility` 各 profile 的 `JumpDuration`（四向八条全是 0.650）
 * 和烘焙命令列的 `JUMP_DURATION_FALLBACK` 是同一个数。
 */
constexpr float VaultTakeoffDuration = 0.650f;

static const FMontageHandoffRoute Routes[] =
{
	{
		TEXT("DanceVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_WuTa.AM_IG_WuTa"),
		0.316f,
		TEXT("measured: MHR id 160->154->137; 154 cancelled into 137 at min 0.316 s over 13 trials (mode 0.316, 6/13)")
	},
	// ── 撑杆跳八条：通知挂在**弧段蒙太奇**（`_Over`）上，时间是弧段局部时间 ────
	//
	// 整条链的墙都是**帧号**：起跳 78 帧 + 弧段 16 帧 = 离地链第 94 帧，
	// 而整条链的绝对蒙太奇时间 = 0.650 + 16/119.8 = 0.7836 ⇒ 项目里一直写作 0.783。
	// 拆成两条蒙太奇之后，弧段那条的**零点是原来的 0.650**，所以墙上移
	// `0.783 − 0.650 = 0.133`。后撑杆跳白灯同理：`0.775 − 0.650 = 0.125`。
	//
	// ⚠ 起手段那一半（`AM_IG_*ChengGanTiao`，**不带** `_Over`）**不能**挂这条通知：
	// 它只有 0.650 s 长，而通知在 0.783 s 触发，永远等不到 —— 可
	// `bAerialHandoffAuthored` 却是真，那条变体的最早可操作帧就没了。
	// 清掉它的是烘焙命令列（见那里的 docstring），所以顺序是**先烘再写通知**；
	// 本命令列末尾还有一条只读断言，把顺序搞反的情形喊出来。
	{
		TEXT("BackVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_HouChengGanTiao_Over.AM_IG_HouChengGanTiao_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 146->147->137; 146 fixed at 0.651 s, 147 cancelled into 137 at min 0.133 s (lone low outlier; 0.250 next, 3x) => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("BackVaultWhite"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_HouChengGanTiao_W_Over.AM_IG_HouChengGanTiao_W_Over"),
		0.775f - VaultTakeoffDuration,
		TEXT("measured: MHR id 158->159->137; 158 fixed at 0.650 s, 159 cancelled into 137 at min 0.125 s over 18 trials (mode 0.125, 5x) => chain 0.775; arc montage starts at 0.650 => 0.125")
	},
	// 前 / 左 / 右 × 无 / 有白灯：**同一个 0.133**，因为三向共用同一条弧段
	// （无白灯 142 / 白灯 156），六组的墙落在同一帧。白灯·向右那组观测里出现过
	// 更晚的一档（95 帧），但文档说 95 只是**上界**，取它会把可操作帧推后一帧。
	{
		TEXT("ForwardVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_QianChengGanTiao_Over.AM_IG_QianChengGanTiao_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 141->142->137; earliest cancel chain frame 94 = jump 78 + arc 16, 0.650 + 16/119.8 = 0.7836 => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("ForwardVaultWhite"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_QianChengGanTiao_W_Over.AM_IG_QianChengGanTiao_W_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 155->156->137; earliest cancel chain frame 94 = jump 78 + arc 16, 0.650 + 16/119.8 = 0.7836 => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("LeftVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_ZuoChengGanTiao_Over.AM_IG_ZuoChengGanTiao_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 144->142->137; earliest cancel chain frame 94 = jump 78 + arc 16, 0.650 + 16/119.8 = 0.7836 => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("LeftVaultWhite"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_ZuoChengGanTiao_W_Over.AM_IG_ZuoChengGanTiao_W_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 144->156->137 (white reuses the normal takeoff clip); earliest cancel chain frame 94, 0.650 + 16/119.8 = 0.7836 => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("RightVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_YouChengGanTiao_Over.AM_IG_YouChengGanTiao_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 145->142->137; earliest cancel chain frame 94 = jump 78 + arc 16, 0.650 + 16/119.8 = 0.7836 => chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	{
		TEXT("RightVaultWhite"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_YouChengGanTiao_W_Over.AM_IG_YouChengGanTiao_W_Over"),
		0.783f - VaultTakeoffDuration,
		TEXT("measured: MHR id 145->156->137; earliest cancel chain frame 94. The 95-frame mode is an upper bound, not the wall, so it is deliberately not used. chain 0.783; arc montage starts at 0.650 => 0.133")
	},
	// ── 空中回避（MHR id 137）：它**自己就是一条完整的弧**，没有起手段那一半 ─────
	//
	// 0.650 s 不是从别处减出来的，它就在这条蒙太奇自己的时间轴上：真值里 `137` 自身的
	// 最早可操作帧是 **78 / 142 帧**（全库 0 段更低、16 段精确命中、三个不同后继都能接上），
	// 而剪辑按 120 fps 栅格烘成有效 142 帧 / 1.18333 s ⇒ 78/120 = **0.6500**。
	//
	// 它也不参与上面的「起手段不许挂通知」只读断言（那条按 `ChengGanTiao_Over` 命中筛选，
	// 本路由天然跳过）。
	{
		TEXT("AirDodge"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"),
		0.650f,
		TEXT("measured: MHR id 137 own earliest actionable frame = 78 of 142 (min over the whole library with no segment lower, 16 exact hits, three distinct successors) => 78/120 = 0.6500 on the clip's 120 fps grid")
	},
};

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

FString JsonEscape(const FString& Value)
{
	return Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}

/** 幂等清理：删掉退役的 NotifyState，以及本命令列上一次写的点通知。 */
int32 RemoveExistingNotifies(UAnimMontage& Montage)
{
	int32 Removed = 0;
	for (int32 Index = Montage.Notifies.Num() - 1; Index >= 0; --Index)
	{
		const FAnimNotifyEvent& Event = Montage.Notifies[Index];
		const bool bRetiredState = (Event.NotifyStateClass
				&& Event.NotifyStateClass->GetClass()->GetFName() == RetiredStateClassName)
			|| Event.NotifyName == RetiredNotifyName;
		const bool bPreviousHandoff =
			Cast<UAnimNotify_IG_AerialHandoff>(Event.Notify) != nullptr;
		if (bRetiredState || bPreviousHandoff)
		{
			Montage.Notifies.RemoveAt(Index);
			++Removed;
		}
	}
	return Removed;
}

/**
 * 重读校验。命令列的输出会被 pythonscript 吞掉，所以判据必须落在报告里 ——
 * 而「写完之后资产里到底有什么」只有重读才算知道。
 */
bool ValidateMontage(const FMontageHandoffRoute& Route, const UAnimMontage& Montage,
	float& OutTriggerTime, int32& OutHandoffCount, int32& OutRetiredLeft)
{
	OutTriggerTime = -1.0f;
	OutHandoffCount = 0;
	OutRetiredLeft = 0;
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		if (Cast<UAnimNotify_IG_AerialHandoff>(Event.Notify))
		{
			++OutHandoffCount;
			OutTriggerTime = Event.GetTriggerTime();
		}
		const bool bRetiredState = (Event.NotifyStateClass
				&& Event.NotifyStateClass->GetClass()->GetFName() == RetiredStateClassName)
			|| Event.NotifyName == RetiredNotifyName;
		if (bRetiredState)
		{
			++OutRetiredLeft;
		}
	}
	return OutHandoffCount == 1
		&& OutRetiredLeft == 0
		&& FMath::IsNearlyEqual(OutTriggerTime, Route.HandoffTime, TimeTolerance);
}
}

#endif // WITH_EDITOR

int32 UMHGZAerialHandoffSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::AerialHandoff;
	(void)Params;

	TArray<FString> Rows;
	bool bAllOk = true;
	for (const FMontageHandoffRoute& Route : Routes)
	{
		FString Row = FString::Printf(TEXT("  {\"label\": \"%s\", \"montage\": \"%s\", "),
			*JsonEscape(Route.Label), *JsonEscape(Route.MontagePath));

		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Route.MontagePath);
		if (!Montage)
		{
			Rows.Add(Row + TEXT("\"error\": \"not found\"}"));
			bAllOk = false;
			continue;
		}

		const float Length = Montage->GetPlayLength();
		const int32 NotifiesBefore = Montage->Notifies.Num();
		const float HandoffTime = FMath::Clamp(Route.HandoffTime, 0.0f,
			FMath::Max(0.0f, Length - TimeTolerance));

		Montage->Modify();
		const int32 Removed = RemoveExistingNotifies(*Montage);

		if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Montage, NotifyTrackName))
		{
			UAnimationBlueprintLibrary::AddAnimationNotifyTrack(
				Montage, NotifyTrackName, FLinearColor::Yellow);
		}
		const bool bWrote = Cast<UAnimNotify_IG_AerialHandoff>(
			UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, NotifyTrackName,
				HandoffTime, UAnimNotify_IG_AerialHandoff::StaticClass())) != nullptr;
		Montage->RefreshCacheData();

		// 重读校验 —— 必须在 SaveAsset 之前，否则保存的是一个没验证过的资产。
		float TriggerTime = -1.0f;
		int32 HandoffCount = 0;
		int32 RetiredLeft = 0;
		const bool bValidated = bWrote
			&& ValidateMontage(Route, *Montage, TriggerTime, HandoffCount, RetiredLeft);
		const bool bSaved = SaveAsset(*Montage);
		bAllOk &= bValidated && bSaved;

		Rows.Add(FString::Printf(
			TEXT("%s\"montage_length\": %.4f, \"handoff_time\": %.4f, ")
			TEXT("\"trigger_time_readback\": %.4f, \"handoff_count\": %d, ")
			TEXT("\"stale_window_removed\": %d, \"retired_left\": %d, ")
			TEXT("\"notifies_before\": %d, \"notifies_after\": %d, ")
			TEXT("\"provenance\": \"%s\", \"validated\": %s, \"saved\": %s}"),
			*Row, Length, HandoffTime, TriggerTime, HandoffCount, Removed, RetiredLeft,
			NotifiesBefore, Montage->Notifies.Num(),
			*JsonEscape(Route.Provenance),
			bValidated ? TEXT("true") : TEXT("false"),
			bSaved ? TEXT("true") : TEXT("false")));
	}

	// ── 顺序断言：起手段那一半**不能**挂着这条通知 ────────────────────────────
	//
	// 通知随拆分泌到了弧段蒙太奇上；而起手段只有 0.650 s 长、墙却在 0.783 s
	// （整条链的绝对时间），它永远等不到 —— 可「这条蒙太奇有没有点通知」的判据
	// 仍然为真，于是那条变体的最早可操作帧<b>消失</b>，且只在 PIE 里表现为
	// 「能出招但没有可取消窗口」，很难归因。
	//
	// 清掉它的是**烘焙命令列**（烘起手段时会把 `Notifies` 清空，见那里的 docstring），
	// 所以顺序必须是「先烘再写通知」。这里**只读检查、不修** —— 由本命令列代劳会
	// 把「烘焙根本没重跑」这件事盖住，而那正是要喊出来的。
	auto HasHandoffNotify = [](const UAnimMontage& Montage)
	{
		for (const FAnimNotifyEvent& Notify : Montage.Notifies)
		{
			if (Notify.Notify && Notify.Notify->IsA(UAnimNotify_IG_AerialHandoff::StaticClass()))
			{
				return true;
			}
		}
		return false;
	};

	TArray<FString> Ordering;
	for (const FMontageHandoffRoute& Route : Routes)
	{
		// `_Over` 只出现在资产名里（包名与对象名各一次），整串去掉即可得到起手段路径。
		const FString ArcPath(Route.MontagePath);
		if (!ArcPath.Contains(TEXT("ChengGanTiao_Over")))
		{
			continue;  // 舞踏不是撑杆跳，没有「另一半」
		}
		const FString TakeoffPath = ArcPath.Replace(TEXT("_Over"), TEXT(""));
		const UAnimMontage* Takeoff = LoadObject<UAnimMontage>(nullptr, *TakeoffPath);
		if (!Takeoff)
		{
			Ordering.Add(FString::Printf(
				TEXT("  {\"label\": \"%s\", \"takeoff\": \"%s\", \"found\": false, \"stale_notify\": false}"),
				*JsonEscape(Route.Label), *JsonEscape(TakeoffPath)));
			continue;
		}
		const bool bStale = HasHandoffNotify(*Takeoff);
		Ordering.Add(FString::Printf(
			TEXT("  {\"label\": \"%s\", \"takeoff\": \"%s\", \"found\": true, \"stale_notify\": %s}"),
			*JsonEscape(Route.Label), *JsonEscape(TakeoffPath),
			bStale ? TEXT("true") : TEXT("false")));
		if (bStale)
		{
			bAllOk = false;
			UE_LOG(LogTemp, Error,
				TEXT("[AerialHandoff] %s 的起手段 %s 上还挂着 IGAerialHandoff 点通知 —— ")
				TEXT("蒙太奇拆成两半之后它落在 0.783 s，而起手段只有 0.650 s 长。")
				TEXT("先重跑 MHGZPoleVaultMontageSetup（它会清掉起手段的 Notifies）再来写通知。"),
				Route.Label, *TakeoffPath);
		}
	}

	const FString Report = FString::Printf(
		TEXT("{\n  \"routes\": [\n%s\n  ],\n  \"takeoff_ordering\": [\n%s\n  ]\n}\n"),
		*FString::Join(Rows, TEXT(",\n")),
		*FString::Join(Ordering, TEXT(",\n")));
	FFileHelper::SaveStringToFile(Report,
		*(FPaths::ProjectSavedDir() / TEXT("_aerial_handoff.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	return bAllOk ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[AerialHandoff] This commandlet requires an editor build."));
	return 1;
#endif
}

UMHGZAerialHandoffSetupCommandlet::UMHGZAerialHandoffSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}
