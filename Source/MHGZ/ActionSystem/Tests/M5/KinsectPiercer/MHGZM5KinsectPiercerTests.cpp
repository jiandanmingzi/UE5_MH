// Copyright MHGZ Project. All Rights Reserved.
//
// 强化操虫穿刺（M5 本招）的 M2 专项：运行时合同；落地 fixture 只读导入动画，复制到 transient。
//
// 覆盖四块：
//   1. 录制局部轨迹模型 —— 校验、按秒采样、覆盖外两种尾段策略、局部→世界基。
//   2. CMC 的录制轨迹租约 —— 逐物理子步恰好施加 Curve(t1)−Curve(t0)；保护飞行段
//      跑完不落回重力；地面段只施加水平位移；半空的"地面相位"被拒。
//   3. 激活级「每段每目标一次」额度 —— 三段独立、窗口重开不退款。
//   4. 舞踏来源的 Resource 标签镜像与入招闸门 —— 真值仍是 EIGDanceSource，
//      标签只是广播；闸门按 §9.2 的表逐行翻。
//
// 这里**不**碰桩上判定、TraceRegion 与 MotionValue：那三项由 E1 在真实资产上摆。

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZRecordedLocalTrajectory.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZCharacter.h"
#include "../../Shared/MHGZM3TestTypes.h"
#include "../../Shared/MHGZM3TestHarness.h"
#include "MHGZM5KinsectPiercerTestTypes.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
/** 显式时间表的三分量线性键曲线；形状与 M3 生成器将写出的资产一致。 */
UCurveVector* MakeTimedTrajectoryCurve(const TArray<float>& Times, const TArray<FVector>& Points)
{
	UCurveVector* Curve = NewObject<UCurveVector>(GetTransientPackage());
	if (!Curve || Times.Num() != Points.Num() || Times.Num() < 2)
	{
		return Curve;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			FRichCurveKey& Key = Curve->FloatCurves[Axis].Keys.AddDefaulted_GetRef();
			Key.Time = Times[Index];
			Key.InterpMode = RCIM_Linear;
			Key.TangentMode = RCTM_Auto;
			Key.TangentWeightMode = RCTWM_WeightedNone;
			Key.Value = Points[Index][Axis];
		}
	}
	return Curve;
}

/**
 * 测试用的标准 profile：0.5 s 覆盖，前向 300 cm、上向 −100 cm，
 * 终末速度 (400, 0, −200) cm/s、终末加速度 (100, 0, −1800) cm/s²。
 */
FRecordedLocalTrajectoryProfile MakeTestProfile()
{
	FRecordedLocalTrajectoryProfile Profile;
	Profile.Curve = MakeTimedTrajectoryCurve(
		{ 0.0f, 0.5f },
		{ FVector(0.0, 0.0, 0.0), FVector(300.0, 0.0, -100.0) });
	Profile.CoveredSeconds = 0.5f;
	Profile.TerminalVelocityLocal = FVector(400.0, 0.0, -200.0);
	Profile.TerminalAccelerationLocal = FVector(100.0, 0.0, -1800.0);
	return Profile;
}

FWeaponMovementRequest MakeTestTrajectoryRequest()
{
	FWeaponMovementRequest Request;
	Request.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.RecordedTrajectory = MakeTestProfile();
	Request.Duration = Request.RecordedTrajectory.GetDuration();
	return Request;
}

/** 一块够大的地板；顶面落在 `TopZ`。 */
AActor* SpawnTestFloor(UWorld& World, const float TopZ)
{
	AActor* Floor = World.SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!Floor)
	{
		return nullptr;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(4000.0f, 4000.0f, 50.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Floor->SetActorLocation(FVector(0.0f, 0.0f, TopZ - 50.0f));
	return Floor;
}

/** 一堵竖墙；前表面落在 `FrontX`（法线朝 -X）。 */
AActor* SpawnTestWall(UWorld& World, const float FrontX)
{
	AActor* Wall = World.SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator);
	if (!Wall)
	{
		return nullptr;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(50.0f, 2000.0f, 1000.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(FrontX + 50.0f, 0.0f, 0.0f));
	return Wall;
}

/** 位移租约测试的最小世界：带 instrumented CMC 的角色 + Host + 一个已注册的 Action。 */
struct FPiercerMovementHarness
{
	UWorld* World = nullptr;
	AMHGZM3TestCharacter* Character = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZInstrumentedCharacterMovementComponent* CMC = nullptr;
	FWeaponActionToken Action;
	/** 建不起来时停在哪一步 —— 断言里直接带出来，免得只看到一句 false。 */
	FString FailureStage;

	bool Build(const bool bWithFloor, const bool bOnFloor,
		const TOptional<float> WallFrontX = TOptional<float>())
	{
		const auto Fail = [this](const TCHAR* Stage)
		{
			FailureStage = Stage;
			return false;
		};

		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return Fail(TEXT("world"));
		}
		// 不经过 PlayerState：本组只测 CMC 租约，不需要 GAS 的完整授予链。
		Character = World->SpawnActor<AMHGZM3TestCharacter>();
		if (!Character)
		{
			return Fail(TEXT("character"));
		}
		CMC = Cast<UMHGZInstrumentedCharacterMovementComponent>(
			Character->GetCharacterMovement());
		if (!CMC)
		{
			return Fail(TEXT("instrumented CMC"));
		}
		if (bWithFloor && !SpawnTestFloor(*World, -10.0f))
		{
			return Fail(TEXT("floor"));
		}
		if (WallFrontX.IsSet() && !SpawnTestWall(*World, WallFrontX.GetValue()))
		{
			return Fail(TEXT("wall"));
		}
		Character->SetActorLocation(FVector(0.0f, 0.0f,
			Character->GetDefaultHalfHeight() + 10.0f));

		ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
		if (!ASC)
		{
			return Fail(TEXT("ASC"));
		}
		Character->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Character, Character);
		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		if (!Host)
		{
			return Fail(TEXT("Host"));
		}
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);

		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(
			FGameplayAbilitySpec(UMHGZKinsectPiercerAbility::StaticClass(), 1, INDEX_NONE, ASC));
		if (!Handle.IsValid())
		{
			return Fail(TEXT("give ability"));
		}
		Action.RuntimeToken = Host->GetCurrentToken();
		Action.AbilityHandle = Handle;
		Action.ActivationSequenceID = Host->AllocateActivationSequenceID();
		Action.AbilityInstance = NewObject<UMHGZKinsectPiercerAbility>(ASC);
		if (!Action.AbilityInstance.IsValid())
		{
			return Fail(TEXT("ability instance"));
		}
		if (!Host->RegisterAction(Action))
		{
			return Fail(TEXT("register action"));
		}
		FName WarpTarget;
		if (!Host->AcquireActionMovement(Action, WarpTarget))
		{
			return Fail(TEXT("acquire movement"));
		}
		CMC->SetMovementMode(bOnFloor ? MOVE_Walking : MOVE_Flying);
		return true;
	}

	void Teardown()
	{
		if (Host)
		{
			Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		}
		if (World)
		{
			World->DestroyWorld(false);
		}
		World = nullptr;
		Character = nullptr;
		ASC = nullptr;
		Host = nullptr;
		CMC = nullptr;
	}
};

/** 主 Montage 的 Section 名 fixture；落地门禁另需真实动画片段。 */
UAnimMontage* MakeSectionMontage(const TArray<FName>& SectionNames)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	for (const FName& SectionName : SectionNames)
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = SectionName;
	}
	return Montage;
}

/** Keep the imported Over pose data; change root-motion settings only on its transient copy. */
UAnimMontage* MakeRootMotionLandingMontage(const bool bEnableRootMotion = true)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongChuanCi_Over.AS_Unsh_CaoChongChuanCi_Over"));
	if (!Source || !Source->GetSkeleton() || Source->GetPlayLength() <= 0.0f)
	{
		return nullptr;
	}
	UAnimSequence* Copy = DuplicateObject<UAnimSequence>(Source, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(),
			TEXT("M5PiercerLandingSequence")));
	if (!Copy)
	{
		return nullptr;
	}
	Copy->ClearFlags(RF_Public | RF_Standalone);
	Copy->SetFlags(RF_Transient);
	Copy->bEnableRootMotion = bEnableRootMotion;
	Copy->bForceRootLock = true;
	Copy->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;

	UAnimMontage* Montage = MakeSectionMontage({ TEXT("Landing203") });
	Montage->SetFlags(RF_Transient);
	Montage->SetSkeleton(Copy->GetSkeleton());
	Montage->CompositeSections[0].NextSectionName = NAME_None;
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = TEXT("DefaultSlot");
	FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Copy, true);
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Copy->GetPlayLength();
	Segment.AnimPlayRate = 1.0f;
	Segment.LoopingCount = 1;
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	return Montage;
}

/**
 * 把 GA 配到「只差来源/姿态这一层」的程度：两条 Montage、两条空中曲线、三段武器判定、
 * RTY 输入标签。这样闸门的每一条否定才都是**真的**在测那一条，而不是被别的缺失挡住。
 *
 * 探针把静态 fixture 配置套到每次激活的新实例上，不改 CDO 或导入动画资产。
 */
bool ConfigureGateAbility()
{
	UMHGZM5KinsectPiercerGateProbeAbility::TestMainMontage = MakeSectionMontage(
		{ TEXT("Start200"), TEXT("Dive201"), TEXT("DiveLoop201") });
	UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage =
		MakeRootMotionLandingMontage();
	UMHGZM5KinsectPiercerGateProbeAbility::bTestConfigureAttackSegments = true;
	return UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage != nullptr;
}

/** 走一次**真实**的激活链，回读探针：闸门是否放行、是否走到了建资产那一步。 */
void TryGate(const FMHGZM3Harness& H, const FGameplayAbilitySpecHandle& Handle,
	bool& bOutAccepted, bool& bOutReached)
{
	UMHGZM5KinsectPiercerGateProbeAbility::ResetProbeForTest();

	FWeaponInputSnapshot Input;
	Input.ResolvedInputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTY"));
	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = H.Host->GetCurrentToken();
	Context.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
	Context.Input = Input;
	H.ASC->PrepareWeaponAbilityActivation(Handle, Context);
	H.ASC->TryActivateAbility(Handle);

	bOutAccepted = UMHGZM5KinsectPiercerGateProbeAbility::bLastDependenciesAccepted;
	bOutReached = UMHGZM5KinsectPiercerGateProbeAbility::bLastReachedPrepareAttackMontage;
	// 没到预检就说明早一步就被拒了：断言里带出来，免得只看到一句 false。
	if (!UMHGZM5KinsectPiercerGateProbeAbility::bLastDependenciesCalled)
	{
		bOutAccepted = false;
		bOutReached = false;
	}
}
}


// ══════════════════════════════════════════════════════════════════════
// 1. 录制局部轨迹模型
// ══════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerTrajectoryModelTest,
	"MHGZ.M5.KinsectPiercer.RecordedTrajectoryModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerTrajectoryModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FRecordedLocalTrajectoryProfile Profile = MakeTestProfile();
	TestTrue(TEXT("the audited profile shape validates"), Profile.IsValid());
	TestEqual(TEXT("duration is the covered span, not the last key"),
		Profile.GetDuration(), 0.5f);

	// ── 覆盖内的位移与速度 ──
	TestTrue(TEXT("inside coverage the offset is the authored curve value"),
		Profile.GetOffset(0.25f, ERecordedTrajectoryTailPolicy::HoldLastKey)
			.Equals(FVector(150.0, 0.0, -50.0), 0.001));
	TestTrue(TEXT("inside coverage the velocity is the linear segment slope"),
		Profile.GetVelocity(0.25f, ERecordedTrajectoryTailPolicy::HoldLastKey)
			.Equals(FVector(600.0, 0.0, -200.0), 0.001));
	TestTrue(TEXT("the phase starts at the origin"),
		Profile.GetOffset(0.0f, ERecordedTrajectoryTailPolicy::HoldLastKey).IsNearlyZero(0.001));

	// ── 覆盖外：两种尾段策略给出不同的答案 ──
	TestTrue(TEXT("HoldLastKey parks on the last authored value"),
		Profile.GetOffset(0.75f, ERecordedTrajectoryTailPolicy::HoldLastKey)
			.Equals(FVector(300.0, 0.0, -100.0), 0.001));
	TestTrue(TEXT("HoldLastKey reports zero velocity once the path is over"),
		Profile.GetVelocity(0.75f, ERecordedTrajectoryTailPolicy::HoldLastKey)
			.IsNearlyZero(0.001));
	TestTrue(TEXT("IntegrateTerminalModel continues with v*dt + a*dt^2/2"),
		Profile.GetOffset(0.75f, ERecordedTrajectoryTailPolicy::IntegrateTerminalModel)
			.Equals(FVector(403.125, 0.0, -206.25), 0.001));
	TestTrue(TEXT("IntegrateTerminalModel reports v + a*dt"),
		Profile.GetVelocity(0.75f, ERecordedTrajectoryTailPolicy::IntegrateTerminalModel)
			.Equals(FVector(425.0, 0.0, -650.0), 0.001));

	// ── 校验拒绝的形状 ──
	FRecordedLocalTrajectoryProfile NoCurve = Profile;
	NoCurve.Curve = nullptr;
	TestFalse(TEXT("a profile without a curve is invalid"), NoCurve.IsValid());

	FRecordedLocalTrajectoryProfile ZeroCoverage = Profile;
	ZeroCoverage.CoveredSeconds = 0.0f;
	TestFalse(TEXT("zero coverage is invalid"), ZeroCoverage.IsValid());

	FRecordedLocalTrajectoryProfile BeyondKeys = Profile;
	BeyondKeys.CoveredSeconds = 5.0f;
	TestFalse(TEXT("coverage past the last key is rejected: nothing is authored there"),
		BeyondKeys.IsValid());

	// 201 的真实形状：换段帧裁剪摘掉了末尾那个键，于是规范域（0.366547）落在末键
	// （0.358333）之后**恰好一个 1/120 s 网格**。这条边界曾经被写成「覆盖终点必须 ≤ 末键」，
	// 那会把唯一一份规范资产判成非法配置，整招连激活都进不去。
	{
		FRecordedLocalTrajectoryProfile GridTrimmed;
		GridTrimmed.Curve = MakeTimedTrajectoryCurve(
			{ 0.0f, 0.25f, 0.358333f },
			{ FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FVector(300.0, 0.0, -100.0) });
		GridTrimmed.CoveredSeconds = 0.366547f;
		GridTrimmed.TerminalVelocityLocal = FVector(3200.0, -1.3, -4395.0);
		GridTrimmed.TerminalAccelerationLocal = FVector(5400.0, -60.0, -13170.0);
		TestTrue(TEXT("a covered end one 1/120 s grid step past the last key is legal (the 201 shape)"),
			GridTrimmed.IsValid());
		TestTrue(TEXT("the clamped stretch still answers with the last authored value"),
			GridTrimmed.GetOffset(0.362f, ERecordedTrajectoryTailPolicy::HoldLastKey)
				.Equals(FVector(300.0, 0.0, -100.0), 0.001));

		GridTrimmed.CoveredSeconds = 0.358333f + (2.0f / 120.0f);
		TestFalse(TEXT("two grid steps past the last key is no longer rounding: it is a false coverage claim"),
			GridTrimmed.IsValid());
	}

	// 三条轴取**最短**的那条给覆盖终点定界：某条轴提前收尾时，拿最长的那条定界
	// 会把那条轴已经夹住的区间算成「已覆盖」。
	{
		UCurveVector* Ragged = MakeTimedTrajectoryCurve(
			{ 0.0f, 0.5f }, { FVector::ZeroVector, FVector(300.0, 0.0, -100.0) });
		Ragged->FloatCurves[2].Keys.RemoveAt(1);
		FRichCurveKey& ShortZ = Ragged->FloatCurves[2].Keys.AddDefaulted_GetRef();
		ShortZ.Time = 0.25f;
		ShortZ.InterpMode = RCIM_Linear;
		ShortZ.TangentMode = RCTM_Auto;
		ShortZ.TangentWeightMode = RCTWM_WeightedNone;
		ShortZ.Value = 0.0;

		FRecordedLocalTrajectoryProfile RaggedProfile = Profile;
		RaggedProfile.Curve = Ragged;
		TestFalse(TEXT("the covered end is bounded by the shortest axis, not the longest"),
			RaggedProfile.IsValid());
	}

	FRecordedLocalTrajectoryProfile NonFiniteVelocity = Profile;
	NonFiniteVelocity.TerminalVelocityLocal =
		FVector(TNumericLimits<double>::Max() * 2.0, 0.0, 0.0);
	TestFalse(TEXT("a non-finite terminal velocity is rejected"), NonFiniteVelocity.IsValid());

	FRecordedLocalTrajectoryProfile NonFiniteAcceleration = Profile;
	NonFiniteAcceleration.TerminalAccelerationLocal =
		FVector(0.0, TNumericLimits<double>::Max() * 2.0, 0.0);
	TestFalse(TEXT("a non-finite terminal acceleration is rejected"),
		NonFiniteAcceleration.IsValid());

	float LastKey = 0.0f;
	TestFalse(TEXT("curve validation rejects a null curve"),
		MHGZRecordedLocalTrajectory::ValidateCurve(nullptr, LastKey));

	TestFalse(TEXT("a curve whose first key is not at t=0 is rejected"),
		MHGZRecordedLocalTrajectory::ValidateCurve(MakeTimedTrajectoryCurve(
			{ 0.05f, 0.5f }, { FVector::ZeroVector, FVector(300.0, 0.0, -100.0) }),
			LastKey));

	TestFalse(TEXT("a curve that does not start at the origin is rejected"),
		MHGZRecordedLocalTrajectory::ValidateCurve(MakeTimedTrajectoryCurve(
			{ 0.0f, 0.5f }, { FVector(10.0, 0.0, 0.0), FVector(300.0, 0.0, -100.0) }),
			LastKey));

	TestFalse(TEXT("a curve with a repeated key time is rejected"),
		MHGZRecordedLocalTrajectory::ValidateCurve(MakeTimedTrajectoryCurve(
			{ 0.0f, 0.25f, 0.25f, 0.5f },
			{ FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FVector(120.0, 0.0, 0.0),
				FVector(300.0, 0.0, -100.0) }),
			LastKey));

	TestTrue(TEXT("a multi-key curve reports its last key time"),
		MHGZRecordedLocalTrajectory::ValidateCurve(MakeTimedTrajectoryCurve(
			{ 0.0f, 0.25f, 0.5f },
			{ FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FVector(300.0, 0.0, -100.0) }),
			LastKey) && FMath::IsNearlyEqual(LastKey, 0.5f, 0.0001f));

	// ── 局部基 ──
	TestTrue(TEXT("forward +X has right = +Y"),
		MHGZRecordedLocalTrajectory::ResolveRightVector(FVector::ForwardVector)
			.Equals(FVector(0.0, 1.0, 0.0), 0.001));
	TestTrue(TEXT("forward +Y has right = -X"),
		MHGZRecordedLocalTrajectory::ResolveRightVector(FVector::RightVector)
			.Equals(FVector(-1.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("the local frame maps X forward, Y right and Z to world up"),
		MHGZRecordedLocalTrajectory::LocalToWorld(FVector(1.0, 0.0, 0.0),
			FVector::ForwardVector).Equals(FVector(1.0, 0.0, 0.0), 0.001)
		&& MHGZRecordedLocalTrajectory::LocalToWorld(FVector(0.0, 1.0, 0.0),
			FVector::ForwardVector).Equals(FVector(0.0, 1.0, 0.0), 0.001)
		&& MHGZRecordedLocalTrajectory::LocalToWorld(FVector(0.0, 0.0, 1.0),
			FVector::ForwardVector).Equals(FVector(0.0, 0.0, 1.0), 0.001));
	TestTrue(TEXT("a pitched aim direction is flattened before it becomes the local frame"),
		MHGZRecordedLocalTrajectory::LocalToWorld(FVector(1.0, 0.0, 0.0),
			FVector(1.0, 0.0, 1.0).GetSafeNormal())
			.Equals(FVector(1.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("a vertical aim direction cannot produce a horizontal frame"),
		MHGZRecordedLocalTrajectory::ResolveRightVector(FVector::UpVector).IsNearlyZero(0.001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerRequestShapeTest,
	"MHGZ.M5.KinsectPiercer.RecordedTrajectoryRequestShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerRequestShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FWeaponMovementRequest Request = MakeTestTrajectoryRequest();
	TestTrue(TEXT("a well-formed recorded request validates"),
		Request.HasValidRecordedTrajectoryParameters());
	TestEqual(TEXT("the request carries the new movement mode"),
		Request.Mode, EWeaponMovementMode::RecordedLocalTrajectory);
	TestEqual(TEXT("the default ground policy protects the start-up phase"),
		Request.TrajectoryGroundPolicy, ERecordedTrajectoryGroundPolicy::ProtectedFlight);
	TestEqual(TEXT("the default tail policy holds the last key"),
		Request.TrajectoryTailPolicy, ERecordedTrajectoryTailPolicy::HoldLastKey);

	FWeaponMovementRequest NoDirection = Request;
	NoDirection.DirectionSnapshot = FVector::ZeroVector;
	TestFalse(TEXT("a request without a frozen direction is rejected"),
		NoDirection.HasValidRecordedTrajectoryParameters());

	FWeaponMovementRequest NoCurve = Request;
	NoCurve.RecordedTrajectory.Curve = nullptr;
	TestFalse(TEXT("a request without a curve is rejected"),
		NoCurve.HasValidRecordedTrajectoryParameters());

	// 其它模式不受这条校验影响 —— 否则新字段会意外拦住已有动作。
	FWeaponMovementRequest OtherMode = Request;
	OtherMode.Mode = EWeaponMovementMode::SegmentedDirectional;
	OtherMode.RecordedTrajectory.Curve = nullptr;
	TestTrue(TEXT("the recorded-trajectory guard does not leak into other modes"),
		OtherMode.HasValidRecordedTrajectoryParameters());

	// 枚举追加在末尾，已有序列化值不动。
	TestEqual(TEXT("RecordedLocalTrajectory is appended after SegmentedDirectional"),
		static_cast<uint8>(EWeaponMovementMode::RecordedLocalTrajectory),
		static_cast<uint8>(EWeaponMovementMode::SegmentedDirectional) + 1);

	return true;
}

// ══════════════════════════════════════════════════════════════════════
// 2. CMC 的录制轨迹租约
// ══════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerCmcLeaseTest,
	"MHGZ.M5.KinsectPiercer.CmcRecordedTrajectoryLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerCmcLeaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FPiercerMovementHarness H;
	if (!TestTrue(*FString::Printf(TEXT("movement harness builds (%s)"), *H.FailureStage), H.Build(false, false)))
	{
		H.Teardown();
		return false;
	}

	FWeaponMovementRequest Request = MakeTestTrajectoryRequest();
	Request.OwnerAction = H.Action;
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;

	const uint64 Lease = H.CMC->BeginRecordedTrajectoryMovement(Request);
	TestTrue(TEXT("a protected-flight lease is granted"), Lease != 0);
	TestTrue(TEXT("the lease reports itself active"),
		H.CMC->IsRecordedTrajectoryMovementActive(Lease));
	TestFalse(TEXT("a fresh lease is not yet complete"),
		H.CMC->HasRecordedTrajectoryMovementCompleted(Lease));
	TestEqual(TEXT("the protected start-up phase owns the capsule in MOVE_Flying"),
		static_cast<int32>(H.CMC->MovementMode), static_cast<int32>(MOVE_Flying));
	TestTrue(TEXT("the phase starts at the curve's own initial tangent"),
		H.CMC->Velocity.Equals(FVector(600.0, 0.0, -200.0), 1.0));

	// 逐物理子步 = Curve(t1) − Curve(t0)。一步 1/60 s 完全落在曲线的线性段内。
	const FVector StartLocation = H.Character->GetActorLocation();
	H.CMC->PerformMovement(1.0f / 60.0f);
	const FVector StepDelta = H.Character->GetActorLocation() - StartLocation;
	const FVector ExpectedStep = FVector(300.0, 0.0, -100.0) * (1.0f / 60.0f) / 0.5f;
	TestTrue(TEXT("one physics step applies exactly Curve(t1)-Curve(t0)"),
		StepDelta.Equals(ExpectedStep, 0.5));
	TestTrue(TEXT("the step is the authored shape, not a chase toward the endpoint (no vertical catch-up)"),
		FMath::IsNearlyEqual(StepDelta.Z, ExpectedStep.Z, 0.5));

	// 跑满整个覆盖时长：相位完成，但**不**落回重力 —— 胶囊停在 MOVE_Flying。
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
	{
		H.CMC->PerformMovement(1.0f / 60.0f);
	}
	TestTrue(TEXT("the phase reports completion once its authored span is spent"),
		H.CMC->HasRecordedTrajectoryMovementCompleted(Lease));
	TestEqual(TEXT("a completed protected-flight phase does not fall back to gravity"),
		static_cast<int32>(H.CMC->MovementMode), static_cast<int32>(MOVE_Flying));
	TestTrue(TEXT("the completed phase parks on the last authored offset"),
		H.Character->GetActorLocation().Equals(StartLocation + FVector(300.0, 0.0, -100.0), 1.0));

	H.CMC->RetireRecordedTrajectoryMovement(Lease);
	TestFalse(TEXT("retiring the lease clears it"),
		H.CMC->IsRecordedTrajectoryMovementActive(Lease));

	// 反过来：位移所有权不在这个 Action 手上时，同一份请求必须被拒。
	FWeaponMovementRequest Unowned = MakeTestTrajectoryRequest();
	Unowned.OwnerAction = H.Action;
	Unowned.OwnerAction.AbilityInstance = nullptr;
	TestTrue(TEXT("a lease whose action has no live instance is refused"),
		H.CMC->BeginRecordedTrajectoryMovement(Unowned) == 0);
	TestTrue(TEXT("an unowned lease leaves the previous lease untouched"),
		H.CMC->BeginRecordedTrajectoryMovement(Unowned) == 0);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerCmcGroundPolicyTest,
	"MHGZ.M5.KinsectPiercer.CmcGroundPolicyRequiresRealFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerCmcGroundPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 负控：没有地板时，地面相位必须被拒 —— 半空启动一条地面曲线会让整段动作悬空滑行。
	{
		FPiercerMovementHarness Air;
		if (!TestTrue(*FString::Printf(TEXT("airborne harness builds (%s)"), *Air.FailureStage), Air.Build(false, false)))
		{
			Air.Teardown();
			return false;
		}
		FWeaponMovementRequest Request = MakeTestTrajectoryRequest();
		Request.OwnerAction = Air.Action;
		Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::Grounded;
		TestTrue(TEXT("a grounded phase without a walkable floor is refused"),
			Air.CMC->BeginRecordedTrajectoryMovement(Request) == 0);
		Air.Teardown();
	}

	// 正控：站在地板上时，地面相位只施加水平位移，曲线的竖直记录不还原成升降。
	FPiercerMovementHarness Ground;
	if (!TestTrue(*FString::Printf(TEXT("grounded harness builds (%s)"), *Ground.FailureStage), Ground.Build(true, true)))
	{
		Ground.Teardown();
		return false;
	}
	FWeaponMovementRequest Request = MakeTestTrajectoryRequest();
	Request.OwnerAction = Ground.Action;
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::Grounded;

	const uint64 Lease = Ground.CMC->BeginRecordedTrajectoryMovement(Request);
	if (TestTrue(TEXT("a grounded lease is granted on a real floor"), Lease != 0))
	{
		TestEqual(TEXT("the grounded phase keeps the capsule walking"),
			static_cast<int32>(Ground.CMC->MovementMode), static_cast<int32>(MOVE_Walking));
		TestTrue(TEXT("the grounded phase drops the vertical part of the initial tangent"),
			FMath::IsNearlyZero(Ground.CMC->Velocity.Z, 1.0));

		const FVector StartLocation = Ground.Character->GetActorLocation();
		Ground.CMC->PerformMovement(1.0f / 60.0f);
		const FVector Delta = Ground.Character->GetActorLocation() - StartLocation;
		TestTrue(TEXT("the grounded phase advances the authored forward step"),
			FMath::IsNearlyEqual(Delta.X, 10.0, 0.5));
		TestTrue(TEXT("the grounded phase never turns the recorded vertical noise into capsule lift"),
			FMath::IsNearlyEqual(Delta.Z, 0.0, 1.0));

		for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
		{
			Ground.CMC->PerformMovement(1.0f / 60.0f);
		}
		TestTrue(TEXT("the grounded phase completes at the authored span"),
			Ground.CMC->HasRecordedTrajectoryMovementCompleted(Lease));
		TestTrue(TEXT("the grounded phase parks on the authored forward displacement"),
			FMath::IsNearlyEqual(Ground.Character->GetActorLocation().X,
				StartLocation.X + 300.0, 1.0));
	}
	Ground.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerCmcWallSlideTest,
	"MHGZ.M5.KinsectPiercer.CmcWallSlideNeverCatchesUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerCmcWallSlideTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// 墙挡在 authored 前向路径的正中间：整条曲线有 300 cm 前向位移，墙前表面在 120 cm。
	constexpr float WallFrontX = 120.0f;

	FPiercerMovementHarness H;
	if (!TestTrue(*FString::Printf(TEXT("walled harness builds (%s)"), *H.FailureStage),
		H.Build(false, false, WallFrontX)))
	{
		H.Teardown();
		return false;
	}

	FWeaponMovementRequest Request = MakeTestTrajectoryRequest();
	Request.OwnerAction = H.Action;
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;

	const uint64 Lease = H.CMC->BeginRecordedTrajectoryMovement(Request);
	const float StartX = H.Character->GetActorLocation().X;
	const float StartZ = H.Character->GetActorLocation().Z;
	if (!TestTrue(TEXT("the walled lease is granted"), Lease != 0))
	{
		H.Teardown();
		return false;
	}

	// 整段跑完：胶囊应当被墙挡住，而不是穿过去，也不是在下一帧把落后的位移补回来。
	float PreviousX = StartX;
	float MaxStepX = 0.0f;
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
	{
		H.CMC->PerformMovement(1.0f / 60.0f);
		const float CurrentX = H.Character->GetActorLocation().X;
		MaxStepX = FMath::Max(MaxStepX, CurrentX - PreviousX);
		PreviousX = CurrentX;
	}

	const FVector Final = H.Character->GetActorLocation();
	const float CapsuleRadius = H.Character->GetCapsuleComponent()
		? H.Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
	TestTrue(TEXT("the capsule never passes through the wall"),
		Final.X <= WallFrontX - CapsuleRadius + 1.0f);
	TestTrue(TEXT("the authored path really would have crossed the wall"),
		StartX + 300.0f > WallFrontX + CapsuleRadius);
	TestTrue(TEXT("a blocked substep is never repaid as a larger later step"),
		MaxStepX <= 300.0f / 0.5f / 60.0f + 1.0f);
	TestTrue(TEXT("sliding along the wall does not climb it"),
		Final.Z <= StartZ + 1.0f);
	TestTrue(TEXT("the phase clock still runs to its authored end"),
		H.CMC->HasRecordedTrajectoryMovementCompleted(Lease));

	H.Teardown();
	return true;
}

// ══════════════════════════════════════════════════════════════════════
// 3. 激活级「每段每目标一次」
// ══════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerSegmentLedgerTest,
	"MHGZ.M5.KinsectPiercer.SegmentTargetLedgerIsActivationScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerSegmentLedgerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}
	AActor* Dummy = World->SpawnActor<AActor>();
	AActor* Other = World->SpawnActor<AActor>();
	UMHGZKinsectPiercerAbility* Ability =
		NewObject<UMHGZKinsectPiercerAbility>(GetTransientPackage());
	if (!Dummy || !Other || !Ability)
	{
		World->DestroyWorld(false);
		return false;
	}
	FMHGZKM5KinsectPiercerTest::ResetActivation(*Ability);

	const FHitResult DummyHit(Dummy, nullptr, FVector::ZeroVector, FVector::UpVector);
	const FHitResult OtherHit(Other, nullptr, FVector::ZeroVector, FVector::UpVector);

	TestFalse(TEXT("nothing is spent before any successful damage"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0) == false
		&& Ability->HasDamagedTarget(0, Dummy));
	TestTrue(TEXT("an unspent target may be hit"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0));

	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, DummyHit, 0);
	TestTrue(TEXT("the same target is recorded as damaged on segment 0"),
		Ability->HasDamagedTarget(0, Dummy));
	TestFalse(TEXT("the same target is refused on the same segment afterwards"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0));
	// 三段是**独立**额度：允许窗口重叠，也允许同一敌人被三个索引各打一次。
	TestTrue(TEXT("segment 1 keeps its own budget for the same target"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 1));
	TestTrue(TEXT("segment 2 keeps its own budget for the same target"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 2));
	TestTrue(TEXT("a different actor is still allowed on segment 0"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, OtherHit, 0));
	TestFalse(TEXT("the other actor's allowance does not leak into the damaged target"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0));

	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, DummyHit, 1);
	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, DummyHit, 2);
	TestTrue(TEXT("all three segments can each damage the same enemy exactly once"),
		Ability->HasDamagedTarget(0, Dummy)
		&& Ability->HasDamagedTarget(1, Dummy)
		&& Ability->HasDamagedTarget(2, Dummy));

	// 下一段窗口重新打开不会重置额度：这就是激活级额度的全部意义。
	// 201 的动画循环与两条 Montage 同理 —— 那两者都不碰这份集合。
	TestFalse(TEXT("reopening a collision window does not refund the budget"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0));

	FMHGZKM5KinsectPiercerTest::ResetActivation(*Ability);
	TestTrue(TEXT("a new activation starts from an empty ledger"),
		FMHGZKM5KinsectPiercerTest::CanApply(*Ability, DummyHit, 0)
		&& !Ability->HasDamagedTarget(0, Dummy));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerRuntimeTelemetrySnapshotTest,
	"MHGZ.M5.KinsectPiercer.RuntimeTelemetrySnapshotReportsPhaseAndHitQuotas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerRuntimeTelemetrySnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}
	AActor* Segment0Target = World->SpawnActor<AActor>();
	AActor* Segment2TargetA = World->SpawnActor<AActor>();
	AActor* Segment2TargetB = World->SpawnActor<AActor>();
	UMHGZKinsectPiercerAbility* Ability =
		NewObject<UMHGZKinsectPiercerAbility>(GetTransientPackage());
	if (!TestNotNull(TEXT("the activation-scoped test ability exists"), Ability)
		|| !TestNotNull(TEXT("segment zero target exists"), Segment0Target)
		|| !TestNotNull(TEXT("segment two target A exists"), Segment2TargetA)
		|| !TestNotNull(TEXT("segment two target B exists"), Segment2TargetB))
	{
		World->DestroyWorld(false);
		return false;
	}

	FMHGZKM5KinsectPiercerTest::ResetActivation(*Ability);
	Ability->AttackSegments.SetNum(3);
	Ability->SetPhaseForTest(EKinsectPiercerPhase::Dive201);
	Ability->SetLockedYawDirectionForTest(FVector(0.0, 1.0, 0.0));
	const FHitResult Segment0Hit(Segment0Target, nullptr, FVector::ZeroVector, FVector::UpVector);
	const FHitResult Segment2HitA(Segment2TargetA, nullptr, FVector::ZeroVector, FVector::UpVector);
	const FHitResult Segment2HitB(Segment2TargetB, nullptr, FVector::ZeroVector, FVector::UpVector);
	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, Segment0Hit, 0);
	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, Segment2HitA, 2);
	FMHGZKM5KinsectPiercerTest::RecordDamage(*Ability, Segment2HitB, 2);

	FMHGZKinsectPiercerTelemetrySnapshot Snapshot;
	Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("the fixture is not falsely reported as a live production activation"),
		Snapshot.bAbilityActive);
	TestEqual(TEXT("the telemetry phase agrees with the production phase enum"),
		Snapshot.Phase, static_cast<int32>(EKinsectPiercerPhase::Dive201));
	TestTrue(TEXT("the locked horizontal direction is observable without pitch"),
		Snapshot.LockedYawDirection.Equals(FVector::RightVector, 0.001));
	TestEqual(TEXT("the actual AttackSegments count is reported"),
		Snapshot.SegmentConfigCount, 3);
	TestEqual(TEXT("ConfigIndex 0 reports one successful target"),
		Snapshot.SuccessfulTargetCounts[0], 1);
	TestEqual(TEXT("ConfigIndex 1 remains unused"),
		Snapshot.SuccessfulTargetCounts[1], 0);
	TestEqual(TEXT("ConfigIndex 2 reports both distinct successful targets"),
		Snapshot.SuccessfulTargetCounts[2], 2);
	TestEqual(TEXT("the latest successful ConfigIndex is reported"),
		Snapshot.LastSuccessfulConfigIndex, 2);

	World->DestroyWorld(false);
	return true;
}

// ══════════════════════════════════════════════════════════════════════
// 4. 舞踏来源镜像、配置合同与入招闸门
// ══════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerDanceSourceMirrorTest,
	"MHGZ.M5.KinsectPiercer.DanceSourceTagMirror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerDanceSourceMirrorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGameplayTag KinsectSlashSource =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Dance.Source.KinsectSlash"));
	const FGameplayTag AdvancingCounterSource =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Dance.Source.AdvancingCounter"));
	if (!TestTrue(TEXT("both dance source tags are registered"),
		KinsectSlashSource.IsValid() && AdvancingCounterSource.IsValid()))
	{
		return false;
	}

	FMHGZM3Harness H;
	if (!TestTrue(TEXT("M3 resource harness builds"), H.Setup()))
	{
		H.Teardown();
		return false;
	}
	H.CombatConfig->MaxDanceStacks = 3;
	H.CombatConfig->DanceDamageMultipliers = { 1.0f, 1.2f, 1.4f, 1.6f };

	TestFalse(TEXT("no source tag is mirrored before any dance stack"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));
	TestFalse(TEXT("the other source is absent too"),
		H.ASC->HasMatchingGameplayTag(AdvancingCounterSource));

	TestTrue(TEXT("a Kinsect Slash dance stack is accepted"),
		H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash));
	TestTrue(TEXT("the resource's own source field is the truth"),
		H.Resource->GetDanceSource() == EIGDanceSource::KinsectSlash);
	TestTrue(TEXT("the source is mirrored onto the ASC as a loose tag"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));
	TestFalse(TEXT("the other source stays absent"),
		H.ASC->HasMatchingGameplayTag(AdvancingCounterSource));

	// 层数到顶仍然要更新来源 —— 它们两个字段，来源不受上限影响。
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TestEqual(TEXT("stacks saturate at the configured maximum"),
		H.Resource->GetDanceStacks(), 3);
	TestTrue(TEXT("the source mirror survives stack saturation"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));

	H.Resource->AddDanceStack(EIGDanceSource::AdvancingCounter);
	TestTrue(TEXT("an Advancing Counter stack moves the mirror"),
		H.ASC->HasMatchingGameplayTag(AdvancingCounterSource));
	TestFalse(TEXT("the Kinsect Slash mirror is released on source change"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));

	H.Resource->ClearDanceStacks(EIGDanceClearReason::Landed);
	TestFalse(TEXT("clearing releases the Advancing Counter mirror"),
		H.ASC->HasMatchingGameplayTag(AdvancingCounterSource));
	TestFalse(TEXT("clearing leaves the Kinsect Slash mirror released as well"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));
	TestTrue(TEXT("clearing resets the resource value"),
		H.Resource->GetDanceSource() == EIGDanceSource::None
		&& H.Resource->GetDanceStacks() == 0);

	// 幂等：连清两次不会因为第二次早退而留下任何镜像。
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	H.Resource->ClearDanceStacks(EIGDanceClearReason::Hit);
	H.Resource->ClearDanceStacks(EIGDanceClearReason::Hit);
	TestFalse(TEXT("a repeated clear leaves no mirror behind"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));

	// 运行期关闭也走同一个入口：卸载后镜像必须一起走。
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TestTrue(TEXT("the mirror is live before shutdown"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));
	H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	TestFalse(TEXT("runtime shutdown releases the source mirror"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashSource));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerLandingRootMotionContractTest,
	"MHGZ.M5.KinsectPiercer.LandingMontageRequiresRootMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerLandingRootMotionContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAnimMontage* RootMotionMontage = MakeRootMotionLandingMontage();
	UAnimMontage* InPlaceMontage = MakeRootMotionLandingMontage(false);
	if (!TestNotNull(TEXT("the transient root-motion Over fixture builds"), RootMotionMontage)
		|| !TestNotNull(TEXT("the transient in-place negative control builds"), InPlaceMontage))
	{
		return false;
	}

	UMHGZKinsectPiercerAbility* Ability =
		NewObject<UMHGZKinsectPiercerAbility>(GetTransientPackage());
	Ability->LandingMontage = RootMotionMontage;
	TestTrue(TEXT("the Over montage reports enabled animation root motion"),
		RootMotionMontage->HasRootMotion());
	TestTrue(TEXT("one Landing203 section with no successor and root motion is accepted"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));
	TestEqual(TEXT("the root-motion segment uses DefaultSlot"),
		RootMotionMontage->SlotAnimTracks[0].SlotName, FName(TEXT("DefaultSlot")));
	UAnimSequence* Copy = Cast<UAnimSequence>(
		RootMotionMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
	if (!TestNotNull(TEXT("the fixture retains a copied imported animation segment"), Copy))
	{
		return false;
	}
	TestTrue(TEXT("only a transient sequence copy supplies the fixture"),
		Copy->GetOutermost() == GetTransientPackage() && Copy->HasAnyFlags(RF_Transient));
	TestTrue(TEXT("the copied sequence enables root motion and force root lock"),
		Copy->bEnableRootMotion && Copy->bForceRootLock);
	TestEqual(TEXT("the root stays locked to the first animation frame"),
		static_cast<int32>(Copy->RootMotionRootLock.GetValue()),
		static_cast<int32>(ERootMotionRootLock::AnimFirstFrame));

	RootMotionMontage->CompositeSections[0].NextSectionName = Ability->LandingSectionName;
	TestFalse(TEXT("a self-looping Landing203 montage is rejected even with root motion"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));
	RootMotionMontage->CompositeSections[0].NextSectionName = NAME_None;
	FCompositeSection& ExtraSection = RootMotionMontage->CompositeSections.AddDefaulted_GetRef();
	ExtraSection.SectionName = TEXT("UnexpectedLandingSection");
	TestFalse(TEXT("a root-motion landing montage with a second section is rejected"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));
	RootMotionMontage->CompositeSections.RemoveAt(1);
	TestTrue(TEXT("restoring the single non-looping section restores acceptance"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));

	Ability->LandingMontage = InPlaceMontage;
	TestFalse(TEXT("the in-place negative control has no enabled root motion"),
		InPlaceMontage->HasRootMotion());
	TestFalse(TEXT("an in-place Over is rejected despite its correct Landing203 section"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));
	Ability->LandingMontage = MakeSectionMontage({ TEXT("Landing203") });
	TestFalse(TEXT("a named landing section without an animation segment is rejected"),
		FMHGZKM5KinsectPiercerTest::LandingMontage(*Ability));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerLandingRootMotionOwnershipTransferTest,
	"MHGZ.M5.KinsectPiercer.LandingRootMotionModeRestoresAfterOwnershipTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerLandingRootMotionOwnershipTransferTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!TestTrue(TEXT("M3 harness builds"), H.Setup()))
	{
		H.Teardown();
		return false;
	}

	UMHGZKinsectPiercerAbility* Ability = NewObject<UMHGZKinsectPiercerAbility>(H.ASC);
	UMHGZKinsectPiercerAbility* ForeignMontageAbility =
		NewObject<UMHGZKinsectPiercerAbility>(H.ASC);
	UMHGZKinsectPiercerAbility* ForeignMovementAbility =
		NewObject<UMHGZKinsectPiercerAbility>(H.ASC);
	UAnimInstance* Anim = NewObject<UAnimInstance>(H.Character->GetMesh());
	UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
	const FGameplayAbilityActorInfo* ActorInfo = H.ASC->AbilityActorInfo.Get();
	if (!TestNotNull(TEXT("persistent test ability exists"), Ability)
		|| !TestNotNull(TEXT("foreign Montage ability exists"), ForeignMontageAbility)
		|| !TestNotNull(TEXT("foreign movement ability exists"), ForeignMovementAbility)
		|| !TestNotNull(TEXT("transient AnimInstance exists"), Anim)
		|| !TestNotNull(TEXT("Harness ActorInfo exists"), ActorInfo)
		|| !TestNotNull(TEXT("Harness CMC exists"), CMC))
	{
		H.Teardown();
		return false;
	}
	TestTrue(TEXT("velocity guard is meaningful on a grounded CMC"), CMC->IsMovingOnGround());

	const auto MakeToken = [&H](UGameplayAbility& TokenAbility)
	{
		FWeaponActionToken Token;
		Token.RuntimeToken = H.Host->GetCurrentToken();
		Token.AbilityHandle = H.GiveAbility(UMHGZKinsectPiercerAbility::StaticClass());
		Token.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
		Token.AbilityInstance = &TokenAbility;
		return Token;
	};
	const auto CaptureThenRetireOld = [this, &H, Ability, Anim, ActorInfo](
		const FWeaponActionToken& OldToken)
	{
		if (!TestTrue(TEXT("old action registers"), H.Host->RegisterAction(OldToken))
			|| !TestTrue(TEXT("old action initially owns Montage root motion"),
				H.Host->AcquireMontageRootMotion(OldToken)))
		{
			return false;
		}
		Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
		FMHGZKM5KinsectPiercerTest::InjectLandingRootMotionCapture(
			*Ability, *ActorInfo, OldToken, *Anim,
			static_cast<uint8>(ERootMotionMode::RootMotionFromEverything));
		if (!TestTrue(TEXT("injected ActorInfo resolves the real RuntimeHost"),
			Ability->GetRuntimeHost() == H.Host))
		{
			return false;
		}
		Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		return TestTrue(TEXT("Host releases old Montage owner"),
			H.Host->ReleaseMontageRootMotion(OldToken))
			&& TestTrue(TEXT("Host unregisters old action"), H.Host->UnregisterAction(OldToken));
	};

	const FWeaponActionToken OldMontage = MakeToken(*Ability);
	const FWeaponActionToken ForeignMontage = MakeToken(*ForeignMontageAbility);
	if (!TestTrue(TEXT("Montage transfer tokens are valid"),
		OldMontage.IsValid() && ForeignMontage.IsValid())
		|| !CaptureThenRetireOld(OldMontage)
		|| !TestTrue(TEXT("foreign Montage action registers"),
			H.Host->RegisterAction(ForeignMontage))
		|| !TestTrue(TEXT("foreign action acquires Montage root motion"),
			H.Host->AcquireMontageRootMotion(ForeignMontage)))
	{
		H.Teardown();
		return false;
	}
	const FVector MontageVelocity(321.0, -87.0, 0.0);
	CMC->Velocity = MontageVelocity;
	FMHGZKM5KinsectPiercerTest::ReleaseLandingRootMotion(*Ability);
	TestEqual(TEXT("stale owner restores its scoped FromEverything baseline"),
		Anim->RootMotionMode.GetValue(), ERootMotionMode::RootMotionFromEverything);
	TestTrue(TEXT("release keeps the foreign Montage owner"),
		H.Host->IsMontageRootMotionOwnedBy(ForeignMontage));
	TestTrue(TEXT("release keeps the foreign owner's CMC velocity"),
		CMC->Velocity.Equals(MontageVelocity));
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	FMHGZKM5KinsectPiercerTest::ReleaseLandingRootMotion(*Ability);
	TestEqual(TEXT("idempotent release does not overwrite a later explicit mode"),
		Anim->RootMotionMode.GetValue(), ERootMotionMode::IgnoreRootMotion);
	TestTrue(TEXT("idempotent release still keeps the foreign Montage owner"),
		H.Host->IsMontageRootMotionOwnedBy(ForeignMontage));
	TestTrue(TEXT("foreign Montage velocity remains unchanged after the second release"),
		CMC->Velocity.Equals(MontageVelocity));
	H.Host->ReleaseMontageRootMotion(ForeignMontage);
	H.Host->UnregisterAction(ForeignMontage);

	const FWeaponActionToken OldMovement = MakeToken(*Ability);
	const FWeaponActionToken ForeignMovement = MakeToken(*ForeignMovementAbility);
	FName ForeignWarpTarget;
	if (!TestTrue(TEXT("movement transfer tokens are valid"),
		OldMovement.IsValid() && ForeignMovement.IsValid())
		|| !CaptureThenRetireOld(OldMovement)
		|| !TestTrue(TEXT("foreign movement action registers"),
			H.Host->RegisterAction(ForeignMovement))
		|| !TestTrue(TEXT("foreign action acquires ActionMovement"),
			H.Host->AcquireActionMovement(ForeignMovement, ForeignWarpTarget)))
	{
		H.Teardown();
		return false;
	}
	const FVector MovementVelocity(-222.0, 45.0, 0.0);
	CMC->Velocity = MovementVelocity;
	FMHGZKM5KinsectPiercerTest::ReleaseLandingRootMotion(*Ability);
	TestEqual(TEXT("ActionMovement transfer also restores FromEverything"),
		Anim->RootMotionMode.GetValue(), ERootMotionMode::RootMotionFromEverything);
	TestTrue(TEXT("release keeps the foreign ActionMovement owner"),
		H.Host->IsActionMovementOwnedBy(ForeignMovement));
	TestTrue(TEXT("release keeps the ActionMovement owner's CMC velocity"),
		CMC->Velocity.Equals(MovementVelocity));

	FMHGZKM5KinsectPiercerTest::InjectLandingRootMotionCapture(
		*Ability, *ActorInfo, OldMovement, *Anim,
		static_cast<uint8>(ERootMotionMode::RootMotionFromEverything));
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	FMHGZKM5KinsectPiercerTest::ReleaseLandingRootMotion(*Ability);
	TestEqual(TEXT("captured release preserves an explicit non-temporary IgnoreRootMotion"),
		Anim->RootMotionMode.GetValue(), ERootMotionMode::IgnoreRootMotion);
	TestTrue(TEXT("different-mode release keeps the foreign ActionMovement owner"),
		H.Host->IsActionMovementOwnedBy(ForeignMovement));
	TestTrue(TEXT("different-mode release keeps foreign CMC velocity"),
		CMC->Velocity.Equals(MovementVelocity));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerDefaultsTest,
	"MHGZ.M5.KinsectPiercer.AbilityConfigurationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UMHGZKinsectPiercerAbility* Ability = GetDefault<UMHGZKinsectPiercerAbility>();
	if (!TestNotNull(TEXT("production defaults exist"), Ability))
	{
		return false;
	}

	TestTrue(TEXT("the ability is bound to Input.Weapon.RTY"),
		Ability->InputTag.MatchesTagExact(
			FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTY"))));
	TestEqual(TEXT("the montage entry section is the start-up phase"),
		Ability->DefaultEntrySection, FName(TEXT("Start200")));
	TestTrue(TEXT("the four section names are pairwise distinct"),
		Ability->StartSectionName != Ability->DiveSectionName
		&& Ability->DiveSectionName != Ability->DiveLoopSectionName
		&& Ability->StartSectionName != Ability->DiveLoopSectionName
		&& Ability->LandingSectionName != Ability->StartSectionName);

	TestTrue(TEXT("the action requires the live aerial falling gate"),
		FMHGZKM5KinsectPiercerTest::RequiresTag(*Ability,
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling"))));
	TestTrue(TEXT("the action requires an unsheathed weapon"),
		FMHGZKM5KinsectPiercerTest::RequiresTag(*Ability,
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"))));
	for (const TCHAR* Blocked : {
		TEXT("Combat.State.Grounded"),
		TEXT("Combat.State.Dead"),
		TEXT("Combat.State.Hitstun"),
		TEXT("Combat.State.Knockdown"),
		TEXT("Combat.State.Aerial.CantAttack") })
	{
		TestTrue(FString::Printf(TEXT("the action is blocked by %s"), Blocked),
			FMHGZKM5KinsectPiercerTest::BlocksTag(*Ability,
				FGameplayTag::RequestGameplayTag(Blocked)));
	}

	// 本招**不**要求 Aerial.Actionable：那是已退役的代理指标，空回与操虫斩都只查 Falling。
	TestFalse(TEXT("the action must not gate on the retired Aerial.Actionable tag"),
		FMHGZKM5KinsectPiercerTest::RequiresTag(*Ability,
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Actionable"))));

	// 交付形态：两条 Montage 都是**常驻资产引用**。默认未赋值是 M3 生成前的正常中间态，
	// 不是兜底路径 —— 运行时代码里没有任何 `NewObject<UAnimMontage>` 现拼（M3 出口复查）。
	TestTrue(TEXT("no montage is assigned before M3, and nothing synthesizes one at runtime"),
		Ability->AttackMontage == nullptr && Ability->LandingMontage == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerEntryGateTest,
	"MHGZ.M5.KinsectPiercer.EntryGateRequiresKinsectSlashSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerEntryGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGameplayTag FallingTag =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling"));
	const FGameplayTag UnsheathedTag =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"));

	FMHGZM3Harness H;
	if (!TestTrue(TEXT("M3 harness builds"), H.Setup()))
	{
		H.Teardown();
		return false;
	}
	H.CombatConfig->MaxDanceStacks = 3;
	H.CombatConfig->DanceDamageMultipliers = { 1.0f, 1.2f, 1.4f, 1.6f };

	// 闸门要求角色有 Mesh + AnimInstance，与生产链一致（本招只查它**存在**）。
	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	if (!TestNotNull(TEXT("the harness character has a mesh"), Mesh))
	{
		H.Teardown();
		return false;
	}
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.ASC->RefreshAbilityActorInfo();
	TestNotNull(TEXT("the harness mesh produces a live AnimInstance"),
		Mesh->GetAnimInstance());

	H.CombatConfig->KinsectPiercerStartProfile = MakeTestProfile();
	H.CombatConfig->KinsectPiercerDiveProfile = MakeTestProfile();
	H.CombatConfig->KinsectPiercerLandingProfile = MakeTestProfile();
	H.CombatConfig->KinsectPiercerDiveWatchdogSeconds = 12.0f;
	if (!TestTrue(TEXT("the transient imported Over root-motion fixture builds"), ConfigureGateAbility()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = H.ASC->GiveAbility(FGameplayAbilitySpec(
		UMHGZM5KinsectPiercerGateProbeAbility::StaticClass(), 1, INDEX_NONE, H.ASC));

	// ── 正控：先把每一层都摆对，确认这条链真的能通过 ──
	// 离地由 Host 领裸 `Combat.State.Aerial.Falling`（`SetGrounded(false)` 里那一句），
	// 与生产同一条路 —— 测试不再自己 AddLooseGameplayTag，否则会造出第二个计数，
	// 后面那次「放掉 Falling」的负控就减不掉 Host 的那一份。
	H.Host->SetGrounded(false);
	H.ASC->AddLooseGameplayTag(UnsheathedTag);
	H.Resource->ClearDanceStacks(EIGDanceClearReason::Hit);
	TestTrue(TEXT("leaving the ground grants the bare aerial falling gate"),
		H.ASC->HasMatchingGameplayTag(FallingTag));

	bool bAccepted = false;
	bool bReached = false;
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("without any dance source the activation is refused"),
		!bAccepted && !bReached);

	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(FString::Printf(
		TEXT("with a Kinsect Slash source and a live Falling gate the activation passes every layer (called=%d accepted=%d reached=%d)"),
		UMHGZM5KinsectPiercerGateProbeAbility::bLastDependenciesCalled ? 1 : 0,
		bAccepted ? 1 : 0, bReached ? 1 : 0),
		bAccepted && bReached);
	TestTrue(TEXT("the production PrepareAttackMontage accepts the montage pair"),
		UMHGZM5KinsectPiercerGateProbeAbility::bLastProductionPrepareAccepted);
	TestTrue(TEXT("the production 200 and 201 requests still have valid recorded trajectories"),
		UMHGZM5KinsectPiercerGateProbeAbility::bLastStartMovementRequestValid
		&& UMHGZM5KinsectPiercerGateProbeAbility::bLastDiveMovementRequestValid);
	TestFalse(TEXT("203 cannot produce a valid recorded request even with a valid legacy landing profile"),
		UMHGZM5KinsectPiercerGateProbeAbility::bLastLandingMovementRequestValid);

	// §6.2：「入招后释放 Falling 许可」。这是 200/201 唯一的空中取消窗口锁 ——
	// 空回的闸门就是「Falling 在场 + 无 CantDodge」，而舞踏提交时 Host 会把 CantDodge
	// 和 CantAttack 一起退掉，所以不释放它，空回能在下潜中途把本招顶掉。
	// 同时它也停掉了系统托管下落的表现会话，触地时才不会被系统落地表现重设成 337 cm/s。
	TestFalse(TEXT("the action releases the aerial falling permit before its montage starts"),
		UMHGZM5KinsectPiercerGateProbeAbility::bLastFallingPermitHeldAfterPrepare);
	// 许可在蒙太奇起播前就被放掉（上一行读的是**那一刻**的 ASC）。探针随后干净失败，
	// 于是基类 `UMHGZInsectGlaiveAbility::EndAbility` 在「取消 + 未接地 + 无其它已注册动作」
	// 时把它领了回来 —— 中止滞空不该把玩家永久锁在门外，这一条钉住那次回领。
	TestTrue(TEXT("an interrupted aerial action hands the falling permit back"),
		H.ASC->HasMatchingGameplayTag(FallingTag));

	// ── 形状层：两条空中曲线与看门狗缺一不可；203 的来源是 Montage Root Motion ──
	// 逐条从上面这个**通过**的状态翻过去，翻完再摆回来。
	{
		const FRecordedLocalTrajectoryProfile SavedLanding = H.CombatConfig->KinsectPiercerLandingProfile;
		H.CombatConfig->KinsectPiercerLandingProfile = FRecordedLocalTrajectoryProfile();
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a missing legacy landing profile still passes dependencies and production prepare"),
			bAccepted && bReached
			&& UMHGZM5KinsectPiercerGateProbeAbility::bLastProductionPrepareAccepted);
		TestTrue(TEXT("200 and 201 retain valid requests without the legacy landing profile"),
			UMHGZM5KinsectPiercerGateProbeAbility::bLastStartMovementRequestValid
			&& UMHGZM5KinsectPiercerGateProbeAbility::bLastDiveMovementRequestValid);
		TestFalse(TEXT("203 still has no valid recorded request when the legacy profile is absent"),
			UMHGZM5KinsectPiercerGateProbeAbility::bLastLandingMovementRequestValid);
		H.CombatConfig->KinsectPiercerLandingProfile = SavedLanding;
	}
	{
		UCurveVector* SavedCurve = H.CombatConfig->KinsectPiercerStartProfile.Curve;
		H.CombatConfig->KinsectPiercerStartProfile.Curve = nullptr;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a missing start curve refuses the activation"), !bAccepted && !bReached);
		H.CombatConfig->KinsectPiercerStartProfile.Curve = SavedCurve;
	}
	{
		UCurveVector* SavedCurve = H.CombatConfig->KinsectPiercerDiveProfile.Curve;
		H.CombatConfig->KinsectPiercerDiveProfile.Curve = nullptr;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a missing dive curve refuses the activation"), !bAccepted && !bReached);
		H.CombatConfig->KinsectPiercerDiveProfile.Curve = SavedCurve;
	}
	{
		const float SavedWatchdog = H.CombatConfig->KinsectPiercerDiveWatchdogSeconds;
		H.CombatConfig->KinsectPiercerDiveWatchdogSeconds = 0.0f;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a non-positive dive watchdog refuses the activation"),
			!bAccepted && !bReached);
		H.CombatConfig->KinsectPiercerDiveWatchdogSeconds = SavedWatchdog;
	}
	{
		UAnimMontage* SavedMontage = UMHGZM5KinsectPiercerGateProbeAbility::TestMainMontage;
		UMHGZM5KinsectPiercerGateProbeAbility::TestMainMontage =
			MakeSectionMontage({ TEXT("Start200"), TEXT("Dive201") });
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a main montage without the self-looping section refuses the activation"),
			!bAccepted && !bReached);
		UMHGZM5KinsectPiercerGateProbeAbility::TestMainMontage = SavedMontage;

		UAnimMontage* SavedLanding = UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage;
		UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage = nullptr;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("a missing landing montage refuses the activation"),
			!bAccepted && !bReached);
		UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage = SavedLanding;
	}
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("restoring the curves and montages restores the gate"),
		bAccepted && bReached);

	// ── 逐条否决；每条都先摆回上面那个**通过**的状态再翻 ──

	// 来源改成 AdvancingCounter：Resource 的两个字段一起变，标签也跟着变。
	H.Resource->AddDanceStack(EIGDanceSource::AdvancingCounter);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("an Advancing Counter source refuses the action"), !bAccepted && !bReached);

	H.Resource->ClearDanceStacks(EIGDanceClearReason::Hit);
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("returning to a Kinsect Slash source re-opens the gate"), bAccepted && bReached);

	// 层数到上限后再次操虫斩有效命中：来源仍然正确，本招仍应放行。
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("a saturated dance stack still admits the action: the source is not the stack count"),
		bAccepted && bReached);

	// 操虫斩舞踏的保护前段：Falling 被放掉，本招必须拒绝。
	// 这正是 `DetectAerialHandoffNotify` 在保护段里做的事，直到 Handoff 才领回来。
	H.Host->ReleaseAerialFallingState();
	TestFalse(TEXT("the dance protection window really released the falling gate"),
		H.ASC->HasMatchingGameplayTag(FallingTag));
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("without Aerial.Falling (the dance protection window) the action is refused"),
		!bAccepted && !bReached);
	H.Host->AcquireAerialFallingState();
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("the aerial handoff re-opening Falling admits the action again"),
		bAccepted && bReached);

	// 已经站在地上。
	H.Host->SetGrounded(true);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("a grounded host refuses the action"), !bAccepted && !bReached);
	H.Host->SetGrounded(false);

	// 空中预算已经用掉。
	const FGameplayTag CantAttack =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantAttack"));
	H.ASC->AddLooseGameplayTag(CantAttack);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("a spent aerial attack budget refuses the action"), !bAccepted && !bReached);
	H.ASC->RemoveLooseGameplayTag(CantAttack);

	// 收刀。
	H.ASC->RemoveLooseGameplayTag(UnsheathedTag);
	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("a sheathed weapon refuses the action"), !bAccepted && !bReached);
	H.ASC->AddLooseGameplayTag(UnsheathedTag);

	// 失能状态。
	for (const TCHAR* Disqualifying : {
		TEXT("Combat.State.Dead"),
		TEXT("Combat.State.Hitstun"),
		TEXT("Combat.State.Knockdown") })
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Disqualifying);
		H.ASC->AddLooseGameplayTag(Tag);
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(FString::Printf(TEXT("%s refuses the action"), Disqualifying),
			!bAccepted && !bReached);
		H.ASC->RemoveLooseGameplayTag(Tag);
	}

	TryGate(H, Handle, bAccepted, bReached);
	TestTrue(TEXT("clearing every disqualifier restores the gate"), bAccepted && bReached);

	// 三条武器判定缺一不可，而且必须是**武器**判定（不能照搬操虫斩 [0] 的猎虫段）。
	{
		UMHGZM5KinsectPiercerGateProbeAbility::bTestConfigureAttackSegments = false;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("fewer than three weapon segments refuses the action"),
			!bAccepted && !bReached);
		UMHGZM5KinsectPiercerGateProbeAbility::bTestConfigureAttackSegments = true;
		TryGate(H, Handle, bAccepted, bReached);
		TestTrue(TEXT("restoring the three segments restores the gate"), bAccepted && bReached);
	}

	H.Teardown();
	return true;
}

#endif
