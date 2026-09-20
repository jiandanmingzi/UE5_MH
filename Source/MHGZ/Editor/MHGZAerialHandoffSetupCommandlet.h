// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MHGZAerialHandoffSetupCommandlet.generated.h"

/**
 * 把「最早可操作帧」写进三个 vault 蒙太奇，并清掉已退役的 IG_AerialWindow。
 *
 * 调用：
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=MHGZAerialHandoffSetup
 * 报告：Saved/_aerial_handoff.json
 *
 * ## 为什么是点通知
 *
 * 「最早可操作帧」是一个**帧**，不是一个区间。原来的 UAnimNotifyState_IG_AerialWindow
 * 用窗口表达它，代价是三样只服务于「窗口还开着吗」这个没人问的问题的东西：
 * begin/end 配对、窗口期间持有一个从没被查询过的 tag、以及按 NotifyEventID 记账的
 * token 表。真正的语义只有一句「这一帧之后胶囊从弧交给 CMC」，所以换成点通知。
 *
 * ## 时间从哪来
 *
 * 全部实测，来源是 MHR 实录 `mhrise_20260918_*`（5 份）。方法：在 `player_motion_old_id`
 * 链上找**被玩家取消**的段（后继 id 与自然结束不同），取其中的**最短**时长 ——
 * 那一段的可取消起点就是最早可操作帧。`146`/`158` 在每一份里都是固定 79 帧 /
 * 0.651 s，所以蒙太奇时间 = 0.650 + 被取消段时长。
 *
 * | 动作 | id 链 | 被取消段 | 最短 | 众数 | 蒙太奇时间 |
 * |---|---|---|---|---|---|
 * | 突进回旋斩→舞踏 | 160 → 154 → 137 | 154 | 0.316 s | 0.316（6/13） | AM_IG_WuTa **0.316** |
 * | 无白灯后撑杆跳 | 146 → 147 → 137 | 147 | 0.133 s | 0.250（3×） | AM_IG_HouChengGanTiao **0.783** |
 * | 白灯后撑杆跳 | 158 → 159 → 137 | 159 | 0.125 s | 0.125（5×） | AM_IG_HouChengGanTiao_W **0.775** |
 *
 * 无白灯的 0.133 是**孤点**（其上 0.250 出现 3 次），按「取最早」采纳；若 PIE 里
 * 表现为过早可操作，先怀疑这一个值。
 *
 * ## 为什么按类名字符串清理而不是 IsA
 *
 * 要让本命令列能在 `UAnimNotifyState_IG_AerialWindow` **被删除之后**仍然正确清理，
 * 过滤条件必须是字符串（类名 + 旧的 NotifyName），否则删类之后 `RemoveAll` 的谓词
 * 编译不过、或者依赖一个已不存在的类。顺带：也不能依赖 `LoadObject` 会保留那个事件 ——
 * 类没了之后导入可能直接丢掉它，所以**先跑本命令列、再删类**。
 */
UCLASS()
class UMHGZAerialHandoffSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;

	UMHGZAerialHandoffSetupCommandlet();
};
