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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Feedback")
	TObjectPtr<UWidgetComponent> DamageWidgetComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	TSubclassOf<UMHGZDamageNumberWidget> DamageWidgetClass;

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
