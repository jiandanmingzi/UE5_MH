// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZHitStopControllerComponent.generated.h"

/**
 * 可叠加卡肉（HitStop）控制器。
 *
 * 唯一 Token 管理重叠请求：首次请求保存 Owner 原 CustomTimeDilation 并应用
 * HitStopTimeDilation；任一请求到期/释放时若还有其他请求存活则不恢复；
 * 只有最后一个请求结束时恢复原值。EndPlay/ClearAll 幂等。
 * Ability/反馈路径不得直接写 CustomTimeDilation。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UMHGZHitStopControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZHitStopControllerComponent();

	/** 提交一个卡肉请求；Duration<=0 或组件正在 EndPlay 时返回 0（失败）。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|HitStop")
	int64 RequestHitStop(float Duration);

	/** 手动释放指定 Token；幂等，未知 Token 无效果。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|HitStop")
	void ReleaseHitStop(int64 Token);

	/** 清空全部请求并恢复原 Dilation；幂等。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|HitStop")
	void ClearAll();

	// ── 最小 C++ 测试查询/释放面 ──
	UFUNCTION(BlueprintPure, Category = "MHGZ|HitStop|Test")
	int32 GetActiveRequestCount() const { return ActiveRequests.Num(); }

	UFUNCTION(BlueprintPure, Category = "MHGZ|HitStop|Test")
	bool IsHitStopActive() const { return bAppliedDilation; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|HitStop|Test")
	float GetCurrentTimeDilation() const;

	/** 卡肉期间的全局时间倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|HitStop",
		meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float HitStopTimeDilation = 0.05f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FHitStopRequest
	{
		float EndTime = 0.f;
	};

	void TryRestoreDilationIfEmpty();

	TMap<int64, FHitStopRequest> ActiveRequests;
	int64 NextToken = 1;
	float OriginalTimeDilation = 1.f;
	bool bAppliedDilation = false;
	bool bIsEnding = false;
};
