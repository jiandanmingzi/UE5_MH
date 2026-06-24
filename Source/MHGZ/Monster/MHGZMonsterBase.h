// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "MHGZMonsterBase.generated.h"

class UAbilitySystemComponent;
class UMHGZMonsterHitzoneComponent;

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
};
