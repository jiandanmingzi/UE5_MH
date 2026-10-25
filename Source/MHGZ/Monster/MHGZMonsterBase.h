// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "MHGZMonsterBase.generated.h"

class UAbilitySystemComponent;
class UMHGZMonsterHitzoneComponent;
class UMHGZAttributeSet;
class UMHGZHitFeedbackRouterComponent;

/**
 * AMHGZMonsterBase — 怪物基类
 * 实现 IAbilitySystemInterface，骨骼上挂 HitzoneComponent
 */
UCLASS(Abstract)
class AMHGZMonsterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMHGZMonsterBase();

	// ── IAbilitySystemInterface ──
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Monster")
	UMHGZAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/** Read-only accessor for the settled-hit feedback router (parallel M2 domain). */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Monster")
	UMHGZHitFeedbackRouterComponent* GetHitFeedbackRouter() const { return HitFeedbackRouter; }

	/** 强制恢复所有部位通道 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Monster")
	void ForceRestoreAllChannels();

	/** 根据配置生成 Hitzone 碰撞体 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Monster")
	void GenerateHitzonesFromConfig(class UMHGZDummyConfig* Config);

	/**
	 * Resolve a gameplay hit point to the outer queryable visual-mesh surface.
	 * A dedicated trace channel keeps this presentation query separate from
	 * weapon, aim, and physical-blocking collision. If no mesh surface is queryable,
	 * the caller should retain the gameplay collision point. The direction-aware
	 * overload should be preferred when the incoming attack direction is available.
	 */
	virtual bool ResolveHitEffectVisualSurface(
		const UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& CollisionPoint,
		const FVector& CollisionNormal,
		FVector& OutVisualPoint,
		FVector& OutVisualNormal) const;
	/** Resolve using the direction from the impact back toward the incoming attack. */
	virtual bool ResolveHitEffectVisualSurface(
		const UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& CollisionPoint,
		const FVector& CollisionNormal,
		const FVector& PreferredOutwardDirection,
		FVector& OutVisualPoint,
		FVector& OutVisualNormal) const;

protected:
	virtual void BeginPlay() override;
	/** Enables only the dedicated mark-surface query on visible mesh components. */
	void InitializeHitEffectVisualSurfaceCollision();

	/** 角色的能力系统组件，负责能力、属性与效果的统一管理。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/** 角色当前的属性集合，保存生命、耐力及战斗数值。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZAttributeSet> AttributeSet;

	/** 承接属性结算结果并统一分发命中反馈的路由组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZHitFeedbackRouterComponent> HitFeedbackRouter;
};
