// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZIncomingHitResolverComponent.generated.h"

class UAbilitySystemComponent;
class UMHGZWeaponRuntimeHostComponent;

/** Intercept result: Pass = continue into the unified damage chain; Consume = swallow this hit. */
UENUM(BlueprintType)
enum class EIncomingHitInterceptResult : uint8
{
	Pass,
	Consume
};

/** Final classification of SubmitIncomingHit. */
UENUM(BlueprintType)
enum class EIncomingHitSubmitResult : uint8
{
	/** Deduplicated, passed interception, damage GE applied successfully. */
	Applied,
	/** Consumed by a counter token; no damage is settled. */
	Consumed,
	/** The same AttackInstanceID was already processed (Applied or Consumed). */
	Duplicate,
	/** Invalid context, or damage apply failed after all tokens; no half state, retry allowed. */
	Rejected
};

/**
 * Single incoming-hit context from a monster/dummy toward the player.
 * The attacker only submits real data; authoritative dedupe and counter consumption
 * are decided by the target Resolver.
 */
USTRUCT(BlueprintType)
struct FIncomingHitContext
{
	GENERATED_BODY()

	/** Unique attack-instance ID; the same ID settles at most once on the target side. */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	FGuid AttackInstanceID;

	/** Real hit result (bBlockingHit and Actor must be valid). */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	FHitResult Hit;

	/** Attacking actor (dummy/monster). */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	TWeakObjectPtr<AActor> SourceActor;

	/** Source attack tag (e.g. DummyAttack.Spin). */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	FGameplayTag SourceAttackTag;

	/** Whether counter tokens may consume this hit; false means interceptors are skipped. */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	bool bCounterable = false;

	/** Damage settled on the player when the hit is not consumed. */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	float Damage = 0.f;

	/** Stagger tag when not consumed (Combat.Stagger.*); invalid means no stagger. */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|IncomingHit")
	FGameplayTag StaggerTag;

	/** Pre-submit sanity check: valid ID, real hit, finite non-negative damage. */
	bool IsValidAttack() const
	{
		return AttackInstanceID.IsValid()
			&& Hit.bBlockingHit
			&& Hit.GetActor() != nullptr
			&& FMath::IsFinite(Damage)
			&& Damage >= 0.f;
	}
};

/** Blueprint-bindable counter callback: returning Consume swallows this hit. */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(
	EIncomingHitInterceptResult, FMHGZIncomingHitInterceptSignature,
	const FIncomingHitContext&, Context);

/** Native C++ counter callback (shares the same priority queue as the BP path). */
using FMHGZIncomingHitInterceptNative = TFunction<EIncomingHitInterceptResult(const FIncomingHitContext&)>;

/**
 * UMHGZIncomingHitResolverComponent - target-side IncomingHit pre-resolver.
 *
 * Frozen behavior:
 * 1. Validate ID/real hit first, then authoritative per-AttackInstanceID dedupe
 *    (Applied and Consumed both count as processed).
 * 2. Only bCounterable hits invoke counter tokens in Priority order (high first).
 *    Tokens bind FWeaponActionToken (AbilityHandle + ActivationSequence +
 *    RuntimeHost identity), carry a unique TokenID and TTL, and unregister
 *    idempotently; stale RuntimeTokens and expired tokens are pruned automatically.
 * 3. On Pass, build one UMHGZDamageGameplayEffect spec and apply it to the player
 *    ASC. The context carries the real Hit / AttackInstanceID / SourceActionTag /
 *    DummyAttack / HitStagger; SetByCaller uses Damage.AttackPower=Damage,
 *    Damage.MotionValue=1 and Damage.BaseStagger=DefaultBaseStagger. When the
 *    source has an ASC, it builds/applies through it. Any failure returns Rejected
 *    and rolls back the dedupe record: no half state.
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UMHGZIncomingHitResolverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZIncomingHitResolverComponent();

	/** Submit one hit; returns Applied/Consumed/Duplicate/Rejected. */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IncomingHit")
	EIncomingHitSubmitResult SubmitIncomingHit(const FIncomingHitContext& Context);

	/**
	 * Register a counter token (Blueprint callback version).
	 * The ActionToken must be fully valid; when the owner has a RuntimeHost the
	 * token must also match the current runtime identity. TTLSeconds <= 0 expires
	 * immediately. Returns a unique TokenID, or 0 on rejection.
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IncomingHit")
	int64 RegisterInterceptor(
		const FWeaponActionToken& ActionToken,
		int32 Priority,
		float TTLSeconds,
		const FMHGZIncomingHitInterceptSignature& ConsumedCallback);

	/** Register a counter token (native C++ callback version); same semantics as above. */
	int64 RegisterInterceptorNative(
		const FWeaponActionToken& ActionToken,
		int32 Priority,
		float TTLSeconds,
		FMHGZIncomingHitInterceptNative Callback);

	/** Unregister by TokenID; idempotent (a second unregister returns false). */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IncomingHit")
	bool UnregisterInterceptor(int64 TokenID);

	/** Unregister every counter token (called automatically on EndPlay). */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IncomingHit")
	void UnregisterAllInterceptors();

	/** Current interceptor count (entries not yet lazily pruned). */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IncomingHit")
	int32 GetActiveInterceptorCount() const;

	/** Whether this attack ID is in the dedupe cache (Applied/Consumed, unexpired). */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IncomingHit")
	bool HasProcessedAttack(const FGuid& AttackInstanceID) const;

	/** Current dedupe-cache entry count (test/diagnostic). */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IncomingHit")
	int32 GetProcessedAttackCount() const;

	/** Dedupe cache TTL in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|IncomingHit", meta = (ClampMin = "0.0"))
	float DedupeTTLSeconds = 5.0f;

	/** Dedupe cache capacity; when exceeded, expired entries are pruned first, then the oldest. */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|IncomingHit", meta = (ClampMin = "1"))
	int32 MaxDedupeEntries = 128;

	/** Default Damage.BaseStagger SetByCaller used on the Pass path. */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|IncomingHit", meta = (ClampMin = "0.0"))
	float DefaultBaseStagger = 10.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FProcessedAttackEntry
	{
		float ExpireTime = 0.f;
		EIncomingHitSubmitResult Result = EIncomingHitSubmitResult::Applied;
	};

	struct FInterceptorEntry
	{
		int64 TokenID = 0;
		FWeaponActionToken ActionToken;
		int32 Priority = 0;
		float ExpireTime = 0.f;
		FMHGZIncomingHitInterceptNative Callback;
	};

	float GetCurrentTime() const;

	void PruneExpiredState();

	bool IsInterceptorUsable(const FInterceptorEntry& Entry) const;

	void RecordProcessed(const FGuid& AttackInstanceID, EIncomingHitSubmitResult Result);

	/** Build and apply the player damage GE; true on success, and no state is left on failure. */
	bool ApplyDamageToPlayer(const FIncomingHitContext& Context);

	TMap<FGuid, FProcessedAttackEntry> ProcessedAttacks;

	/** Maintained in descending Priority, ascending TokenID order. */
	TArray<FInterceptorEntry> Interceptors;

	int64 NextTokenID = 1;
};
