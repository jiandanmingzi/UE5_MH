// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZStripPresentationRootMotionCommandlet.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{

// 表现 clip 锚点（归属见 docs/reference/资产序号对照.md）：
//   AS_Unsh_Fall_Jump     = 143 起跳下坠（AM_IG_AerialFall）
//   AS_Unsh_Fall_W_Jump   = 157 回避后下坠（AM_IG_AerialFall_W，空回与白灯共用）
//   AS_Unsh_FallDown_Jump = 148 起跳落地（AM_IG_AerialLanding，用户重烘 0.5833 s）
// 实际剥离面 = 锚点 ∪ 下列蒙太奇段引用的全部 UAnimSequence（空回本体的源 clip
// 也在其中 —— 测试按段遍历，2026-09-23 首跑就是它漏网红的）。
const TCHAR* SequencePaths[] = {
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_Jump.AS_Unsh_Fall_Jump"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_W_Jump.AS_Unsh_Fall_W_Jump"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_FallDown_Jump.AS_Unsh_FallDown_Jump"),
};

// 表现蒙太奇 + 空回本体（其位移 100% 由 BallisticVault 源驱动，动画根同样无用）。
const TCHAR* MontagePaths[] = {
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialFall.AM_IG_AerialFall"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialFall_W.AM_IG_AerialFall_W"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"),
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

} // namespace

int32 UMHGZStripPresentationRootMotionCommandlet::Main(const FString& Params)
{
	(void)Params;
	FString Report;
	int32 Changed = 0;
	int32 Failed = 0;

	auto AppendSequence = [&Report](const TCHAR* Path, const UAnimSequence* Seq,
		const bool bSaved, const TCHAR* Action)
	{
		Report += FString::Printf(
			TEXT("%s{\"path\": \"%s\", \"action\": \"%s\", \"enable_root_motion\": %s, ")
			TEXT("\"force_root_lock\": %s, \"copied_from_montage\": %s, \"saved\": %s}"),
			Report.IsEmpty() ? TEXT("") : TEXT(", "),
			*JsonEscape(Path), Action,
			Seq->bEnableRootMotion ? TEXT("true") : TEXT("false"),
			Seq->bForceRootLock ? TEXT("true") : TEXT("false"),
			Seq->bRootMotionSettingsCopiedFromMontage ? TEXT("true") : TEXT("false"),
			bSaved ? TEXT("true") : TEXT("false"));
	};

	FString SequenceReport;
	TArray<UAnimSequence*> Targets;
	TSet<FName> Seen;
	auto AddTarget = [&Targets, &Seen](UAnimSequence* Seq)
	{
		if (Seq && !Seen.Contains(Seq->GetFName()))
		{
			Seen.Add(Seq->GetFName());
			Targets.Add(Seq);
		}
	};
	for (const TCHAR* Path : SequencePaths)
	{
		AddTarget(LoadObject<UAnimSequence>(nullptr, Path));
	}
	for (const TCHAR* Path : MontagePaths)
	{
		const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!Montage)
		{
			continue;
		}
		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				AddTarget(Cast<UAnimSequence>(Segment.GetAnimReference().Get()));
			}
		}
	}
	for (UAnimSequence* Seq : Targets)
	{
		const TCHAR* Path = *Seq->GetPathName();
		const bool bAlreadyClean = !Seq->bEnableRootMotion && Seq->bForceRootLock
			&& Seq->bRootMotionSettingsCopiedFromMontage;
		Seq->bEnableRootMotion = false;
		Seq->bForceRootLock = true;
		Seq->bRootMotionSettingsCopiedFromMontage = true;
		const bool bSaved = SaveAsset(*Seq);
		if (!bSaved || Seq->bEnableRootMotion || !Seq->bForceRootLock
			|| !Seq->bRootMotionSettingsCopiedFromMontage)
		{
			UE_LOG(LogTemp, Error, TEXT("[StripPresentationRootMotion] failed to pin %s"), Path);
			++Failed;
		}
		if (!bAlreadyClean)
		{
			++Changed;
		}
		AppendSequence(Path, Seq, bSaved, bAlreadyClean ? TEXT("noop") : TEXT("stripped"));
	}
	SequenceReport = Report;
	Report.Reset();

	auto AppendMontage = [&Report](const TCHAR* Path, const UAnimMontage* Montage,
		const bool bSaved, const TCHAR* Action)
	{
		Report += FString::Printf(
			TEXT("%s{\"path\": \"%s\", \"action\": \"%s\", \"translation\": %s, ")
			TEXT("\"rotation\": %s, \"has_root_motion\": %s, \"saved\": %s}"),
			Report.IsEmpty() ? TEXT("") : TEXT(", "),
			*JsonEscape(Path), Action,
			Montage->bEnableRootMotionTranslation ? TEXT("true") : TEXT("false"),
			Montage->bEnableRootMotionRotation ? TEXT("true") : TEXT("false"),
			Montage->HasRootMotion() ? TEXT("true") : TEXT("false"),
			bSaved ? TEXT("true") : TEXT("false"));
	};

	FString MontageReport;
	for (const TCHAR* Path : MontagePaths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!Montage)
		{
			UE_LOG(LogTemp, Error, TEXT("[StripPresentationRootMotion] missing montage %s"), Path);
			++Failed;
			continue;
		}
		const bool bAlreadyClean = !Montage->bEnableRootMotionTranslation
			&& !Montage->bEnableRootMotionRotation;
		Montage->bEnableRootMotionTranslation = false;
		Montage->bEnableRootMotionRotation = false;
		const bool bSaved = SaveAsset(*Montage);
		if (!bSaved || Montage->bEnableRootMotionTranslation
			|| Montage->bEnableRootMotionRotation)
		{
			UE_LOG(LogTemp, Error, TEXT("[StripPresentationRootMotion] failed to pin %s"), Path);
			++Failed;
		}
		// 抽取总闸门的最终读数：源 clip 全关之后必须为 false。
		if (Montage->HasRootMotion())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[StripPresentationRootMotion] %s still reports HasRootMotion"), Path);
			++Failed;
		}
		if (!bAlreadyClean)
		{
			++Changed;
		}
		AppendMontage(Path, Montage, bSaved, bAlreadyClean ? TEXT("noop") : TEXT("stripped"));
	}
	MontageReport = Report;

	const FString Json = FString::Printf(
		TEXT("{\n  \"changed\": %d,\n  \"failed\": %d,\n  \"sequences\": [%s],\n  \"montages\": [%s]\n}\n"),
		Changed, Failed, *SequenceReport, *MontageReport);
	const FString OutPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("_strip_root_motion_report.json"));
	FFileHelper::SaveStringToFile(Json, *OutPath);

	UE_LOG(LogTemp, Display,
		TEXT("[StripPresentationRootMotion] changed=%d failed=%d report=%s"),
		Changed, Failed, *OutPath);
	return Failed == 0 ? 0 : 1;
}
