// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MHGZDamageNumberActor.generated.h"

class UMHGZDamageNumberWidget;
class AMHGZPlayerController;
class UWidgetComponent;

/** 有界生命周期的世界空间伤害数字载体；不参与结算。 */
UCLASS(Blueprintable)
class MHGZ_API AMHGZDamageNumberActor : public AActor
{
	GENERATED_BODY()

public:
	AMHGZDamageNumberActor();
	bool InitializeDamageNumber(float ActualDamage, bool bCritical,
		AMHGZPlayerController* TelemetryController, uint64 RuntimeGeneration);
	UMHGZDamageNumberWidget* GetDamageNumberWidget() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	void HandleLifetimeExpired();

	/** 承载伤害数字界面的世界空间Widget组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Feedback")
	TObjectPtr<UWidgetComponent> DamageWidgetComponent;

	/** 该伤害数字角色创建的界面控件类。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	TSubclassOf<UMHGZDamageNumberWidget> DamageWidgetClass;

	/** 伤害数字从显示到自动回收的总时长，单位为秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback",
		meta = (ClampMin = "0.05"))
	float DisplayLifetime = 1.0f;

	/** 消失前的淡出时长；此前保持原地、不透明。包含在DisplayLifetime内。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback",
		meta = (ClampMin = "0.01"))
	float FadeOutDuration = 0.25f;

	/** 仅淡出阶段使用的上飘速度（cm/s）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	float RiseSpeed = 60.0f;

	TWeakObjectPtr<AMHGZPlayerController> TelemetryController;
	uint64 TelemetryRuntimeGeneration = 0;
	FTimerHandle LifetimeTimerHandle;
	bool bLifetimeExpired = false;
	float DisplayAge = 0.0f;
};
