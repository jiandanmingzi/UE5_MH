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
	/** 最早可操作帧（秒）。 */
	float HandoffTime;
	const TCHAR* Provenance;
};

static const FMontageHandoffRoute Routes[] =
{
	{
		TEXT("DanceVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_WuTa.AM_IG_WuTa"),
		0.316f,
		TEXT("measured: MHR id 160->154->137; 154 cancelled into 137 at min 0.316 s over 13 trials (mode 0.316, 6/13)")
	},
	{
		TEXT("BackVault"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_HouChengGanTiao.AM_IG_HouChengGanTiao"),
		0.783f,
		TEXT("measured: MHR id 146->147->137; 146 fixed at 0.651 s, 147 cancelled into 137 at min 0.133 s (lone low outlier; 0.250 next, 3x) => 0.650+0.133")
	},
	{
		TEXT("BackVaultWhite"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_HouChengGanTiao_W.AM_IG_HouChengGanTiao_W"),
		0.775f,
		TEXT("measured: MHR id 158->159->137; 158 fixed at 0.650 s, 159 cancelled into 137 at min 0.125 s over 18 trials (mode 0.125, 5x) => 0.650+0.125")
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

	const FString Report = FString::Printf(TEXT("{\n  \"routes\": [\n%s\n  ]\n}\n"),
		*FString::Join(Rows, TEXT(",\n")));
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
