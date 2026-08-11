// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "MHGZ.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "GameplayEffect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& GetAttackPowerSetByCallerTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower"));
	return Tag;
}

const FGameplayTag& GetMotionValueSetByCallerTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue"));
	return Tag;
}

const FGameplayTag& GetBaseStaggerSetByCallerTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger"));
	return Tag;
}

}

UMHGZIncomingHitResolverComponent::UMHGZIncomingHitResolverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

EIncomingHitSubmitResult UMHGZIncomingHitResolverComponent::SubmitIncomingHit(
	const FIncomingHitContext& Context)
{
	if (!Context.IsValidAttack())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[IncomingHit] Rejected: invalid ID or unreal hit (Source=%s)"),
			*GetNameSafe(Context.SourceActor.Get()));
		return EIncomingHitSubmitResult::Rejected;
	}
	if (Context.Hit.GetActor() != GetOwner())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[IncomingHit] Rejected: Hit actor does not match resolver owner"));
		return EIncomingHitSubmitResult::Rejected;
	}

	PruneExpiredState();

	// Authoritative dedupe: one AttackInstanceID settles at most once (Applied or Consumed).
	if (HasProcessedAttack(Context.AttackInstanceID))
	{
		return EIncomingHitSubmitResult::Duplicate;
	}

	// Only bCounterable hits enter the counter chain; higher Priority is asked first.
	if (Context.bCounterable)
	{
		for (const FInterceptorEntry& Entry : Interceptors)
		{
			if (!IsInterceptorUsable(Entry))
			{
				continue;
			}

			EIncomingHitInterceptResult InterceptResult = EIncomingHitInterceptResult::Pass;
			if (Entry.Callback)
			{
				InterceptResult = Entry.Callback(Context);
			}
			if (InterceptResult == EIncomingHitInterceptResult::Consume)
			{
				RecordProcessed(Context.AttackInstanceID, EIncomingHitSubmitResult::Consumed);
				UE_LOG(LogMHGZ, Log,
					TEXT("[IncomingHit] Consumed AttackInstance=%s by Token=%lld (Source=%s)"),
					*Context.AttackInstanceID.ToString(), Entry.TokenID,
					*GetNameSafe(Context.SourceActor.Get()));
				return EIncomingHitSubmitResult::Consumed;
			}
		}
	}

	// Pass: reserve the dedupe slot first, then apply; roll back on failure so the
	// same ID can be retried and no half state remains.
	RecordProcessed(Context.AttackInstanceID, EIncomingHitSubmitResult::Applied);
	if (!ApplyDamageToPlayer(Context))
	{
		ProcessedAttacks.Remove(Context.AttackInstanceID);
		UE_LOG(LogMHGZ, Warning,
			TEXT("[IncomingHit] Rejected: damage apply failed (AttackInstance=%s)"),
			*Context.AttackInstanceID.ToString());
		return EIncomingHitSubmitResult::Rejected;
	}

	UE_LOG(LogMHGZ, Log,
		TEXT("[IncomingHit] Applied AttackInstance=%s Damage=%.2f (Source=%s)"),
		*Context.AttackInstanceID.ToString(), Context.Damage,
		*GetNameSafe(Context.SourceActor.Get()));
	return EIncomingHitSubmitResult::Applied;
}

int64 UMHGZIncomingHitResolverComponent::RegisterInterceptor(
	const FWeaponActionToken& ActionToken,
	int32 Priority,
	float TTLSeconds,
	const FMHGZIncomingHitInterceptSignature& ConsumedCallback)
{
	FMHGZIncomingHitInterceptSignature Callback = ConsumedCallback;
	return RegisterInterceptorNative(ActionToken, Priority, TTLSeconds,
		[Callback](const FIncomingHitContext& Context) -> EIncomingHitInterceptResult
		{
			return Callback.IsBound()
				? Callback.Execute(Context)
				: EIncomingHitInterceptResult::Pass;
		});
}

int64 UMHGZIncomingHitResolverComponent::RegisterInterceptorNative(
	const FWeaponActionToken& ActionToken,
	int32 Priority,
	float TTLSeconds,
	FMHGZIncomingHitInterceptNative Callback)
{
	AActor* OwnerActor = GetOwner();
	if (!ActionToken.IsValid())
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[IncomingHit] Interceptor rejected: invalid ActionToken"));
		return 0;
	}
	UMHGZWeaponRuntimeHostComponent* Host = OwnerActor
		? OwnerActor->FindComponentByClass<UMHGZWeaponRuntimeHostComponent>() : nullptr;
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[IncomingHit] Interceptor rejected: missing Host or stale RuntimeToken (Generation=%llu)"),
			ActionToken.RuntimeToken.Generation);
		return 0;
	}
	if (!Callback)
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[IncomingHit] Interceptor rejected: no callback bound"));
		return 0;
	}

	FInterceptorEntry Entry;
	Entry.TokenID = NextTokenID++;
	const int64 RegisteredTokenID = Entry.TokenID;
	Entry.ActionToken = ActionToken;
	Entry.Priority = Priority;
	Entry.ExpireTime = GetCurrentTime() + FMath::Max(0.f, TTLSeconds);
	Entry.Callback = MoveTemp(Callback);
	Interceptors.Add(MoveTemp(Entry));
	Interceptors.Sort([](const FInterceptorEntry& A, const FInterceptorEntry& B)
	{
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.TokenID < B.TokenID;
	});
	return RegisteredTokenID;
}

bool UMHGZIncomingHitResolverComponent::UnregisterInterceptor(int64 TokenID)
{
	const int32 Removed = Interceptors.RemoveAll(
		[TokenID](const FInterceptorEntry& Entry) { return Entry.TokenID == TokenID; });
	return Removed > 0;
}

void UMHGZIncomingHitResolverComponent::UnregisterAllInterceptors()
{
	Interceptors.Reset();
}

int32 UMHGZIncomingHitResolverComponent::GetActiveInterceptorCount() const
{
	return Interceptors.Num();
}

bool UMHGZIncomingHitResolverComponent::HasProcessedAttack(const FGuid& AttackInstanceID) const
{
	const FProcessedAttackEntry* Entry = ProcessedAttacks.Find(AttackInstanceID);
	return Entry && Entry->ExpireTime > GetCurrentTime();
}

int32 UMHGZIncomingHitResolverComponent::GetProcessedAttackCount() const
{
	return ProcessedAttacks.Num();
}

void UMHGZIncomingHitResolverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAllInterceptors();
	ProcessedAttacks.Reset();
	Super::EndPlay(EndPlayReason);
}

float UMHGZIncomingHitResolverComponent::GetCurrentTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void UMHGZIncomingHitResolverComponent::PruneExpiredState()
{
	const float Now = GetCurrentTime();
	for (auto It = ProcessedAttacks.CreateIterator(); It; ++It)
	{
		if (It->Value.ExpireTime <= Now)
		{
			It.RemoveCurrent();
		}
	}
	Interceptors.RemoveAll([this](const FInterceptorEntry& Entry)
	{
		return !IsInterceptorUsable(Entry);
	});
}

bool UMHGZIncomingHitResolverComponent::IsInterceptorUsable(const FInterceptorEntry& Entry) const
{
	if (Entry.ExpireTime <= GetCurrentTime())
	{
		return false;
	}
	if (!Entry.ActionToken.IsValid())
	{
		return false;
	}
	AActor* OwnerActor = GetOwner();
	UMHGZWeaponRuntimeHostComponent* Host = OwnerActor
		? OwnerActor->FindComponentByClass<UMHGZWeaponRuntimeHostComponent>() : nullptr;
	return Host && Host->IsTokenCurrent(Entry.ActionToken.RuntimeToken);
}

void UMHGZIncomingHitResolverComponent::RecordProcessed(
	const FGuid& AttackInstanceID, EIncomingHitSubmitResult Result)
{
	FProcessedAttackEntry Entry;
	Entry.ExpireTime = GetCurrentTime() + FMath::Max(0.f, DedupeTTLSeconds);
	Entry.Result = Result;
	ProcessedAttacks.Add(AttackInstanceID, Entry);

	if (ProcessedAttacks.Num() <= MaxDedupeEntries)
	{
		return;
	}

	// Capacity: prune expired first; if still over, evict the entry expiring soonest.
	PruneExpiredState();
	if (ProcessedAttacks.Num() > MaxDedupeEntries)
	{
		FGuid OldestKey;
		float OldestExpire = TNumericLimits<float>::Max();
		for (const TPair<FGuid, FProcessedAttackEntry>& Pair : ProcessedAttacks)
		{
			if (Pair.Value.ExpireTime < OldestExpire)
			{
				OldestExpire = Pair.Value.ExpireTime;
				OldestKey = Pair.Key;
			}
		}
		ProcessedAttacks.Remove(OldestKey);
	}
}

bool UMHGZIncomingHitResolverComponent::ApplyDamageToPlayer(const FIncomingHitContext& Context)
{
	AActor* OwnerActor = GetOwner();
	UAbilitySystemComponent* TargetASC = nullptr;
	if (IAbilitySystemInterface* TargetASI = Cast<IAbilitySystemInterface>(OwnerActor))
	{
		TargetASC = TargetASI->GetAbilitySystemComponent();
	}
	if (!TargetASC && OwnerActor)
	{
		TargetASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>();
	}
	if (!TargetASC)
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[IncomingHit] Apply failed: target has no ASC"));
		return false;
	}

	AActor* SourceActor = Context.SourceActor.Get();
	UAbilitySystemComponent* SourceASC = nullptr;
	if (IAbilitySystemInterface* SourceASI = Cast<IAbilitySystemInterface>(SourceActor))
	{
		SourceASC = SourceASI->GetAbilitySystemComponent();
	}
	if (!SourceASC && SourceActor)
	{
		SourceASC = SourceActor->FindComponentByClass<UAbilitySystemComponent>();
	}

	// Build the context through the source ASC when available; otherwise fall back to
	// the target ASC (damage still comes from the SetByCaller override).
	// UE5.6 MakeEffectContext() is parameterless; instigator is set on the context below.
	UAbilitySystemComponent* ContextOwnerASC = SourceASC ? SourceASC : TargetASC;
	FGameplayEffectContextHandle ContextHandle = ContextOwnerASC->MakeEffectContext();
	FMHGZGameplayEffectContext* MHGZContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	if (!MHGZContext)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[IncomingHit] Apply failed: context is not FMHGZGameplayEffectContext"));
		return false;
	}

	MHGZContext->AddInstigator(SourceActor, SourceActor);
	MHGZContext->AddHitResult(Context.Hit, true);
	MHGZContext->AttackInstanceID = Context.AttackInstanceID;
	MHGZContext->SourceActionTag = Context.SourceAttackTag;
	MHGZContext->DamageSourceType = EMHGZDamageSourceType::DummyAttack;
	MHGZContext->HitStaggerTag = Context.StaggerTag;
	if (UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Context.Hit.GetComponent()))
	{
		MHGZContext->HitzoneTag = Hitzone->HitzoneTag;
	}

	FGameplayEffectSpec Spec(GetDefault<UMHGZDamageGameplayEffect>(), ContextHandle, 1.f);
	Spec.SetSetByCallerMagnitude(GetAttackPowerSetByCallerTag(), Context.Damage);
	Spec.SetSetByCallerMagnitude(GetMotionValueSetByCallerTag(), 1.f);
	Spec.SetSetByCallerMagnitude(GetBaseStaggerSetByCallerTag(), FMath::Max(0.f, DefaultBaseStagger));

	const FActiveGameplayEffectHandle Handle = SourceASC
		? SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC)
		: TargetASC->ApplyGameplayEffectSpecToSelf(Spec);
	return Handle.WasSuccessfullyApplied();
}
