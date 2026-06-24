// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "MHGZDataManager.generated.h"

class UDataTable;
class UCurveTable;

/**
 * UMHGZDataManager — 全局 DataTable/CurveTable 集中管理
 * GameInstanceSubsystem，生命周期与 GameInstance 相同
 * 策划一处配置，所有系统通过 GetSubsystem 获取
 */
UCLASS()
class UMHGZDataManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ═══════════════════════════════════════════
	// DataTable 引用
	// ═══════════════════════════════════════════

	/** 词条目录 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> EntryCatalog;

	/** 武器→连招表映射 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> WeaponComboConfig;

	/** 武器→资源组件映射 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> WeaponResourceConfig;

	/** Ability 标量曲线表 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UCurveTable> AbilityScalars;

	/** 词条数值曲线表 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UCurveTable> EntryMagnitudes;

	/** 武器翻滚配置 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> DodgeConfig;

	// ═══════════════════════════════════════════
	// 查询接口
	// ═══════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "MHGZ|DataManager")
	static UMHGZDataManager* Get(const UObject* WorldContext);

	UDataTable* GetEntryCatalog() const;
	UDataTable* GetWeaponComboConfig() const;
	UDataTable* GetWeaponResourceConfig() const;
	UCurveTable* GetAbilityScalars() const;
};
