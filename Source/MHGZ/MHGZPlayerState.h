// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "MHGZPlayerState.generated.h"

class UMHGZAbilitySystemComponent;
class UMHGZAttributeSet;
class UMHGZEquipmentComponent;
class UMHGZBackpackComponent;
class UMHGZWarehouseComponent;

/**
 * AMHGZPlayerState
 * - ASC 挂载于此（跨 Character 生命周期）
 * - 装备/背包/仓库等持久数据组件挂载于此
 * - Pawn 世界运行时（武器 Resource、猎虫、动作）由 Character RuntimeHost 持有
 * - PlayerState 自身不需要 Tick
 */
UCLASS()
class AMHGZPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMHGZPlayerState();

	// ── IAbilitySystemInterface ──
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ── 子组件访问 ──
	UFUNCTION(BlueprintCallable, Category = "MHGZ|PlayerState")
	UMHGZAbilitySystemComponent* GetMHGZAbilitySystemComponent() const { return AbilitySystemComponent; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|PlayerState")
	UMHGZAttributeSet* GetAttributeSet() const { return AttributeSet; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|PlayerState")
	UMHGZEquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|PlayerState")
	UMHGZBackpackComponent* GetBackpackComponent() const { return BackpackComponent; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|PlayerState")
	UMHGZWarehouseComponent* GetWarehouseComponent() const { return WarehouseComponent; }

protected:
	virtual void BeginPlay() override;

	// ── 组件 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZAttributeSet> AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZEquipmentComponent> EquipmentComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZBackpackComponent> BackpackComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Components")
	TObjectPtr<UMHGZWarehouseComponent> WarehouseComponent;
};
