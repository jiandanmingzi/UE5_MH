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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZAttributeSet> AttributeSet;

	/** Settled-hit feedback router; lets the dummy route AttributeSet -> Router. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZHitFeedbackRouterComponent> HitFeedbackRouter;
};
