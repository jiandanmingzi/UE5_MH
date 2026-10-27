// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** 只保留最近10游戏秒的实际输出伤害；固定除以10，不随首击时间改变分母。 */
struct FMHGZRecentDamageWindow
{
	static constexpr double DurationSeconds = 10.0;

	void Add(double Now, float Damage)
	{
		if (!FMath::IsFinite(Now) || !FMath::IsFinite(Damage) || Damage <= 0.f) return;
		Prune(Now);
		Samples.Add({Now, double(Damage)});
		Total += Damage;
	}

	double GetDPS(double Now)
	{
		if (!FMath::IsFinite(Now)) return 0.0;
		Prune(Now);
		return Total / DurationSeconds;
	}

	void Reset() { Samples.Reset(); First = 0; Total = 0.0; }

private:
	struct FSample { double Time; double Damage; };
	void Prune(double Now)
	{
		// 世界时间回退代表换场景/重置，不能把上一时间轴的伤害带入新统计。
		if (!Samples.IsEmpty() && Now < Samples.Last().Time) { Reset(); return; }
		while (First < Samples.Num() && Samples[First].Time <= Now - DurationSeconds)
		{
			Total -= Samples[First++].Damage;
		}
		if (First == Samples.Num()) { Reset(); }
		else if (First >= 128 && First * 2 >= Samples.Num())
		{
			Samples.RemoveAt(0, First, EAllowShrinking::No);
			First = 0;
		}
		Total = FMath::Max(Total, 0.0);
	}
	TArray<FSample> Samples;
	int32 First = 0;
	double Total = 0.0;
};
