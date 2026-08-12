// Copyright MHGZ Project. All Rights Reserved.

#include "Res_InsectGlaive.h"

#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstance.h"
#include "GameplayEffect.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/IGMarkProjectile.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& WhiteExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

const FGameplayTag& OrangeExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Orange"));
	return Tag;
}

const FGameplayTag& RedExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Red"));
	return Tag;
}

const FGameplayTag& TripleCostTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Cost.IG.TripleUp"));
	return Tag;
}

const FGameplayTag& KinsectActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Kinsect.Active"));
	return Tag;
}

const FGameplayTag& MarkActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Mark.Active"));
	return Tag;
}
}

URes_InsectGlaive::URes_InsectGlaive()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	WeaponTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Weapon.InsectGlaive"));
}

void URes_InsectGlaive::InitializeRuntime(const FWeaponRuntimeContext& Context)
{
	Super::InitializeRuntime(Context);
	bRuntimeShuttingDown = false;
	CombatConfig = Cast<UInsectGlaiveCombatConfig>(Context.CombatConfig.Get());
	if (!CombatConfig || !Context.Character.IsValid() || !Context.ASC.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[IG Resource] Invalid runtime context; resource remains inert."));
		return;
	}

	SetComponentTickEnabled(true);
	KinsectData = CombatConfig->KinsectData;
	if (!KinsectData)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[IG Resource] CombatConfig has no KinsectData; resource remains inert."));
		return;
	}

	USceneComponent* AttachComponent = Context.Character->GetMesh();
	OnWeaponEquipped(KinsectData, AttachComponent, CombatConfig->KinsectAttachSocket);
}

void URes_InsectGlaive::ShutdownRuntime(EWeaponRuntimeEndReason Reason)
{
	if (bRuntimeShuttingDown)
	{
		return;
	}
	bRuntimeShuttingDown = true;
	SetComponentTickEnabled(false);

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
	}
	ClearKinsectMark(Reason == EWeaponRuntimeEndReason::WeaponChanged
		? EIGMarkClearReason::WeaponChanged : EIGMarkClearReason::RuntimeShutdown);
	OnWeaponUnequipped();
	ClearAllResourceGameplayEffects();
	TripleReservations.Reset();
	CombatConfig = nullptr;

	Super::ShutdownRuntime(Reason);
}

void URes_InsectGlaive::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bRuntimeShuttingDown || !KinsectActor || !KinsectData)
	{
		return;
	}

	const EKinsectState State = KinsectActor->GetState();
	const float PreviousStamina = KinsectStamina;
	if (State == EKinsectState::Flying)
	{
		KinsectStamina = FMath::Max(0.f,
			KinsectStamina - KinsectData->FlightDrainRate * DeltaTime);
	}
	else if (State == EKinsectState::Hovering)
	{
		KinsectStamina = FMath::Max(0.f,
			KinsectStamina - KinsectData->HoverDrainRate * DeltaTime);
	}
	else if (State == EKinsectState::Attached)
	{
		KinsectStamina = FMath::Min(MaxKinsectStamina,
			KinsectStamina + KinsectData->StaminaRegenRate * DeltaTime);
	}

	if (PreviousStamina > 0.f && KinsectStamina <= 0.f
		&& (State == EKinsectState::Flying || State == EKinsectState::Hovering))
	{
		bDepletionEdgeTriggered = true;
		PlayResourceSound(CombatConfig ? CombatConfig->KinsectDepletedSound : nullptr);
		KinsectActor->ForceRecall();
	}
	else if (KinsectStamina > 0.f)
	{
		bDepletionEdgeTriggered = false;
	}

	if (!FMath::IsNearlyEqual(PreviousStamina, KinsectStamina))
	{
		OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
	}

	const bool bHasTrackedMark = ActiveMarkHitzone.IsValid()
		|| ActiveMarkProjectile.IsValid() || MarkActiveTagToken.IsValid();
	if (bHasTrackedMark
		&& (!ActiveMarkHitzone.IsValid() || !ActiveMarkProjectile.IsValid()
			|| !IsValid(ActiveMarkHitzone->GetOwner())))
	{
		ClearKinsectMark(EIGMarkClearReason::TargetInvalid);
	}
}

bool URes_InsectGlaive::OnWeaponEquipped(UInsectGlaiveKinsectData* Data,
	USceneComponent* AttachComponent, FName AttachSocket)
{
	if (!Data || !AttachComponent
		|| (!AttachSocket.IsNone() && !AttachComponent->DoesSocketExist(AttachSocket))
		|| !GetWorld() || KinsectActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[IG Resource] Cannot spawn kinsect: attach component/socket '%s' is invalid."),
			*AttachSocket.ToString());
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	KinsectActor = GetWorld()->SpawnActor<AKinsect>(AKinsect::StaticClass(), SpawnParams);
	if (!KinsectActor)
	{
		return false;
	}

	KinsectData = Data;
	KinsectAttachComponent = AttachComponent;
	KinsectAttachSocket = AttachSocket;
	KinsectActor->KinsectData = Data;
	KinsectActor->ResourceComponent = this;
	if (USkeletalMesh* Mesh = Data->KinsectMesh.LoadSynchronous())
	{
		KinsectActor->Mesh->SetSkeletalMesh(Mesh);
	}
	if (Data->KinsectAnimClass)
	{
		KinsectActor->Mesh->SetAnimInstanceClass(Data->KinsectAnimClass);
	}
	for (const TPair<FName, TSoftObjectPtr<UMaterialInstance>>& Pair : Data->MaterialOverrides)
	{
		if (UMaterialInstance* Material = Pair.Value.LoadSynchronous())
		{
			const int32 Index = KinsectActor->Mesh->GetMaterialIndex(Pair.Key);
			if (Index != INDEX_NONE)
			{
				KinsectActor->Mesh->SetMaterial(Index, Material);
			}
		}
	}
	KinsectActor->AttachToPlayer(AttachComponent, AttachSocket);
	MaxKinsectStamina = FMath::Max(0.f, Data->StaminaPool);
	KinsectStamina = MaxKinsectStamina;
	OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
	return true;
}

void URes_InsectGlaive::OnWeaponUnequipped()
{
	SetKinsectActiveTag(false);
	// Kinsect.Active is Resource-exclusive. Normalize any pre-TagLedger count left
	// by an older runtime so a weapon swap cannot carry that stale state forward.
	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		ASC->SetLooseGameplayTagCount(KinsectActiveTag(), 0);
	}
	if (KinsectActor)
	{
		KinsectActor->Destroy();
		KinsectActor = nullptr;
	}
	KinsectData = nullptr;
	KinsectAttachComponent.Reset();
	KinsectAttachSocket = NAME_None;
	KinsectStamina = 0.f;
	MaxKinsectStamina = 0.f;
	bDepletionEdgeTriggered = false;
}

bool URes_InsectGlaive::CanDeployKinsect(const FKinsectFlightRequest& Request) const
{
	return !bRuntimeShuttingDown && KinsectActor && KinsectData
		&& KinsectStamina > 0.f && IsRuntimeRequestCurrent(Request.RuntimeToken)
		&& Request.MaxDistance > 0.f && Request.FlightSpeed > 0.f
		&& Request.ArrivalRadius > 0.f;
}

bool URes_InsectGlaive::DeployKinsect(const FKinsectFlightRequest& Request)
{
	if (!CanDeployKinsect(Request) || !KinsectActor->BeginFlight(Request))
	{
		return false;
	}
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::RecallKinsect()
{
	if (bRuntimeShuttingDown || !KinsectActor
		|| KinsectActor->GetState() == EKinsectState::Attached)
	{
		return false;
	}
	KinsectActor->StartReturn();
	return true;
}

void URes_InsectGlaive::OnKinsectReachedPlayer(FGameplayTag ExtractColor)
{
	SetKinsectActiveTag(false);
	bDepletionEdgeTriggered = false;
	if (ExtractColor.IsValid())
	{
		ApplyExtract(ExtractColor);
	}
}

bool URes_InsectGlaive::IsRuntimeRequestCurrent(const FWeaponRuntimeToken& Token) const
{
	return !bRuntimeShuttingDown && Token.IsValid()
		&& RuntimeContext.RuntimeToken == Token
		&& Token.Host.IsValid() && Token.Host->IsTokenCurrent(Token);
}

bool URes_InsectGlaive::IsLeafExtractTag(const FGameplayTag& ExtractColor) const
{
	return ExtractColor.MatchesTagExact(WhiteExtractTag())
		|| ExtractColor.MatchesTagExact(OrangeExtractTag())
		|| ExtractColor.MatchesTagExact(RedExtractTag());
}

bool URes_InsectGlaive::IsHandleActive(const FActiveGameplayEffectHandle& Handle) const
{
	const UAbilitySystemComponent* ASC = GetPlayerASC();
	return ASC && Handle.IsValid() && ASC->GetActiveGameplayEffect(Handle) != nullptr;
}

bool URes_InsectGlaive::ApplyExtract(FGameplayTag ExtractColor)
{
	if (!IsLeafExtractTag(ExtractColor) || !CombatConfig || !GetPlayerASC())
	{
		return false;
	}

	// 三灯期间所有吸收入口都统一吞灯，绝不生成/缓存/刷新任何 GE。
	if (IsTripleUpActive())
	{
		PlayResourceSound(CombatConfig->ExtractCollectedSound);
		return true;
	}

	const bool bApplied = ApplySingleExtractEffect(ExtractColor);
	if (bApplied)
	{
		PlayResourceSound(CombatConfig->ExtractCollectedSound);
		CheckAndActivateTripleUp();
		BroadcastExtractState();
	}
	return bApplied;
}

bool URes_InsectGlaive::ApplyExtractFromHitzone(
	const UMHGZMonsterHitzoneComponent* Hitzone)
{
	return Hitzone && ApplyExtract(Hitzone->ExtractColorTag);
}

bool URes_InsectGlaive::ApplySingleExtractEffect(FGameplayTag ExtractColor)
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC || !CombatConfig)
	{
		return false;
	}

	TSubclassOf<UGameplayEffect> EffectClass;
	float Duration = 0.f;
	FGameplayTag MultiplierTag;
	float Multiplier = 1.f;
	if (ExtractColor.MatchesTagExact(WhiteExtractTag()))
	{
		EffectClass = CombatConfig->WhiteEffectClass;
		Duration = CombatConfig->WhiteExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.MoveSpeedMultiplier"));
		Multiplier = CombatConfig->WhiteMoveSpeedMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(OrangeExtractTag()))
	{
		EffectClass = CombatConfig->OrangeEffectClass;
		Duration = CombatConfig->OrangeExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.DefenseMultiplier"));
		Multiplier = CombatConfig->OrangeDefenseMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(RedExtractTag()))
	{
		EffectClass = CombatConfig->RedEffectClass;
		Duration = CombatConfig->RedExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.AttackMultiplier"));
		Multiplier = CombatConfig->RedAttackMultiplier;
	}

	if (!EffectClass || Duration <= 0.f)
	{
		return false;
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		EffectClass, 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetDuration(Duration, true);
	Spec.Data->SetSetByCallerMagnitude(MultiplierTag, Multiplier);
	const FActiveGameplayEffectHandle NewHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	if (!IsHandleActive(NewHandle))
	{
		return false;
	}

	const FActiveGameplayEffectHandle OldHandle = ActiveExtractHandles.FindRef(ExtractColor);
	ActiveExtractHandles.Add(ExtractColor, NewHandle);
	if (FOnActiveGameplayEffectRemoved_Info* Delegate =
		ASC->OnGameplayEffectRemoved_InfoDelegate(NewHandle))
	{
		Delegate->AddUObject(this, &URes_InsectGlaive::HandleSingleExtractRemoved,
			ExtractColor, NewHandle);
	}
	// 新 Handle 确实 Active 后才删旧 Handle；Apply 失败会保留旧灯与剩余时间。
	if (IsHandleActive(OldHandle))
	{
		ASC->RemoveActiveGameplayEffect(OldHandle);
	}
	return true;
}

void URes_InsectGlaive::CheckAndActivateTripleUp()
{
	if (IsTripleUpActive() || !CombatConfig)
	{
		return;
	}
	if (!IsHandleActive(ActiveExtractHandles.FindRef(WhiteExtractTag()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(OrangeExtractTag()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(RedExtractTag())))
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC || !CombatConfig->TripleUpEffectClass)
	{
		return;
	}
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		CombatConfig->TripleUpEffectClass, 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}
	Spec.Data->SetDuration(CombatConfig->TripleUpDuration, true);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.AttackMultiplier")),
		CombatConfig->TripleAttackMultiplier);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.MoveSpeedMultiplier")),
		CombatConfig->TripleMoveSpeedMultiplier);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.DefenseMultiplier")),
		CombatConfig->TripleDefenseMultiplier);

	const FActiveGameplayEffectHandle NewTriple = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	if (!IsHandleActive(NewTriple))
	{
		return; // 三灯 Apply 失败：三个单灯完整保留。
	}

	bExtractTransitionGuard = true;
	TripleUpHandle = NewTriple;
	if (FOnActiveGameplayEffectRemoved_Info* Delegate =
		ASC->OnGameplayEffectRemoved_InfoDelegate(NewTriple))
	{
		Delegate->AddUObject(this, &URes_InsectGlaive::HandleTripleUpRemoved, NewTriple);
	}
	const TArray<FActiveGameplayEffectHandle> OldSingleHandles = {
		ActiveExtractHandles.FindRef(WhiteExtractTag()),
		ActiveExtractHandles.FindRef(OrangeExtractTag()),
		ActiveExtractHandles.FindRef(RedExtractTag())
	};
	ActiveExtractHandles.Reset();
	for (const FActiveGameplayEffectHandle& Handle : OldSingleHandles)
	{
		if (IsHandleActive(Handle))
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	bExtractTransitionGuard = false;
	PlayResourceSound(CombatConfig->TripleUpActivatedSound);
	OnTripleUpChanged.Broadcast();
	BroadcastExtractState();
}

bool URes_InsectGlaive::HasExtract(FGameplayTag ExtractType) const
{
	return IsHandleActive(ActiveExtractHandles.FindRef(ExtractType));
}

bool URes_InsectGlaive::IsTripleUpActive() const
{
	return IsHandleActive(TripleUpHandle);
}

bool URes_InsectGlaive::TryConsumeTripleUpAtomic()
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	const FActiveGameplayEffectHandle Handle = TripleUpHandle;
	return ASC && IsHandleActive(Handle) && ASC->RemoveActiveGameplayEffect(Handle);
}

bool URes_InsectGlaive::ConsumeExtract(FGameplayTag ExtractType)
{
	if (IsTripleUpActive())
	{
		return false;
	}
	UAbilitySystemComponent* ASC = GetPlayerASC();
	const FActiveGameplayEffectHandle Handle = ActiveExtractHandles.FindRef(ExtractType);
	return ASC && IsHandleActive(Handle) && ASC->RemoveActiveGameplayEffect(Handle);
}

void URes_InsectGlaive::HandleSingleExtractRemoved(
	const FGameplayEffectRemovalInfo& RemovalInfo, FGameplayTag Color,
	FActiveGameplayEffectHandle ExpectedHandle)
{
	(void)RemovalInfo;
	if (ActiveExtractHandles.FindRef(Color) == ExpectedHandle)
	{
		ActiveExtractHandles.Remove(Color);
		if (!bExtractTransitionGuard)
		{
			BroadcastExtractState();
		}
	}
}

void URes_InsectGlaive::HandleTripleUpRemoved(
	const FGameplayEffectRemovalInfo& RemovalInfo,
	FActiveGameplayEffectHandle ExpectedHandle)
{
	if (TripleUpHandle != ExpectedHandle)
	{
		return; // 旧 Handle 的迟到回调不能清后来建立的三灯。
	}
	TripleUpHandle = FActiveGameplayEffectHandle();
	TripleReservations.Reset();
	if (!bRuntimeShuttingDown && CombatConfig && !RemovalInfo.bPrematureRemoval)
	{
		PlayResourceSound(CombatConfig->TripleUpExpiredSound);
	}
	if (!bRuntimeShuttingDown)
	{
		OnTripleUpChanged.Broadcast();
		BroadcastExtractState();
	}
}

void URes_InsectGlaive::BroadcastExtractState()
{
	for (const FGameplayTag& Color : { WhiteExtractTag(), OrangeExtractTag(), RedExtractTag() })
	{
		float Ratio = 0.f;
		const FActiveGameplayEffectHandle Handle = ActiveExtractHandles.FindRef(Color);
		if (UAbilitySystemComponent* ASC = GetPlayerASC(); IsHandleActive(Handle) && ASC)
		{
			const FActiveGameplayEffect* ActiveEffect = ASC->GetActiveGameplayEffect(Handle);
			const float Duration = ActiveEffect ? ActiveEffect->GetDuration() : 0.f;
			const float Remaining = ActiveEffect && GetWorld()
				? ActiveEffect->GetTimeRemaining(GetWorld()->GetTimeSeconds()) : 0.f;
			Ratio = Duration > 0.f ? FMath::Clamp(Remaining / Duration, 0.f, 1.f) : 1.f;
		}
		OnExtractTimeUpdated.Broadcast(Color, Ratio);
	}
}

bool URes_InsectGlaive::ApplyKinsectDamage(
	const FHitResult& Hit, float MotionValue, const FGuid& HitInstanceID)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	AActor* Target = Hit.GetActor();
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	UAbilitySystemComponent* TargetASC = Target
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
	if (!Hitzone || !SourceASC || !TargetASC || !HitInstanceID.IsValid())
	{
		return false;
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	if (!Context)
	{
		return false;
	}
	Context->AddHitResult(Hit, true);
	// IncomingHitResolver 按 AttackInstanceID 全局去重；贯通同一 Flight 的每次
	// 有效伤害必须有独立 HitInstanceID，FlightInstanceID 只留在请求/调试域。
	Context->AttackInstanceID = HitInstanceID;
	Context->DamageSourceType = EMHGZDamageSourceType::Kinsect;
	Context->bUseHitzoneDefense = true;
	Context->HitzoneTag = Hitzone->HitzoneTag;
	Context->HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Kinsect"));
	if (AActor* Source = RuntimeContext.Character.Get())
	{
		Context->AddInstigator(Source, Source);
	}

	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UMHGZDamageGameplayEffect::StaticClass(), 1.f, ContextHandle);
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), MotionValue);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")),
		GetModifiedKinsectAttackPower());
	return SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC)
		.WasSuccessfullyApplied();
}

float URes_InsectGlaive::GetModifiedKinsectAttackPower() const
{
	return KinsectData ? FMath::Max(0.f, KinsectData->KinsectAttackPower) : 0.f;
}

bool URes_InsectGlaive::LaunchKinsectMark(const FWeaponAimSnapshot& AimSnapshot)
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld()
		|| AimSnapshot.Context != EWeaponAimSnapshotContext::Kinsect
		|| AimSnapshot.Direction.ContainsNaN()
		|| AimSnapshot.Direction.GetSafeNormal().IsNearlyZero())
	{
		return false;
	}

	TSubclassOf<AIGMarkProjectile> ProjectileClass = CombatConfig->KinsectMarkProjectileClass;
	if (!ProjectileClass)
	{
		ProjectileClass = AIGMarkProjectile::StaticClass();
	}

	FVector LaunchOrigin = AimSnapshot.Origin;
	FVector LaunchDirection = AimSnapshot.Direction.GetSafeNormal();
	if (!CombatConfig->KinsectMarkLaunchSocket.IsNone())
	{
		ACharacter* Character = RuntimeContext.Character.Get();
		TArray<USkeletalMeshComponent*> Meshes;
		if (Character)
		{
			Character->GetComponents<USkeletalMeshComponent>(Meshes);
		}
		USkeletalMeshComponent* LaunchMesh = nullptr;
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->ComponentHasTag(TEXT("WeaponTrace"))
				&& Mesh->DoesSocketExist(CombatConfig->KinsectMarkLaunchSocket))
			{
				LaunchMesh = Mesh;
				break;
			}
		}
		if (!LaunchMesh)
		{
			return false;
		}
		LaunchOrigin = LaunchMesh->GetSocketLocation(CombatConfig->KinsectMarkLaunchSocket);
		LaunchDirection = (AimSnapshot.TargetPoint - LaunchOrigin).GetSafeNormal();
		if (LaunchDirection.IsNearlyZero())
		{
			return false;
		}
	}
	FWeaponAimSnapshot LaunchSnapshot = AimSnapshot;
	LaunchSnapshot.Origin = LaunchOrigin;
	LaunchSnapshot.Direction = LaunchDirection;
	AIGMarkProjectile* Projectile = GetWorld()->SpawnActorDeferred<AIGMarkProjectile>(
		ProjectileClass,
		FTransform(LaunchDirection.Rotation(), LaunchOrigin),
		GetOwner(), Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return false;
	}
	Projectile->Initialize(this, RuntimeContext.RuntimeToken, LaunchSnapshot,
		CombatConfig->KinsectMarkProjectileSpeed,
		CombatConfig->KinsectMarkProjectileRadius,
		CombatConfig->KinsectMarkMaxDistance,
		CombatConfig->KinsectMarkProjectileLifetime);
	Projectile->FinishSpawning(FTransform(LaunchDirection.Rotation(), LaunchOrigin));
	return true;
}

bool URes_InsectGlaive::SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
	const FVector& ImpactPoint, AIGMarkProjectile* Projectile)
{
	if (bRuntimeShuttingDown || !Hitzone || !Projectile
		|| !IsValid(Hitzone->GetOwner()) || Projectile->GetOwner() != GetOwner())
	{
		return false;
	}
	ClearKinsectMark(EIGMarkClearReason::Replaced);
	ActiveMarkHitzone = Hitzone;
	ActiveMarkProjectile = Projectile;
	ActiveMarkLocalPoint = Hitzone->GetComponentTransform().InverseTransformPosition(ImpactPoint);
	SetMarkActiveTag(true);

	const uint64 ThisMarkSerial = ++MarkSerial;
	GetWorld()->GetTimerManager().SetTimer(MarkExpiryTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, ThisMarkSerial]()
		{
			if (ThisMarkSerial == MarkSerial)
			{
				ClearKinsectMark(EIGMarkClearReason::Expired);
			}
		}), FMath::Max(0.01f, CombatConfig->KinsectMarkDuration), false);
	return true;
}

void URes_InsectGlaive::ClearKinsectMark(EIGMarkClearReason Reason)
{
	(void)Reason;
	++MarkSerial;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
	}
	if (AIGMarkProjectile* Projectile = ActiveMarkProjectile.Get())
	{
		Projectile->Destroy();
	}
	ActiveMarkProjectile.Reset();
	ActiveMarkHitzone.Reset();
	ActiveMarkLocalPoint = FVector::ZeroVector;
	SetMarkActiveTag(false);
}

bool URes_InsectGlaive::HasValidKinsectMark() const
{
	return ActiveMarkHitzone.IsValid() && ActiveMarkProjectile.IsValid()
		&& IsValid(ActiveMarkHitzone->GetOwner());
}

bool URes_InsectGlaive::GetKinsectMarkWorldLocation(FVector& OutLocation) const
{
	if (!HasValidKinsectMark())
	{
		return false;
	}
	OutLocation = ActiveMarkHitzone->GetComponentTransform()
		.TransformPosition(ActiveMarkLocalPoint);
	return true;
}

bool URes_InsectGlaive::AreTripleCostSpecs(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	if (Specs.Num() != 1 || !Specs[0].CostType.MatchesTagExact(TripleCostTag()))
	{
		return false;
	}
	return FMath::IsNearlyEqual(Specs[0].Amount.GetValueAtLevel(1.f), 1.f);
}

bool URes_InsectGlaive::CanReserveCosts(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	return Specs.IsEmpty() || (AreTripleCostSpecs(Specs) && IsTripleUpActive());
}

bool URes_InsectGlaive::TryReserveCosts(const FWeaponActionToken& ActionToken,
	const TArray<FWeaponResourceCostSpec>& Specs,
	FWeaponResourceCostReservation& OutReservation)
{
	if (Specs.IsEmpty())
	{
		return Super::TryReserveCosts(ActionToken, Specs, OutReservation);
	}
	OutReservation = FWeaponResourceCostReservation();
	if (!ActionToken.IsValid() || !IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| !AreTripleCostSpecs(Specs) || !IsTripleUpActive())
	{
		return false;
	}
	uint64 ID = NextReservationID++;
	if (ID == 0)
	{
		ID = NextReservationID++;
	}
	TripleReservations.Add(ID, TripleUpHandle);
	OutReservation.RuntimeToken = ActionToken.RuntimeToken;
	OutReservation.ActivationSequenceID = ActionToken.ActivationSequenceID;
	OutReservation.ReservationID = ID;
	return true;
}

void URes_InsectGlaive::ReleaseReservation(
	const FWeaponResourceCostReservation& Reservation)
{
	TripleReservations.Remove(Reservation.ReservationID);
}

void URes_InsectGlaive::ConsumeReservedCosts(
	const FWeaponResourceCostReservation& Reservation)
{
	const FActiveGameplayEffectHandle ReservedHandle =
		TripleReservations.FindRef(Reservation.ReservationID);
	if (!ReservedHandle.IsValid() || ReservedHandle != TripleUpHandle)
	{
		TripleReservations.Remove(Reservation.ReservationID);
		return;
	}
	TripleReservations.Remove(Reservation.ReservationID);
	if (UAbilitySystemComponent* ASC = GetPlayerASC(); IsHandleActive(ReservedHandle) && ASC)
	{
		ASC->RemoveActiveGameplayEffect(ReservedHandle);
	}
}

void URes_InsectGlaive::SetKinsectActiveTag(bool bActive)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host)
	{
		return;
	}
	if (bActive && !KinsectActiveTagToken.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(KinsectActiveTag());
		KinsectActiveTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
			FGameplayAbilitySpecHandle(), 0, TEXT("IG.Kinsect.Active"), Tags);
	}
	else if (!bActive && KinsectActiveTagToken.IsValid())
	{
		Host->ReleaseTags(KinsectActiveTagToken);
		KinsectActiveTagToken = FWeaponOwnedTagToken();
	}
}

void URes_InsectGlaive::SetMarkActiveTag(bool bActive)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host)
	{
		return;
	}
	if (bActive && !MarkActiveTagToken.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(MarkActiveTag());
		MarkActiveTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
			FGameplayAbilitySpecHandle(), 0, TEXT("IG.Mark.Active"), Tags);
	}
	else if (!bActive && MarkActiveTagToken.IsValid())
	{
		Host->ReleaseTags(MarkActiveTagToken);
		MarkActiveTagToken = FWeaponOwnedTagToken();
	}
}

void URes_InsectGlaive::ClearAllResourceGameplayEffects()
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC)
	{
		ActiveExtractHandles.Reset();
		TripleUpHandle = FActiveGameplayEffectHandle();
		return;
	}
	bExtractTransitionGuard = true;
	TArray<FActiveGameplayEffectHandle> SingleHandles;
	ActiveExtractHandles.GenerateValueArray(SingleHandles);
	ActiveExtractHandles.Reset();
	for (const FActiveGameplayEffectHandle& Handle : SingleHandles)
	{
		if (IsHandleActive(Handle))
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	const FActiveGameplayEffectHandle TripleHandle = TripleUpHandle;
	TripleUpHandle = FActiveGameplayEffectHandle();
	if (IsHandleActive(TripleHandle))
	{
		ASC->RemoveActiveGameplayEffect(TripleHandle);
	}
	bExtractTransitionGuard = false;
}

void URes_InsectGlaive::ApplyEntryModifier(FGameplayTag AttributeTag, float Value,
	TEnumAsByte<EGameplayModOp::Type> Op)
{
	(void)AttributeTag;
	(void)Value;
	(void)Op;
	// Demo 冻结：该路径语义有缺陷，明确禁用，不允许悄悄修改猎虫数值。
}

void URes_InsectGlaive::ClearAllEntryModifiers()
{
	Super::ClearAllEntryModifiers();
}
