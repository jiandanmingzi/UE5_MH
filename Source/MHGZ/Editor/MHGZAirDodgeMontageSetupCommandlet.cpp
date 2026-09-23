// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZAirDodgeMontageSetupCommandlet.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::AirDodgeMontage
{
const TCHAR* SequencePath =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Dash_Air.AS_UnSh_Dash_Air");
const TCHAR* MontageDirectory = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage");
const TCHAR* MontageName = TEXT("AM_IG_AirDodge");
const TCHAR* SlotName = TEXT("DefaultSlot");

/** 与其它动作蒙太奇同一组常量（入场 0 / 出场 0.05）。 */
constexpr float BlendInTime = 0.0f;
constexpr float BlendOutTime = 0.05f;

/** 烘完的长度必须与目标时长一致；超出这个容差就是段参数写错了。 */
constexpr float LengthTolerance = 0.01f;

/**
 * 与 UMHGZPoleVaultAbility::RequiredSegmentPlayRate 同一份契约：让 `DesiredDuration`
 * 秒墙钟正好播完整条剪辑的段速率。
 */
float RequiredSegmentPlayRate(UAnimSequenceBase& Sequence, const float DesiredDuration)
{
	if (!FMath::IsFinite(DesiredDuration) || DesiredDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale)
		|| FMath::IsNearlyZero(Sequence.RateScale))
	{
		return 0.0f;
	}
	return Sequence.GetPlayLength() / (DesiredDuration * FMath::Abs(Sequence.RateScale));
}

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
}

int32 UMHGZAirDodgeMontageSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::AirDodgeMontage;
	(void)Params;

	UAnimSequenceBase* Sequence = LoadObject<UAnimSequenceBase>(nullptr, SequencePath);
	if (!Sequence || !Sequence->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("[AirDodgeMontage] Missing sequence or skeleton: %s"),
			SequencePath);
		return 1;
	}

	// 目标时长 = 剪辑自己的有效长度（见头文件：这条剪辑按 120 fps 栅格烘成有效 142 帧，
	// 与 MHR 实测的 142 帧同栅格）。结果是段速率恒 1.0 —— 本命令列因此**不是**为了
	// 重定时，而是为了让蒙太奇本身常驻、可挂点通知、并能被只读审计。
	const float DesiredDuration = Sequence->GetPlayLength() / FMath::Abs(Sequence->RateScale);
	const float PlayRate = RequiredSegmentPlayRate(*Sequence, DesiredDuration);
	if (PlayRate <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirDodgeMontage] Cannot derive a play rate from clip length %.4f / rate %.4f."),
			Sequence->GetPlayLength(), Sequence->RateScale);
		return 1;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), MontageDirectory, MontageName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, MontageName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Montage = NewObject<UAnimMontage>(Package, FName(MontageName), RF_Public | RF_Standalone);
	}
	if (!Montage)
	{
		UE_LOG(LogTemp, Error, TEXT("[AirDodgeMontage] Could not create %s."), *ObjectPath);
		return 1;
	}

	Montage->Modify();
	Montage->SetSkeleton(Sequence->GetSkeleton());
	Montage->BlendIn.SetBlendTime(BlendInTime);
	Montage->BlendOut.SetBlendTime(BlendOutTime);

	// 从零重建 ⇒ 重跑是幂等的而不是叠加的。
	// ⚠ `Notifies` **刻意不清**：点通知由 MHGZAerialHandoffSetup 写、且它自己先删后加；
	// 这里清掉的话，「先烘再写通知」的顺序就变成必须每次重跑两条命令列。撑杆跳的弧段
	// 那一半也是这么处理的（只有起手段那一半清 Notifies）。
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	SlotTrack.SlotName = FName(SlotName);
	FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Sequence, true);
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Sequence->GetPlayLength();
	Segment.AnimPlayRate = PlayRate;
	Segment.LoopingCount = 1;

	FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = FName(TEXT("Default"));
	Section.Link(Sequence, Sequence->GetPlayLength());
	Section.SetTime(0.0f);

	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();

	// `GetPlayLength()` 对刚重建的蒙太奇恒返回 0（它读的是缓存路径），所以长度必须从
	// **段参数**反算 —— 正着用同一条公式再算一遍会是同义反复，反算才有信息量。
	const FAnimSegment& Baked = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
	const float SegmentSpanSeconds = (Baked.AnimEndTime - Baked.AnimStartTime)
		* FMath::Max(Baked.LoopingCount, 1);
	const float ReadBackLength = Baked.AnimPlayRate > KINDA_SMALL_NUMBER
		? SegmentSpanSeconds / (Baked.AnimPlayRate * FMath::Abs(Sequence->RateScale))
		: 0.0f;
	const bool bSingleSegment = Montage->SlotAnimTracks.Num() == 1
		&& Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1;
	const bool bSingleSection = Montage->CompositeSections.Num() == 1;
	const bool bLengthOk = FMath::IsNearlyEqual(ReadBackLength, DesiredDuration,
		LengthTolerance);
	// 本招的关键不变量：不短于窗口的姿势长度会让动作尾巴被砍。这里两者相等，所以
	// 判据是「一致」。同时把点通知的存在性记进报告（它决定最早可操作帧）。
	int32 HandoffNotifies = 0;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		if (Notify.Notify && Notify.Notify->GetClass()->GetName().Contains(
			TEXT("IG_AerialHandoff")))
		{
			++HandoffNotifies;
		}
	}

	const bool bSaved = SaveAsset(*Montage);

	const FString Report = FString::Printf(
		TEXT("{\n")
		TEXT("  \"montage\": \"%s\",\n")
		TEXT("  \"sequence\": \"%s\",\n")
		TEXT("  \"clip_play_length\": %.4f,\n")
		TEXT("  \"clip_rate_scale\": %.4f,\n")
		TEXT("  \"desired_duration\": %.5f,\n")
		TEXT("  \"baked_segment_play_rate\": %.4f,\n")
		TEXT("  \"readback_length\": %.5f,\n")
		TEXT("  \"readback_within_tolerance\": %s,\n")
		TEXT("  \"single_segment\": %s,\n")
		TEXT("  \"single_section\": %s,\n")
		TEXT("  \"handoff_notifies\": %d,\n")
		TEXT("  \"saved\": %s\n")
		TEXT("}\n"),
		*JsonEscape(ObjectPath), *JsonEscape(Sequence->GetPathName()),
		Sequence->GetPlayLength(), Sequence->RateScale, DesiredDuration, PlayRate,
		ReadBackLength, bLengthOk ? TEXT("true") : TEXT("false"),
		bSingleSegment ? TEXT("true") : TEXT("false"),
		bSingleSection ? TEXT("true") : TEXT("false"),
		HandoffNotifies, bSaved ? TEXT("true") : TEXT("false"));

	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("_air_dodge_montage.json");
	FFileHelper::SaveStringToFile(Report, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogTemp, Display,
		TEXT("[AirDodgeMontage] rate=%.4f readback=%.5f single=%d/%d notifies=%d saved=%s"),
		PlayRate, ReadBackLength, bSingleSegment ? 1 : 0, bSingleSection ? 1 : 0,
		HandoffNotifies, bSaved ? TEXT("true") : TEXT("false"));

	return (bSaved && bSingleSegment && bSingleSection && bLengthOk) ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[AirDodgeMontage] This commandlet requires an editor build."));
	return 1;
#endif
}

UMHGZAirDodgeMontageSetupCommandlet::UMHGZAirDodgeMontageSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}
