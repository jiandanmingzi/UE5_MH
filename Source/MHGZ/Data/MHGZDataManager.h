// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "MHGZDataManager.generated.h"

class UDataTable;
class UCurveTable;

/**
 * UMHGZDataManager —— 全局 DataTable/CurveTable 集中管理
 * GameInstanceSubsystem，生命周期与 GameInstance 相同
 * 策画一处配置，所有系统通过 GetSubsystem 获取
 */
UCLASS(Config = Game, DefaultConfig)
class UMHGZDataManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ----------------------------------------------------------------------
	// DataTable 引用
	// ----------------------------------------------------------------------
	/** 词条目录 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> EntryCatalog;

	/** Ability 标量曲线表 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UCurveTable> AbilityScalars;

	/** 词条数值曲线表 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UCurveTable> EntryMagnitudes;

	/** 武器翻滚配置 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Data")
	TSoftObjectPtr<UDataTable> DodgeConfig;

	// ----------------------------------------------------------------------
	// 查询接口
	// ----------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "MHGZ|DataManager")
	static UMHGZDataManager* Get(const UObject* WorldContext);

	UDataTable* GetEntryCatalog() const;
	UCurveTable* GetAbilityScalars() const;
};