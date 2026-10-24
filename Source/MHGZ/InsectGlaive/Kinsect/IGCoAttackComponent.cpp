// Copyright MHGZ Project. All Rights Reserved.
#include "IGCoAttackComponent.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kinsect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "DrawDebugHelpers.h"

bool FIGCoAttackHistory::Add(const double Time, const FTransform& Pose,
	const FIGCoAttackTuning& Tuning, const TOptional<double> RetainFrom)
{
	if (!Tuning.IsValid() || !FMath::IsFinite(Time) || Pose.ContainsNaN()
		|| !Pose.GetRotation().IsNormalized()) return false;
	if (!Samples.IsEmpty())
	{
		const double Gap = Time - Samples.Last().Time;
		if (Gap < 0.0 || Gap > Tuning.MaxSampleGapSeconds) return false;
		// Pause / multiple updates in one world frame must not consume capacity.
		if (Gap == 0.0) return true;
	}
	// Preserve one predecessor to interpolate the oldest needed replay point.
	const double Cutoff = RetainFrom.IsSet()
		? FMath::Min(RetainFrom.GetValue(), Time - Tuning.TrajectoryDelaySeconds)
		: Time - Tuning.TrajectoryDelaySeconds;
	while (Samples.Num() > 1 && Samples[1].Time < Cutoff)
		Samples.RemoveAt(0, 1, EAllowShrinking::No);
	// Never silently evict history still needed to represent the delay.
	if (Samples.Num() >= Tuning.MaxSamples) return false;
	Samples.Add({Time, Pose});
	return true;
}

bool FIGCoAttackHistory::GetPath(const double Begin, const double End, TArray<FSample>& Out) const
{
	Out.Reset();
	FTransform FirstPose, LastPose;
	if (End < Begin || !Evaluate(Begin, FirstPose) || !Evaluate(End, LastPose)) return false;
	Out.Add({Begin, FirstPose});
	for (const FSample& Sample : Samples)
		if (Sample.Time > Begin && Sample.Time < End) Out.Add(Sample);
	if (End > Begin) Out.Add({End, LastPose});
	return true;
}

bool FIGCoAttackHistory::Evaluate(const double Time, FTransform& OutPose) const
{
	if (!FMath::IsFinite(Time) || Samples.IsEmpty()
		|| Time < Samples[0].Time || Time > Samples.Last().Time) return false;
	for (int32 I = 1; I < Samples.Num(); ++I)
	{
		if (Time <= Samples[I].Time)
		{
			const FSample& A = Samples[I - 1];
			const FSample& B = Samples[I];
			OutPose.Blend(A.Pose, B.Pose, (Time - A.Time) / (B.Time - A.Time));
			return true;
		}
	}
	OutPose = Samples.Last().Pose;
	return true;
}

UIGCoAttackComponent::UIGCoAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

bool UIGCoAttackComponent::Start(URes_InsectGlaive* InResource,
	const FWeaponActionToken& Action, const uint64 Serial,
	USkeletalMeshComponent* InWeapon, const FIGCoAttackSource& InSource,
	const FIGCoAttackTuning& InTuning, const uint64 DescendingThrustHoverSerial)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (IsRunning() || !IsValid(InResource) || !IsValid(InWeapon)
		|| !Character || !Character->GetMesh() || !Character->GetCharacterMovement()
		|| !Character->GetCharacterMovement()->IsMovingOnGround()
		|| !Action.IsValid() || Serial == 0 || !InSource.bEnabled || InSource.Socket.IsNone()
		|| !InWeapon->DoesSocketExist(InSource.Socket) || InSource.LocalOffset.ContainsNaN()
		|| InWeapon->GetOwner() != Character || !InTuning.IsValid()
		|| !InResource->IsKinsectCommandCurrent(Serial, EKinsectCommandSource::CoAttack,
			0, Action.RuntimeToken)) return false;
	AKinsect* Insect = InResource->GetKinsectActor();
	if (!Insect || !Insect->BeginCoAttack(Action.RuntimeToken, Serial, DescendingThrustHoverSerial)) return false;
	Resource = InResource;
	Weapon = InWeapon;
	CharacterMesh = Character->GetMesh();
	ActionToken = Action;
	CommandSerial = Serial;
	Source = InSource;
	Tuning = InTuning;
	History.Reset();
	bTracking = false;
	bEntryAtFirstSample = false;
	Windows.Reset();
	NextReceiptID = 0;
	SourceEndTime.Reset();
	AttackPathEndTime.Reset();
	FrozenAttackPose.Reset();
	bReturnAfterAttackReplay = false;
	bRecoveryPathFrozen = false;
	ReplayCursor.Reset();
	TrackingSince.Reset();
	ReplayForward = Insect->GetAttackForwardVector();
	// Skeletal tick completion includes parallel evaluation; never sample from a notify.
	AddTickPrerequisiteComponent(CharacterMesh.Get());
	AddTickPrerequisiteComponent(InWeapon);
	AddTickPrerequisiteComponent(InResource); // drain/depletion before replay
	ActionRegisteredHandle = Action.RuntimeToken.Host->OnWeaponActionRegistered.AddUObject(
		this, &UIGCoAttackComponent::HandleActionRegistered);
	DodgeAcceptOpenedHandle = Action.RuntimeToken.Host->OnWeaponDodgeAcceptWindowOpened.AddUObject(
		this, &UIGCoAttackComponent::HandleDodgeAcceptWindowOpened);
	ComboClosedHandle = Action.RuntimeToken.Host->OnWeaponComboWindowsClosed.AddUObject(
		this, &UIGCoAttackComponent::HandleComboWindowsClosed);
	SetComponentTickEnabled(true);
	return true;
}

bool UIGCoAttackComponent::Seal(const FWeaponActionToken& Action)
{
	if (!IsRunning() || Action != ActionToken) return false;
	if (SourceEndTime.IsSet()) return true; // first end wins
	if (!IsSessionCurrent(CommandSerial) || !History.Last()) return false;
	const double End = GetWorld()->GetTimeSeconds();
	// Do not sample an unevaluated/new montage pose inside EndAbility/Notify.
	// Hold the last completed post-pose sample only up to this exact end time.
	const FTransform LastPose = History.Last()->Pose;
	const TOptional<double> Retain = ReplayCursor.IsSet() ? ReplayCursor
		: TOptional<double>(History.First()->Time);
	if (!History.Add(End, LastPose, Tuning, Retain)) return false;
	SourceEndTime = End;
	for (FWindow& W : Windows)
		if (!W.CloseTime.IsSet()) W.CloseTime = FMath::Max(W.Spec.OpenTime, End);
	return true;
}

bool UIGCoAttackComponent::Join(const FWeaponActionToken& Action,
	USkeletalMeshComponent* InWeapon, const FIGCoAttackSource& InSource)
{
	if (!IsSessionCurrent(CommandSerial) || !Action.IsValid()
		|| Action.RuntimeToken != ActionToken.RuntimeToken
		|| !Action.RuntimeToken.Host->IsActionRegistered(Action)
		|| !InSource.bEnabled || InWeapon != Weapon.Get() || InSource.Socket != Source.Socket
		|| !InSource.LocalOffset.Equals(Source.LocalOffset, .001f)) return false;
	if (Action == ActionToken) return !SourceEndTime.IsSet();
	const double Now = GetWorld()->GetTimeSeconds();
	if (SourceEndTime.IsSet() && Now > SourceEndTime.GetValue() + Tuning.TrajectoryDelaySeconds)
		return false;
	if (History.Last() && Now - History.Last()->Time > Tuning.MaxSampleGapSeconds) return false;
	if (!SourceEndTime.IsSet() && !Seal(ActionToken)) return false;
	ActionToken = Action;
	Source = InSource;
	SourceEndTime.Reset();
	AttackPathEndTime.Reset();
	FrozenAttackPose.Reset();
	bReturnAfterAttackReplay = false;
	bRecoveryPathFrozen = false;
	return true;
}

void UIGCoAttackComponent::FinishAttackPath(const FWeaponActionToken& Action, const bool bReturnAfterReplay)
{
	if (Action != ActionToken || !IsSessionCurrent(CommandSerial)
		|| SourceEndTime.IsSet() || bRecoveryPathFrozen) return;
	if (!AttackPathEndTime.IsSet()) AttackPathEndTime = GetWorld()->GetTimeSeconds();
	bReturnAfterAttackReplay |= bReturnAfterReplay;
}

void UIGCoAttackComponent::HandleDodgeAcceptWindowOpened(const FWeaponActionToken& Action)
{
	if (Action != ActionToken || !IsSessionCurrent(CommandSerial)
		|| SourceEndTime.IsSet() || bRecoveryPathFrozen) return;
	bRecoveryPathFrozen = true;
	AttackPathEndTime = GetWorld()->GetTimeSeconds();
	// NotifyBegin precedes evaluated skeletal pose completion. Never read a socket
	// here: keep the last completed sample (the frame before the window opens).
	// If no sample exists yet, Tick captures the first available post-pose sample.
	if (History.Last()) FrozenAttackPose = History.Last()->Pose;
	bReturnAfterAttackReplay = false; // actual combo closure still owns recall
}

void UIGCoAttackComponent::HandleComboWindowsClosed(const FWeaponActionToken& Action)
{
	if (IsRunning() && Action == ActionToken)
		if (URes_InsectGlaive* RC = Resource.Get()) RC->EndKinsectCoAttack(ActionToken);
}

void UIGCoAttackComponent::HandleActionRegistered(const FWeaponActionToken& Action)
{
	if (!IsRunning() || Action == ActionToken || Action.RuntimeToken != ActionToken.RuntimeToken) return;
	if (auto* IG = Cast<UMHGZInsectGlaiveAbility>(Action.AbilityInstance.Get()))
		if (IG->TryStartKinsectCoAttack()) return;
	// Only successful registration reaches here. Failed Commit never preempts us.
	if (URes_InsectGlaive* RC = Resource.Get()) RC->EndKinsectCoAttack(ActionToken);
}

void UIGCoAttackComponent::Stop()
{
	// Invalidate identity first. A later old GA End cannot stop a newer command.
	if (ActionToken.RuntimeToken.Host.IsValid())
	{
		ActionToken.RuntimeToken.Host->OnWeaponActionRegistered.Remove(ActionRegisteredHandle);
		ActionToken.RuntimeToken.Host->OnWeaponDodgeAcceptWindowOpened.Remove(DodgeAcceptOpenedHandle);
		ActionToken.RuntimeToken.Host->OnWeaponComboWindowsClosed.Remove(ComboClosedHandle);
	}
	ActionRegisteredHandle.Reset();
	DodgeAcceptOpenedHandle.Reset();
	ComboClosedHandle.Reset();
	ActionToken = FWeaponActionToken();
	CommandSerial = 0;
	SetComponentTickEnabled(false);
	if (Weapon.IsValid()) RemoveTickPrerequisiteComponent(Weapon.Get());
	if (CharacterMesh.IsValid()) RemoveTickPrerequisiteComponent(CharacterMesh.Get());
	if (Resource.IsValid()) RemoveTickPrerequisiteComponent(Resource.Get());
	Weapon.Reset();
	CharacterMesh.Reset();
	Resource.Reset();
	History.Reset();
	bTracking = false;
	bEntryAtFirstSample = false;
	Windows.Reset();
	ReplayCursor.Reset();
	TrackingSince.Reset();
	SourceEndTime.Reset();
	AttackPathEndTime.Reset();
	FrozenAttackPose.Reset();
	bReturnAfterAttackReplay = false;
	bRecoveryPathFrozen = false;
}

void UIGCoAttackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	Super::EndPlay(Reason);
}

void UIGCoAttackComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsRunning()) return;
	URes_InsectGlaive* RC = Resource.Get();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AKinsect* Insect = RC ? RC->GetKinsectActor() : nullptr;
	auto Retire = [&]()
	{
		if (RC) RC->EndKinsectCoAttack(ActionToken);
		else Stop();
	};
	if (!RC || !IsValid(Insect) || !Weapon.IsValid() || !Character
		|| !Weapon->IsRegistered() || !CharacterMesh.IsValid() || !CharacterMesh->IsRegistered()
		|| !IsSessionCurrent(CommandSerial)
		|| !Weapon->DoesSocketExist(Source.Socket)) { Retire(); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (SourceEndTime.IsSet() && ReplayCursor.IsSet() && ReplayCursor.GetValue() >= SourceEndTime.GetValue())
	{
		Retire(); return;
	}
	const FTransform SocketPose = Weapon->GetSocketTransform(Source.Socket);
	FTransform Sample = SocketPose;
	Sample.SetLocation(SocketPose.TransformPosition(Source.LocalOffset));
	TOptional<double> RetainFrom = ReplayCursor;
	if (!RetainFrom.IsSet() && History.First()) RetainFrom = History.First()->Time;
	if (AttackPathEndTime.IsSet())
	{
		if (!FrozenAttackPose.IsSet())
		{
			// The notification is before post-pose sampling. Capture its final frame
			// here; if the game clock already advanced, keep the last evaluated pose.
			FrozenAttackPose = FMath::Abs(Now - AttackPathEndTime.GetValue()) < 1.e-5
				? Sample : (History.Last() ? History.Last()->Pose : Sample);
			if (!SourceEndTime.IsSet() && !History.Add(AttackPathEndTime.GetValue(), FrozenAttackPose.GetValue(), Tuning, RetainFrom))
			{ Retire(); return; }
		}
		Sample = FrozenAttackPose.GetValue(); // bounded stationary history, not recovery-tip movement
	}
	if (!SourceEndTime.IsSet() && !History.Add(Now, Sample, Tuning, RetainFrom)) { Retire(); return; }
	if (!History.First()) { Retire(); return; }
	const double ReadTime = SourceEndTime.IsSet()
		? FMath::Min(Now - Tuning.TrajectoryDelaySeconds, SourceEndTime.GetValue())
		: Now - Tuning.TrajectoryDelaySeconds;
	FTransform Delayed;
	const bool bHasHistory = History.Evaluate(ReadTime, Delayed);
	// Entry aims at the first real sample; it never invents negative-time history.
	const FVector Target = bHasHistory ? Delayed.GetLocation() : History.First()->Pose.GetLocation();
	const FVector Previous = Insect->GetActorLocation();
	const FVector Next = bTracking ? Target : FMath::VInterpConstantTo(Previous, Target,
		DeltaTime, Insect->GetReturnSpeed());
	if (!bHasHistory) bEntryAtFirstSample = Next.Equals(Target, .01f);
	bTracking = bHasHistory && (bTracking || Next.Equals(Target, .01f));
	if (bTracking && !TrackingSince.IsSet())
		TrackingSince = bEntryAtFirstSample ? History.First()->Time : ReadTime;
	if (!Insect->ApplyCoAttackPose(CommandSerial, Next, Next - Previous)) { Retire(); return; }
	const uint64 Serial = CommandSerial;
	if (bHasHistory)
	{
		const double From = ReplayCursor.IsSet() ? ReplayCursor.GetValue() : History.First()->Time;
		if (!ReplayWindows(From, ReadTime, Serial))
		{
			if (CommandSerial == Serial) Retire(); // never retire a new session created by a GE callback
			return;
		}
		ReplayCursor = ReadTime;
	}
	if (SourceEndTime.IsSet() && bHasHistory && ReadTime >= SourceEndTime.GetValue())
	{
		Retire();
		return;
	}
	if (bReturnAfterAttackReplay && AttackPathEndTime.IsSet() && bHasHistory
		&& ReadTime >= AttackPathEndTime.GetValue()) { Retire(); return; }
	if (Tuning.bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), Sample.GetLocation(), 5.f, 8, FColor::Cyan);
		DrawDebugSphere(GetWorld(), Target, 5.f, 8, bHasHistory ? FColor::Yellow : FColor::Silver);
		DrawDebugSphere(GetWorld(), Next, 5.f, 8, bTracking ? FColor::Green : FColor::Orange);
	}
}
