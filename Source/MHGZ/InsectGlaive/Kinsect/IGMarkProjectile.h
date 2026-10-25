// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "MHGZWeaponRuntimeTypes.h"
#include "IGMarkProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class URes_InsectGlaive;
class UMHGZMonsterHitzoneComponent;

/**
 * AIGMarkProjectile — 虫印投射物。
 * Sphere Root + ProjectileMovement；物理仅 Block WorldStatic，不 Overlap Hitzone；
 * 命中 Hitzone 由代码 Sphere Sweep（Previous→Current，按 Time 取最早）判定，
 * 成功后由 Resource 收编为持续虫印 Actor。
 * 禁止裸定时器跨 Runtime；每帧核对 RuntimeToken，失效即销毁。
 */
UCLASS()
class AIGMarkProjectile : public AActor
{
	GENERATED_BODY()

public:
	AIGMarkProjectile();

	virtual void Tick(float DeltaTime) override;

	/** 以瞄准快照的方向初始化并起飞。 */
	void Initialize(URes_InsectGlaive* InResource, const FWeaponRuntimeToken& InRuntimeToken,
		const FWeaponAimSnapshot& AimSnapshot, float InSpeed, float InRadius,
		float InMaxDistance, float InLifetime);

	/** 以起点方向初始化并起飞。 */
	void Initialize(URes_InsectGlaive* InResource, const FWeaponRuntimeToken& InRuntimeToken,
		const FVector& StartDirection, float InSpeed, float InRadius,
		float InMaxDistance, float InLifetime);

	/** 是否已成为持续虫印（已成功附着到 Hitzone）。 */
	bool IsAttachedMark() const { return bIsAttachedMark; }

protected:
	virtual void BeginPlay() override;

	/** 世界碰撞回调——物理仅 Block WorldStatic；缓存后由 Tick 销毁。 */
	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	void InitializeInternal(URes_InsectGlaive* InResource,
		const FWeaponRuntimeToken& InRuntimeToken, const FVector& Direction,
		float InSpeed, float InRadius, float InMaxDistance, float InLifetime);

	/** 每帧 Previous→Current Sphere Sweep Hitzone Object，按 Time 取最早命中。 */
	void SweepHitzones();

	/** 虫印弹用于移动和接触查询的球形根组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IGMark|Components",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> Sphere;

	/** 虫印弹的投射物移动组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IGMark|Components",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProjectileMovementComponent> Movement;

	/** 拥有该虫印弹并负责施加虫印的虫棍资源组件。 */
	UPROPERTY()
	TWeakObjectPtr<URes_InsectGlaive> Resource;

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	FVector FlightStartLocation = FVector::ZeroVector;
	FVector LastTickLocation = FVector::ZeroVector;
	float MaxDistance = 0.f;
	float Lifetime = 0.f;
	float ElapsedTime = 0.f;
	bool bInitialized = false;
	bool bIsAttachedMark = false;
	bool bBlockedByWorld = false;
};
