// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IGCoAttackTypes.generated.h"

/** Ground attack opt-in; only explicitly mapped source segments gain independent damage. */
USTRUCT(BlueprintType)
struct FIGCoAttackSource
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (DisplayName = "启用地面协同"))
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack")
	FName MeshTag = TEXT("WeaponTrace");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack")
	FName Socket = TEXT("IG_FrontTip");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (Units = "cm"))
	FVector LocalOffset = FVector::ZeroVector;
	/** 源武器段索引→猎虫动作值。空表只演示轨迹，不复制武器动作值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "0.0"))
	TMap<int32, float> SegmentMotionValues;
};

USTRUCT(BlueprintType)
struct FIGCoAttackTuning
{
	GENERATED_BODY()
	/** 协同轨迹和攻击判定相对武器的统一延迟（游戏秒），不是固定帧数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "0.001", ClampMax = "1.0", Units = "s"))
	float TrajectoryDelaySeconds = .2f;
	/** 允许的最大采样间隔（秒）；超限终止旧轨迹，避免跨缺帧直接连成大范围攻击线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "0.01", ClampMax = "1.0", Units = "s"))
	float MaxSampleGapSeconds = .25f;
	/** 历史采样数量上限；满时安全终止，不删除仍待回放的合法采样。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "8", ClampMax = "2048"))
	int32 MaxSamples = 256;
	/** 是否绘制猎虫协同的调试轨迹与扫掠，不改变伤害或运动。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack")
	bool bDrawDebug = false;
	/** 最小球形判定半径；运行时还会覆盖猎虫可见包围球和原胶囊。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "1.0", Units = "cm"))
	float SweepRadius = 15.f;
	/** 单帧总查询片段上限；超限安全结束会话，不跨历史折线合并扫掠。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoAttack", meta = (ClampMin = "8", ClampMax = "4096"))
	int32 MaxSweepSegments = 256;

	bool IsValid() const
	{
		return FMath::IsFinite(TrajectoryDelaySeconds) && TrajectoryDelaySeconds > 0.f
			&& TrajectoryDelaySeconds <= 1.f && FMath::IsFinite(MaxSampleGapSeconds)
			&& MaxSampleGapSeconds >= .01f && MaxSampleGapSeconds <= 1.f
			&& MaxSamples >= 8 && MaxSamples <= 2048
			&& FMath::IsFinite(SweepRadius) && SweepRadius >= 1.f
			&& MaxSweepSegments >= 8 && MaxSweepSegments <= 4096;
	}
};

/** Bounded world-space history. No extrapolation and no frame-count delay. */
class FIGCoAttackHistory
{
public:
	struct FSample { double Time; FTransform Pose; };
	bool Add(double Time, const FTransform& Pose, const FIGCoAttackTuning& Tuning,
		TOptional<double> RetainFrom = {});
	bool Evaluate(double Time, FTransform& OutPose) const;
	bool GetPath(double Begin, double End, TArray<FSample>& OutPath) const;
	void Reset() { Samples.Reset(); }
	int32 Num() const { return Samples.Num(); }
	const FSample* First() const { return Samples.IsEmpty() ? nullptr : &Samples[0]; }
	const FSample* Last() const { return Samples.IsEmpty() ? nullptr : &Samples.Last(); }
private:
	TArray<FSample> Samples;
};

/** Immutable submission; target hit state is held separately by the Resource-owned sampler. */
struct FIGCoAttackWindowSpec
{
	uint64 Occurrence = 0;
	int32 Segment = INDEX_NONE;
	FGuid SourceAttackID;
	float MotionValue = 0.f;
	float AttackPower = 0.f;
	int32 MaxHits = 1;
	float HitInterval = .1f;
	double OpenTime = 0.;
};
