// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPoleVaultMontageSetupCommandlet.h"

#include "ActionSystem/MHGZBackVaultAbility.h"
#include "ActionSystem/MHGZForwardVaultAbility.h"
#include "ActionSystem/MHGZLeftVaultAbility.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "ActionSystem/MHGZRightVaultAbility.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::PoleVaultMontage
{
const TCHAR* MontageDirectory = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage");
const TCHAR* SlotName = TEXT("DefaultSlot");

/**
 * 段间过渡。与旧的运行时兜底构建器（`BuildBackVaultMontage`）取同一个值：
 * 它是**已经在跑的**后撑杆跳行为，新六条照抄它才不会在接缝处看出差别。
 */
constexpr float BlendInTime = 3.0f / 60.0f;
constexpr float BlendOutTime = 0.05f;

/**
 * 段名。起手段那条蒙太奇叫 `Jump`、弧段那条叫 `JumpOver` —— 各是一条单段蒙太奇，
 * 名字只用于在编辑器里认出它是哪一半。
 *
 * ⚠ **运行时不再靠段名找边界**。拆成两条蒙太奇之后，`"Jump"`/`"JumpOver"` 不再是
 * 同一条资产里的两个 `FCompositeSection`，交棒由
 * `UMHGZPoleVaultAbility::BeginJumpOverPhase` 在 C++ 里按 `JumpDuration` 触发。
 * 名字留下来是为了可读性，**不是**契约。
 */
const TCHAR* JumpSectionName = TEXT("Jump");
const TCHAR* JumpOverSectionName = TEXT("JumpOver");

/**
 * 弧段蒙太奇的名字后缀。**起手段沿用现有资产名**（`AM_IG_QianChengGanTiao` 等），
 * 因为 GA 蓝图与 `DA_IG_Combo` 都按名字引用它们；弧段是新增的，带后缀最省事。
 */
const TCHAR* ArcMontageSuffix = TEXT("_Over");

/** 起手段实测跑满 78 帧（全部观测）⇒ 78 / 119.8 = 0.651 s。profile 取不到时的兜底。 */
constexpr float JUMP_DURATION_FALLBACK = 0.650f;

/** 段边界容差。段速率是浮点换算来的，`RefreshCacheData` 之后有末位误差。 */
constexpr float SectionTolerance = 0.002f;
/** 蒙太奇总长与期望时长的容差。 */
constexpr float LengthTolerance = 0.01f;

/** 一条撑杆跳被拆成两半：起手段（Jump）与弧段（JumpOver）。 */
enum class EPhase : uint8
{
	Takeoff,
	Arc,
};

/** 与能力里的 `RequiredSegmentPlayRate` 同一条契约：让 `DesiredDuration` 秒走完整条 clip。 */
float RequiredSegmentPlayRate(UAnimSequenceBase& Sequence, const float DesiredDuration)
{
	if (!FMath::IsFinite(DesiredDuration) || DesiredDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale) || FMath::IsNearlyZero(Sequence.RateScale))
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

/**
 * 从段的**存储值**回读蒙太奇时长。
 *
 * **不要用 `UAnimMontage::GetPlayLength()`**：段是刚 `AddDefaulted_GetRef` 出来的、
 * 走的是缓存路径，它在这里恒返回 0。这一条踩过 —— 六条路由全报 `length=0.0000`，
 * 看起来像「段没建出来」，实际是读数的方式错了。
 *
 * 回读 `AnimPlayRate` / `AnimEndTime` 才是**有信息量**的检查：它验证换算后的速率
 * 真的让每一段在时间轴上占 `DesiredDuration` 秒。用同一个公式正着算一遍是
 * 同义反复，回读不是。
 */
float ReadBackLength(const UAnimMontage& Montage)
{
	float Total = 0.0f;
	for (const FSlotAnimationTrack& Slot : Montage.SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
		{
			const UAnimSequenceBase* Sequence = Segment.GetAnimReference();
			const float Scale = Sequence ? FMath::Abs(Sequence->RateScale) : 1.0f;
			const float Rate = Segment.AnimPlayRate;
			if (Rate <= KINDA_SMALL_NUMBER || Scale <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Total += (Segment.AnimEndTime - Segment.AnimStartTime) / (Rate * Scale);
		}
	}
	return Total;
}

/** 一条撑杆跳变体。**两个产物**（起手段 / 弧段）由 `Phase` 选。 */
struct FRoute
{
	const TCHAR* BaseName;
	UClass* AbilityClass;
	EDirectionalInput Direction;
	bool bWhite;
};

FString MontageNameFor(const FRoute& Route, const EPhase Phase)
{
	return Phase == EPhase::Arc
		? FString(Route.BaseName) + ArcMontageSuffix
		: FString(Route.BaseName);
}

const TCHAR* SectionNameFor(const EPhase Phase)
{
	return Phase == EPhase::Arc ? JumpOverSectionName : JumpSectionName;
}

/**
 * 烘**一条**蒙太奇（起手段或弧段）。返回非空字符串即为**失败原因**。
 *
 * 拆成两条之后每条都是**单段单节**。理由见 `MHGZPoleVaultAbility.h` 的类注释：
 * `FAnimTrack::GetAnimationPose` 用 `GetSegmentAtTime` 只取一个段、而
 * `ValidateSegmentTimes` 又把段的 `StartPos` 重新首尾相接 —— 蒙太奇**内部**
 * 段与段之间永远是硬切，连手工做重叠都不可能。要一次混合只能跨蒙太奇，
 * 靠 `UAnimInstance::Montage_PlayInternal` 的 `StopAllMontagesByGroupName`
 * 拿入场 blend-in 去淡出旧的那条。
 *
 * 幂等：`SlotAnimTracks` 与 `CompositeSections` 先整体清空再重建。
 *
 * ⚠ **起手段那一半要把 `Notifies` 一起清掉**，弧段那一半**不清** —— 与今天
 * 「重烘不抹点通知」的约定相比这是一处**刻意的例外**：点通知随拆分搬到了弧段
 * 蒙太奇上，起手段再挂着它就成了孤儿 —— 它在 0.783 s 触发，而起手段只有
 * 0.650 s 长，永远等不到，`bAerialHandoffAuthored` 却是真，那条变体的最早
 * 可操作帧就没了。所以起手段清、弧段留。
 */
FString BakePhase(const FRoute& Route, const EPhase Phase,
	const UInsectGlaiveCombatConfig& Config, FString& OutJson)
{
	const FString MontageName = MontageNameFor(Route, Phase);
	const UMHGZPoleVaultAbility* Ability = Route.AbilityClass
		? Cast<UMHGZPoleVaultAbility>(Route.AbilityClass->GetDefaultObject()) : nullptr;
	if (!Ability)
	{
		return TEXT("能力 CDO 取不到");
	}

	// 与运行时同源：profile 给 clip 与 Jump 段时长，config 给移动窗口。
	const FVaultProfile* Profile = nullptr;
	for (const FVaultProfile& Candidate : Ability->VaultProfiles)
	{
		if (Candidate.Direction == Route.Direction && Candidate.bWhite == Route.bWhite)
		{
			Profile = &Candidate;
			break;
		}
	}
	if (!Profile)
	{
		return TEXT("该方向的 VaultProfiles 里没有这一条");
	}
	const FVaultTuning* Tuning = Config.FindVaultTuning(Route.Direction, Route.bWhite);
	if (!Tuning)
	{
		return TEXT("CombatConfig 的 VaultTunings 里没有这一条");
	}

	// 两条蒙太奇共用同一对 clip，只是各取一条 —— 骨架一致性照旧校验。
	UAnimSequenceBase* Jump = Profile->JumpSequence.LoadSynchronous();
	UAnimSequenceBase* JumpOver = Profile->JumpOverSequence.LoadSynchronous();
	if (!Jump || !JumpOver || !Jump->GetSkeleton()
		|| Jump->GetSkeleton() != JumpOver->GetSkeleton())
	{
		return TEXT("起手段/弧段 clip 缺失或骨架不一致");
	}

	const float JumpDuration = Profile->JumpDuration;
	// 弧段长度取 profile 自己的 `JumpOverDuration`，**不**去推
	// `VaultDuration − JumpDuration`。那两个是**不同的量**：后者是**移动窗口**，
	// 前者是**姿势长度**；前/左/右六组恰好相等，后撑杆跳白灯刻意不等（姿势比窗口
	// 长 0.096 s，让落地把它砍断）。详见 `FVaultProfile::JumpOverDuration` 的注释。
	const float ArcDuration = Profile->JumpOverDuration;

	// 姿势**不短于**窗口。短了就意味着移动还在走、姿势已经播完并开始保持终末帧，
	// 那是眼睛看得见的「动作提前结束」。允许长（后撑杆跳白灯就是长的，落地把它
	// 砍断，这是 MHR 的行为），不允许短。
	if (Phase == EPhase::Arc && ArcDuration < Tuning->VaultDuration - JumpDuration - LengthTolerance)
	{
		return FString::Printf(
			TEXT("弧段姿势 %.4f s 短于移动窗口 %.4f s（VaultDuration %.4f − JumpDuration %.4f）"
				 "：姿势会在窗口结束前播完并被保持。"),
			ArcDuration, Tuning->VaultDuration - JumpDuration,
			Tuning->VaultDuration, JumpDuration);
	}
	UAnimSequenceBase* Clip = Phase == EPhase::Arc ? JumpOver : Jump;
	const float DesiredDuration = Phase == EPhase::Arc ? ArcDuration : JumpDuration;
	const float Rate = RequiredSegmentPlayRate(*Clip, DesiredDuration);
	if (Rate <= 0.0f)
	{
		return FString::Printf(TEXT("%s 段速率推不出来（期望 %.4f s，窗口 %.4f − %.4f）"),
			Phase == EPhase::Arc ? TEXT("弧段") : TEXT("起手段"),
			DesiredDuration, Tuning->VaultDuration, JumpDuration);
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), MontageDirectory, *MontageName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *MontageName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Montage = NewObject<UAnimMontage>(Package, FName(*MontageName),
			RF_Public | RF_Standalone);
	}
	if (!Montage)
	{
		return TEXT("蒙太奇建不出来");
	}

	Montage->Modify();
	Montage->SetSkeleton(Jump->GetSkeleton());
	Montage->BlendIn.SetBlendTime(BlendInTime);
	Montage->BlendOut.SetBlendTime(BlendOutTime);

	// 清空重建 ⇒ 幂等。`Notifies` 的例外规则见本函数的 docstring。
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();
	if (Phase == EPhase::Takeoff)
	{
		Montage->Notifies.Reset();
	}

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	SlotTrack.SlotName = FName(SlotName);

	// 段速率**逐字复用** `RequiredSegmentPlayRate`，不写死 1.0：clip 的导入参数
	// 未核实，写死会让段时长与 `JumpDuration` 错位。
	FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Clip, true);
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Clip->GetPlayLength();
	Segment.AnimPlayRate = Rate;
	Segment.LoopingCount = 1;

	FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = FName(SectionNameFor(Phase));
	Section.Link(Montage, 0.0f, 0);

	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();

	// ── 保存**之前**校验，不让一条坏资产落盘 ──────────────────────────────
	const float Length = ReadBackLength(*Montage);
	const int32 NumSections = Montage->CompositeSections.Num();
	const int32 NumSegments = Montage->SlotAnimTracks.Num() > 0
		? Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() : 0;
	const float SectionStart = NumSections > 0 ? Montage->CompositeSections[0].GetTime() : -1.0f;
	const bool bLengthOk = FMath::IsNearlyEqual(Length, DesiredDuration, LengthTolerance);
	const bool bShapeOk = NumSections == 1 && NumSegments == 1
		&& FMath::IsNearlyZero(SectionStart, SectionTolerance);
	if (!bLengthOk || !bShapeOk)
	{
		return FString::Printf(
			TEXT("自检不过：length=%.4f（期望 %.4f±%.3f）sections=%d segments=%d 首节起点=%.4f"),
			Length, DesiredDuration, LengthTolerance, NumSections, NumSegments, SectionStart);
	}

	const bool bSaved = SaveAsset(*Montage);

	OutJson = FString::Printf(
		TEXT("    {\n")
		TEXT("      \"montage\": \"%s\",\n")
		TEXT("      \"phase\": \"%s\",\n")
		TEXT("      \"direction\": %d,\n")
		TEXT("      \"white\": %s,\n")
		TEXT("      \"clip\": \"%s\",\n")
		TEXT("      \"desired_duration\": %.6f,\n")
		TEXT("      \"jump_duration\": %.6f,\n")
		TEXT("      \"vault_duration\": %.6f,\n")
		TEXT("      \"baked_rate\": %.6f,\n")
		TEXT("      \"resulting_effective_duration\": %.6f,\n")
		TEXT("      \"section\": \"%s\",\n")
		TEXT("      \"sections\": %d,\n")
		TEXT("      \"segments\": %d,\n")
		TEXT("      \"notifies\": %d,\n")
		TEXT("      \"saved\": %s\n")
		TEXT("    }"),
		*JsonEscape(ObjectPath),
		Phase == EPhase::Arc ? TEXT("arc") : TEXT("takeoff"),
		static_cast<int32>(Route.Direction),
		Route.bWhite ? TEXT("true") : TEXT("false"),
		*JsonEscape(Clip->GetPathName()),
		DesiredDuration, JumpDuration, Tuning->VaultDuration,
		Rate, Length, SectionNameFor(Phase),
		NumSections, NumSegments, Montage->Notifies.Num(),
		bSaved ? TEXT("true") : TEXT("false"));

	return bSaved ? FString() : TEXT("落盘失败");
}

/**
 * 存盘**之后**再取回来读一遍，把分节与通知的读数写进报告。
 *
 * ⚠ **长度必须用 `ReadBackLength`（走段），不能用 `GetPlayLength()`。** 这一条在
 * 这个文件里已经是第二次踩：拆分之后每条蒙太奇都是**新建**的，而
 * `GetPlayLength()` 在刚建出来、没进过缓存路径的蒙太奇上恒返回 0；更阴的是对
 * **刚烘过的既有**资产它返回的是**上一次**的长度（内存里那个对象还没刷新），
 * 于是十六条回读全报错、而烘出来的东西全是对的 —— 一个纯读数错误长得像十六个
 * 烘焙失败。
 *
 * ⚠ **本步骤不是一次独立的「从盘上读」**：`LoadObject` 拿回来的是**内存里那个**
 * 对象（包还没卸），所以段的形状在这里其实与烘的时候是同一份数据。它真正证明的是
 * 「`RefreshCacheData` 之后这些字段还在、分节名与通知没丢」，**不是**「它们被序列化
 * 了」。分节与通知没有暴露给 Python 探针，所以这仍是能拿到它们的最直接的一步，
 * 但别把它说成盘上证据。
 */
FString ReadbackPhase(const FRoute& Route, const EPhase Phase, const float ExpectedDuration)
{
	const FString MontageName = MontageNameFor(Route, Phase);
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"),
		MontageDirectory, *MontageName, *MontageName);
	const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		return FString::Printf(TEXT("    { \"montage\": \"%s\", \"error\": \"读不回来\" }"),
			*MontageName);
	}

	TArray<FString> SectionTimes;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		SectionTimes.Add(FString::Printf(TEXT("{\"name\": \"%s\", \"time\": %.6f}"),
			*Section.SectionName.ToString(), Section.GetTime()));
	}
	TArray<FString> NotifyTimes;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		NotifyTimes.Add(FString::Printf(TEXT("%.6f"), Notify.GetTriggerTime()));
	}

	const float Length = ReadBackLength(*Montage);
	const bool bLengthOk = FMath::IsNearlyEqual(Length, ExpectedDuration, LengthTolerance);
	const bool bShapeOk = Montage->CompositeSections.Num() == 1
		&& FMath::IsNearlyZero(Montage->CompositeSections[0].GetTime(), SectionTolerance);
	if (!bLengthOk || !bShapeOk)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PoleVaultMontage] 回读不过 %s：length=%.4f（期望 %.4f）sections=%d"),
			*MontageName, Length, ExpectedDuration, Montage->CompositeSections.Num());
	}

	return FString::Printf(
		TEXT("    {\n")
		TEXT("      \"montage\": \"%s\",\n")
		TEXT("      \"phase\": \"%s\",\n")
		TEXT("      \"expected_duration\": %.6f,\n")
		TEXT("      \"play_length\": %.6f,\n")
		TEXT("      \"length_ok\": %s,\n")
		TEXT("      \"shape_ok\": %s,\n")
		TEXT("      \"sections\": [%s],\n")
		TEXT("      \"notify_times\": [%s]\n")
		TEXT("    }"),
		*MontageName,
		Phase == EPhase::Arc ? TEXT("arc") : TEXT("takeoff"),
		ExpectedDuration, Length,
		bLengthOk ? TEXT("true") : TEXT("false"),
		bShapeOk ? TEXT("true") : TEXT("false"),
		*FString::Join(SectionTimes, TEXT(", ")),
		*FString::Join(NotifyTimes, TEXT(", ")));
}
} // namespace UE::MHGZ::PoleVaultMontage

UMHGZPoleVaultMontageSetupCommandlet::UMHGZPoleVaultMontageSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPoleVaultMontageSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::PoleVaultMontage;
	(void)Params;

	const UInsectGlaiveCombatConfig* Config = GetDefault<UInsectGlaiveCombatConfig>();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("[PoleVaultMontage] CombatConfig CDO 取不到。"));
		return 1;
	}

	// 路由表在函数内建：`StaticClass()` 不该出现在命名空间作用域的静态初始化里。
	//
	// **八条**（四向 × 两灯态），每条产出**两个**蒙太奇。上一版只有六条、把后撑杆跳
	// 那两条当「legacy 只读」，因为那时它们是两段式单蒙太奇、由手工资产承载；
	// 拆成双蒙太奇之后它也必须走同一条烘焙路径，否则能力里要长期留两条代码路径。
	const FRoute Routes[] = {
		{ TEXT("AM_IG_QianChengGanTiao"),   UMHGZForwardVaultAbility::StaticClass(),
		  EDirectionalInput::Forward, false },
		{ TEXT("AM_IG_QianChengGanTiao_W"), UMHGZForwardVaultAbility::StaticClass(),
		  EDirectionalInput::Forward, true },
		{ TEXT("AM_IG_ZuoChengGanTiao"),    UMHGZLeftVaultAbility::StaticClass(),
		  EDirectionalInput::Left, false },
		{ TEXT("AM_IG_ZuoChengGanTiao_W"),  UMHGZLeftVaultAbility::StaticClass(),
		  EDirectionalInput::Left, true },
		{ TEXT("AM_IG_YouChengGanTiao"),    UMHGZRightVaultAbility::StaticClass(),
		  EDirectionalInput::Right, false },
		{ TEXT("AM_IG_YouChengGanTiao_W"),  UMHGZRightVaultAbility::StaticClass(),
		  EDirectionalInput::Right, true },
		{ TEXT("AM_IG_HouChengGanTiao"),    UMHGZBackVaultAbility::StaticClass(),
		  EDirectionalInput::Back, false },
		{ TEXT("AM_IG_HouChengGanTiao_W"),  UMHGZBackVaultAbility::StaticClass(),
		  EDirectionalInput::Back, true },
	};
	const EPhase Phases[] = { EPhase::Takeoff, EPhase::Arc };

	// 两半时长**都**来自 profile：`JumpDuration` 给起手段、`JumpOverDuration` 给弧段。
	// 弧段**不能**用 `VaultDuration − JumpDuration` 推 —— 见烘焙里那段注释。
	auto DurationsFor = [](const FRoute& Route, float& OutJump, float& OutArc)
	{
		const UMHGZPoleVaultAbility* Ability = Route.AbilityClass
			? Cast<UMHGZPoleVaultAbility>(Route.AbilityClass->GetDefaultObject()) : nullptr;
		OutJump = JUMP_DURATION_FALLBACK;
		OutArc = -1.0f;
		if (Ability)
		{
			for (const FVaultProfile& Profile : Ability->VaultProfiles)
			{
				if (Profile.Direction == Route.Direction && Profile.bWhite == Route.bWhite)
				{
					OutJump = Profile.JumpDuration;
					OutArc = Profile.JumpOverDuration;
					break;
				}
			}
		}
	};

	TArray<FString> Entries;
	TArray<FString> Failures;
	for (const FRoute& Route : Routes)
	{
		for (const EPhase Phase : Phases)
		{
			FString Json;
			const FString Error = BakePhase(Route, Phase, *Config, Json);
			const FString Name = MontageNameFor(Route, Phase);
			if (Error.IsEmpty())
			{
				Entries.Add(Json);
				UE_LOG(LogTemp, Display, TEXT("[PoleVaultMontage] 烘好 %s"), *Name);
			}
			else
			{
				Failures.Add(FString::Printf(TEXT("{\"montage\": \"%s\", \"error\": \"%s\"}"),
					*Name, *JsonEscape(Error)));
				UE_LOG(LogTemp, Error, TEXT("[PoleVaultMontage] %s 失败：%s"), *Name, *Error);
			}
		}
	}

	// 存盘后回读：分节与通知的证据必须来自盘上那条资产。
	TArray<FString> Readbacks;
	for (const FRoute& Route : Routes)
	{
		float Jump = 0.0f, Arc = 0.0f;
		DurationsFor(Route, Jump, Arc);
		Readbacks.Add(ReadbackPhase(Route, EPhase::Takeoff, Jump));
		Readbacks.Add(ReadbackPhase(Route, EPhase::Arc, Arc));
	}

	const FString Report = FString::Printf(
		TEXT("{\n  \"routes\": %d,\n  \"montages_expected\": %d,\n  \"baked\": %d,\n  \"failed\": %d,\n")
		TEXT("  \"baked_montages\": [\n%s\n  ],\n")
		TEXT("  \"failures\": [%s],\n")
		TEXT("  \"readback\": [\n%s\n  ]\n}\n"),
		UE_ARRAY_COUNT(Routes), UE_ARRAY_COUNT(Routes) * UE_ARRAY_COUNT(Phases),
		Entries.Num(), Failures.Num(),
		*FString::Join(Entries, TEXT(",\n")),
		*FString::Join(Failures, TEXT(", ")),
		*FString::Join(Readbacks, TEXT(",\n")));

	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("_pole_vault_montage.json");
	FFileHelper::SaveStringToFile(Report, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	return Failures.IsEmpty() ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[PoleVaultMontage] 需要 editor 构建。"));
	return 1;
#endif
}
