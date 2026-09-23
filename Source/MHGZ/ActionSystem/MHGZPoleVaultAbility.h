// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZPoleVaultAbility.generated.h"

class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAbilityTask_MHGZWeaponMovement;
class UAnimMontage;
class UAnimSequenceBase;
class UCurveVector;

/** One point from the recorded local back-vault trajectory. */
USTRUCT(BlueprintType)
struct FVaultTrajectoryKey
{
	GENERATED_BODY()

	/** Normalized movement time, in the inclusive [0, 1] range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float Time = 0.0f;

	/**
	 * X = travel progress, Y = lateral offset divided by total travel distance,
	 * Z = height divided by measured apex height.  The full recording ends at
	 * ground level; the ability samples only its action-owned prefix, then CMC
	 * continues from that non-zero-height handoff.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	FVector NormalizedPosition = FVector::ZeroVector;
};

/**
 * 在 JumpOver 段内从 clip 自己的根骨骼轨道回收的**前向**位移。
 *
 * clip 的根骨骼在整段里偏离 ref pose，而该序列 `bEnableRootMotion` 与
 * `bForceRootLock` 都为假 —— 于是轨迹**既不提取也不重置**，那 23 cm 前向位移
 * 只活在姿势里，换坠落正片时被丢掉。现在给序列打开 `bForceRootLock` 把姿势
 * 前导消掉（否则它会以「一帧弹回」的形式重现），再由本表把同量的**前向**位移
 * 补回胶囊。
 *
 * 只补前向：Rise 实录用朝向分解后，侧向是「去—回—归零」的摆动，净位移为零，
 * 而曲线自己的横向已经就是这个形状；再叠姿势那份侧移，可见轨迹就回不了零。
 */
USTRUCT(BlueprintType)
struct FVaultClipDriftKey
{
	GENERATED_BODY()

	/** 在 CurvedVault 窗口内的归一化时间，闭区间 [0, 1]。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float CurveTime = 0.0f;

	/** 已回收的前向位移 ÷ TotalDistance。末端必须平（斜率为零）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float ForwardFraction = 0.0f;
};

/**
 * 一条序列的根轨道该处于什么状态。
 *
 * 两个极端都是错，且后果不同：既不锁也不提取会让位移静默地留在姿势里（A9）；
 * 既锁又提取则让动画根运动抢占 CurvedVault 的 RootMotionSource
 * （`CharacterMovementComponent.cpp` 里 `HasAnimRootMotion()` 直接 return），
 * 整条解析轨迹连同交棒切线一起失效。
 */
enum class EVaultRootTrackPolicy : uint8
{
	/** 既不提取也不重置：位移只活在姿势里，换正片时弹回。 */
	Unaccounted,
	/** 锁在 ref pose、不提取 —— JumpOver 段唯一可接受的状态。 */
	LockedNotExtracted,
	/** 提取、不锁 —— Jump 段的状态（该段没有 RootMotionSource 在跑）。 */
	Extracted,
	/** 既锁又提取：提取会抢占 CurvedVault，比 Unaccounted 更糟。 */
	Conflicting,
};

/**
 * 打一根撑杆跳变体的全部数据：**方向 × 灯态**。
 *
 * 「一个方向一条」而不是「两个灯态各一片平铺字段」，是因为四个方向的字段名本来
 * 就该一样 —— 平铺会得到 `ForwardVaultTrajectory` / `LeftVaultTrajectory` / …
 * 八套同构字段，而它们之间的差异需要**跨字段**才能校验（`ArcDuration ≥ VaultDuration`，
 * 以及非白灯的残余自由落体必须落在实测的 0.18~0.30 s 内）。收成一条一条才校验得了。
 */
USTRUCT(BlueprintType)
struct FVaultProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault")
	EDirectionalInput Direction = EDirectionalInput::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault")
	bool bWhite = false;

	/**
	 * 整条链的**弦向**减起跳朝向（度）—— `DirectionSnapshot` 的基准。
	 *
	 * 取整条链的弦向、**不是**起手段自己的方向：只有以弦向为基准，曲线末帧的侧向
	 * 分量才恒为零（参照系构造性质）。取 ±90 这类整数角会让末帧侧向从 0 变成 ~30 cm，
	 * 直接违反消费方「起止偏移为零」。后撑杆跳恰好是 180（向后 = 朝向取负）。
	 *
	 * ⚠ **符号约定：本字段「朝左为正」（MHR 约定），而 UE 的 `FRotator(0, Yaw, 0)`
	 * 正 Yaw 朝右 —— 消费时必须取负。** 见 `UMHGZPoleVaultAbility::StartBackVaultMovement`
	 * 里 `DirectionSnapshot` 那一段的长注释与实测判据。
	 *
	 * 值由 `Scripts/MHRise/build_vault_curves.py` 生成，逐字来自
	 * `docs/reference/撑杆跳曲线.md` 的「整条链弦向−朝向」列，**不要手改符号** ——
	 * 改了这里就等于改了那份文档的口径。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault")
	float ChordYawDegrees = 180.0f;

	/**
	 * Jump 段的蒙太奇时长；`JumpOver` 段 = 移动窗口 − 本值，由配置里的
	 * `VaultDuration` 减出来，**不单独存**。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault", meta = (ForceUnits = "s"))
	float JumpDuration = 0.650f;

	/**
	 * **弧段姿势**的时长（秒）—— 烘成弧段蒙太奇那一条的长度。
	 *
	 * ⚠ **不要**用 `VaultTuning::VaultDuration − JumpDuration` 推它。那两个是
	 * **不同的量**：`VaultDuration` 是**移动窗口**（解析曲线跑多久），这个是**姿势
	 * 长度**。前/左/右六组两者恰好相等，**后撑杆跳白灯不是** —— 它的姿势刻意比窗口
	 * 长（1.583333 vs 1.487），让落地把姿势砍断，这是 MHR 的行为，也是这条变体
	 * PIE 验证过的视觉时间线的组成部分。拿窗口去推会把它的弧段姿势压短 6.1%。
	 *
	 * 这个项目已经为「一个量兼两个语义」付过两次账（`BackVaultDuration` 同时当归一化
	 * 域与窗口 → A8；`WhiteBackVaultDuration` 被逼成剪辑长度以满足
	 * `HandoffProgress ≤ 1.0` → 路径被拉长 4.7%）。所以这里把它**单独存**。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault", meta = (ForceUnits = "s"))
	float JumpOverDuration = 1.0f;

	/** 起手段 clip（Jump 段）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> JumpSequence;

	/** 弧段 clip（JumpOver 段）。整段 `bForceRootLock=True`、`bEnableRootMotion=False`。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> JumpOverSequence;

	/** 实录轨迹，逐帧归一化后抽的 25 个关键帧。含下坠段，所以归一化域比移动窗口长。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Recorded Path")
	TArray<FVaultTrajectoryKey> Trajectory;

	/** 从 JumpOver clip 根轨道回收的前向位移。锁根轨道后实测为 0，机制保留。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Recorded Path")
	TArray<FVaultClipDriftKey> ClipDrift;
};

/** `MHGZ::VaultCurves::FMeta` 的引擎侧镜像（那边是 constexpr POD，拿不进 UPROPERTY）。 */
struct FVaultCurveMeta
{
	float ArcDuration = 0.0f;
	float VaultDuration = 0.0f;
	float Distance = 0.0f;
	float Apex = 0.0f;
	float ChordYawDegrees = 0.0f;
	FString Trial;
};

namespace MHGZ::VaultCurves
{
/** 下面三个都由手写的 `Generated/MHGZVaultCurveTables.cpp` 实现。 */

/** 把生成表里该变体的 25 个关键帧灌进 `Out`；方向/灯态不在表里返回 false。 */
bool BuildTrajectory(EDirectionalInput Direction, bool bWhite,
	TArray<FVaultTrajectoryKey>& Out);

/** 同上，漂移表（当前六组全为零，机制保留）。 */
bool BuildClipDrift(EDirectionalInput Direction, bool bWhite,
	TArray<FVaultClipDriftKey>& Out);

/** 取该变体的标量（时长 / 位移 / 顶点 / 弦向）。表里的行末点为试次标识。 */
bool FindMeta(EDirectionalInput Direction, bool bWhite, FVaultCurveMeta& Out);

/** 该 (方向, 灯态) 是否由生成表覆盖。后撑杆跳不在表里 —— 它是手抄的。 */
bool IsGenerated(EDirectionalInput Direction, bool bWhite);
} // namespace MHGZ::VaultCurves

/**
 * 撑杆跳的**共用机制**。前 / 左 / 右 / 后四个方向只差数据，不差流程。
 *
 * ⚠ **本类的内容是整体平移来的，读的时候请带上这条口径。**
 * 第 2 步把 `UMHGZBackVaultAbility` 的一千余行机制原封不动搬到这里，**一行未改**，
 * 所以正文里的注释、成员名（`BackVaultTrajectory` / `BackJumpSequence` /
 * `BackVaultVisualTask` …）以及日志文案**仍然是「后撑杆跳」口径**。
 * 这样第 2 步的验收标准是「后撑杆跳行为逐位不变」，而不是「看起来合理」。
 *
 * 它们会在后续步骤里逐个泛化：第 3 步把四向 × 灯态的标量收成 `TArray<FVaultProfile>`，
 * 第 4 步把方向基准从「取负朝向」推广成「朝向 + 弦向角」。
 * 成员名不改则已（那是蓝图已序列化的名字），改就必须配 redirector。
 *
 * ---- 平移前的类注释，逐字保留（它描述的行为此刻仍然成立）----
 *
 * RT+A 后撑杆跳。
 *
 * 这是一个完整的地面动作取消：Combo 边只会在来源攻击的精确
 * DodgeAcceptWindow 内命中；Ability 根据白灯选择姿势序列和实录轨迹。动画
 * 的 Jump 段保留导入的 Montage Root Motion；到 Jump_Over_Back 的精确段边界才
 * 由 CurvedVault RootMotionSource 接管。随后保留末端速度并交由 CMC 自由下落，
 * 避免有根运动的 Jump_Back 与无可靠根运动的 Jump_Over_Back 产生位移所有权断层。
 *
 */
/*
 * Abstract：这个类本身**没有任何曲线数据** —— `VaultProfiles` 由每个方向自己的
 * 构造灌（见 `BuildDirectionProfiles`）。直接拿它做 BP 只会得到一个永远激活失败的
 * 能力，所以在这里就挡住。四个方向各有一个薄子类。
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class MHGZ_API UMHGZPoleVaultAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZPoleVaultAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	// （旧 lead 段覆写已随 InputPolicy 退役 —— 锁 tag 从 Detect 起领，起手段在锁定期里。）

	/**
	 * Analytic tangent of a recorded back-vault trajectory at HandoffProgress,
	 * expressed in the path's facing frame (X forward, Y right, Z up) in cm/s.
	 *
	 * This is the value the free-fall hand-off must carry.  Reading it back from
	 * CMC->Velocity instead is what silently cost the fall two thirds of its
	 * vertical speed, because the CMC removes a finished MoveToForce source on
	 * its own movement update and the velocity has already collapsed to the
	 * source's final partial-frame remainder by then.
	 *
	 * Static and free of editor state so the hand-off contract is directly
	 * testable without a world or a live ability.
	 */
	static bool ComputeTrajectoryTangent(const TArray<FVaultTrajectoryKey>& Keys,
		float StartProgress, float HandoffProgress, float TotalDistance,
		float ApexHeight, float CurveDuration, FVector& OutVelocityLocal);

	/**
	 * 由序列的两个根运动开关判定它处于哪种状态。纯函数，便于直接测试。
	 */
	static EVaultRootTrackPolicy ResolveRootTrackPolicy(
		bool bEnableRootMotion, bool bForceRootLock);

	/**
	 * 本次移动的基准方向：把**冻结的输入朝向**绕 yaw 转过该变体的整条链弦向。
	 *
	 * 抽成独立纯函数是为了让**手性**成为可测试的契约 —— 这个符号已经写错过一次
	 * （左右撑杆跳互换），而它写错时后撑杆跳与向前撑杆跳**都看不出来**。
	 * `MHGZM5MovementTests.cpp` 里按「正弦向 = 角色的左侧」直接断言。
	 *
	 * ⚠ `ChordYawDegrees` 用 **MHR / 文档约定（朝左为正）**，而 UE 的
	 * `FRotator(0, Yaw, 0)` 正 Yaw 朝右 ⇒ 这里必须**减**。
	 */
	static FVector ComputeDirectionSnapshot(const FVector& FrozenActorForward,
		float ChordYawDegrees);

	/**
	 * 在 CurvedVault 窗口的归一化时间处采样已回收的前向位移（÷ TotalDistance）。
	 *
	 * 表为空时返回 0（合法：没有漂移要补）。形状非法 —— 首键非零、末键不为 1、
	 * 时间不单调、含 NaN —— 返回 false，让调用方像其他退化输入一样拒绝这次移动。
	 */
	static bool SampleClipDriftForwardFraction(
		const TArray<FVaultClipDriftKey>& DriftKeys, float CurveTime,
		float& OutFraction);

	/** Authored White Extract counterpart of AttackMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation")
	TObjectPtr<UAnimMontage> WhiteAttackMontage;

	/**
	 * 弧段（JumpOver）那一条蒙太奇。与 `AttackMontage` 同灯态，方向由 GA 子类决定。
	 *
	 * **为什么撑杆跳是两条蒙太奇而不是一条两段式。** `FAnimTrack::GetAnimationPose`
	 * 用 `GetSegmentAtTime` 只取**一个**段，`ValidateSegmentTimes` 又把段的 `StartPos`
	 * 重新首尾相接 —— 蒙太奇内部段与段之间永远是**硬切，且无法做重叠**。于是接缝
	 * 就是「起手段末帧姿势」紧接「弧段首帧姿势」，逐骨量出来是弧段自己相邻一帧
	 * 落差的 1.4×~4.3×（白灯左/右最差，因为它们复用无白灯的起手段 `144`/`145`，
	 * 而那两条是为弧段 `142` 授权的 —— 这一点忠于原作，原作靠混合糊过去）。
	 *
	 * 拆开之后，播这一条时会走
	 * `UAnimInstance::Montage_PlayInternal` → `StopAllMontagesByGroupName(Group,
	 * BlendInSettings)`，**入场蒙太奇的 blend-in 设置被同时用来把起手段按同样时长
	 * 淡出**，那就是一次真正的交叉淡入 —— 不需要任何新机制。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation")
	TObjectPtr<UAnimMontage> ArcMontage;

	/** Authored White Extract counterpart of ArcMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation")
	TObjectPtr<UAnimMontage> WhiteArcMontage;

	/**
	 * 起手段 → 弧段的交叉淡入时长（秒）。
	 *
	 * **0.1334 s 是扫出来的拐点，不是拍的。**
	 * `Scripts/AssetProbe/probe_vault_seam_blend.py`（倒姿势网格）+
	 * `Saved/_mhr_scratch/analyze_vault_seam_blend.py`（离线判据）把 8 组 ×
	 * 4 种帧率 × 8 个候选时长扫了一遍，判据是**逐帧局部**的：
	 *
	 *     r(k) = 淡入第 k 帧的逐骨角度增量 / 纯弧段第 k 帧的同一量
	 *     弹跳 = max r(k)      代价 = min r(k)      总偏差 = max|r(k) - 1|
	 *
	 * 拐点在 0.1334 s，且 **30 / 40 / 60 / 120 fps 下都在同一点**：
	 *
	 *     B(s)     弹跳max   代价min   总偏差max   拖拽(deg·s)
	 *     硬切      2.24        —        1.24        —
	 *     0.10      1.98      0.40       0.98        0.9
	 *     0.1334    1.58      0.30       0.70        1.2
	 *     0.2668    1.88      0.15       0.88        2.5
	 *
	 * 三件必须一起记的事：
	 *
	 * 1. **再长就变差**（0.20/0.2668 的弹跳回升）。原因是 nlerp 在 α≈0.5 处角速度
	 *    不均匀（引擎的合成是 `AccumulateWithShortestRotation`，**不是 slerp**），
	 *    淡入越长，中点越容易落在弧段的快速段里，反而蹦一下。
	 * 2. **代价是真实的**：淡入期间姿势滞后于真弧段（拖拽 1.2 deg·s），
	 *    开头那一帧只跑到应有速度的 0.30 倍 —— 弧段起手有一瞬间的迟滞。
	 * 3. **对本来就对齐的组合是负收益**：白灯·前 1.09→1.24、白灯·后 0.85→1.24
	 *    （硬切落差本来就小于一帧正常运动量，摊开只会多走一段路）。这两组
	 *    仍在 1.24 以内，远低于最差的 1.58，所以没有做逐变体常量。
	 *
	 * 复跑：先 `probe_vault_seam_blend.py`，再离线分析器。
	 * 另有 `probe_vault_seam_pose.py` 量**硬切**基线（改动前的对照）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation",
		meta = (ForceUnits = "s"))
	float VaultSeamBlendTime = 0.1334f;

	/**
	 * 交棒的**提前量**（秒）：`WaitDelay` 从 `JumpDuration` 里减掉它。
	 *
	 * **默认 0 = 今天的既有行为**，这是一个**待标定的旋钮**，不是拍出来的数。
	 * 它要解决的是：`WaitDelay` 在能力 tick 到期、而胶囊位移在 CMC 的 movement
	 * tick 累加（同帧内更早），所以源第一次推动胶囊要等下一帧 —— 那一帧里起手段
	 * 剪辑的根位移已经走完，姿势还在动、**胶囊完全不动**（白灯左跳 15 次录制里
	 * 源的首帧落在蒙太奇 0.675/0.700/0.725，±1 帧）。
	 *
	 * ⚠ 标定它要同时看两件事：`[VaultHandoff]` 日志里源的首次累加落在哪一帧，
	 * 以及六组距离是否还在 MHR 的 ±4% 内 —— 提前量会连同姿势把整条弧一起前移，
	 * 并让起手段少走尾部那么多帧的根位移（那一带竖直速度 ~11 cm/帧）。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Animation",
		meta = (ForceUnits = "s"))
	float VaultHandoffLeadSeconds = 0.0f;

	/**
	 * 四个方向 × 两种灯态的**全部数据**，一个方向一条。
	 *
	 * 构造里灌默认值：后撑杆跳来自本文件的手抄表，前/左/右来自
	 * `Generated/MHGZVaultCurveTables.h`（由 `Scripts/MHRise/build_vault_curves.py`
	 * 从 MHR 实机录制生成）。蓝图可覆盖任意一条。
	 *
	 * ⚠ **方向是这一次 RTA 的属性，不是第二个输入入口** —— `InputTag` 仍然只有
	 * `Input.Weapon.RTA` 一个。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Data")
	TArray<FVaultProfile> VaultProfiles;

	/**
	 * 本次激活的变体数据；灯态由 `bUseWhiteBackVault` 决定。
	 *
	 * **方向不在这里判 —— 它是这个类的身份。** 每个方向一个 GA 蓝图，
	 * `DA_IG_Combo` 的边按摇杆方向选中对应那个；所以「空摇杆 RT+A 走前推版本」
	 * 是数据层的事（加一条 `Direction=None` 的兜底边），C++ 不需要特例。
	 *
	 * 找不到返回空，调用方**必须显式拒绝**（拒激活或拒移动）。不能退化成某个
	 * 默认方向：那会表现成「朝一个没配数据的方向跳了出去」，比不出招难查得多。
	 */
	const FVaultProfile* ResolveVaultProfile() const;

	/**
	 * `VaultProfiles` 共同声明的方向。
	 *
	 * 各条不一致、或数组为空时返回 `None` —— 那是一个**配置错误**，解析处会
	 * 记一条 Error 并拒绝激活，而不是随便挑一条的方向往下跑。
	 */
	EDirectionalInput GetVaultDirection() const;

	// ══ 下面这一组**只**服务回退蒙太奇构建器 `BuildBackVaultMontage()` ══
	//
	// 运行时的动画来源现在是 `VaultProfiles`，不再是这些字段。它们留着是因为
	// `BuildBackVaultMontage()` 需要一个「没有指派蒙太奇资产时」的兜底，而它只认
	// 后撑杆跳的两条 clip。
	//
	// ⚠ **`BackJumpOverDuration` 与 `WhiteBackJumpOverDuration` 是蓝图覆盖过的**
	// （`GA_IG_HouChengGanTiao` 里分别写死 1.0833 与 1.5833，与本文件的 1.092/1.487
	// 不同）。这也是它们**没有**跟着其他数据搬进 `FVaultProfile` 的唯一原因 ——
	// 搬走会让蓝图的覆盖静默失效。删除它们的时机是「六条蒙太奇资产全部指派、
	// 兜底构建器被删掉」之后。
	//
	// `BackJumpOverDuration` 对应的量在运行时另有算法：JumpOver 段 = 移动窗口
	// （配置的 `VaultDuration`）− `FVaultProfile::JumpDuration`。两者不是一回事。

	/** The normal (no-white-extract) visual chain used by the fallback montage builder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpOverSequence;

	/** White-extract visual chain; its air phase is longer and higher. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpOverSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage")
	FName BackVaultMontageSlot = FName(TEXT("DefaultSlot"));

	/** Natural effective durations of the ordinary Jmp and Jump_Over source sequences. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage", meta = (ForceUnits = "s"))
	float BackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage", meta = (ForceUnits = "s"))
	float BackJumpOverDuration = 1.092f;

	/**
	 * Natural effective durations of the White Extract Jmp and Jump_Over source
	 * sequences, consumed only by BuildBackVaultMontage().
	 *
	 * That builder is the FALLBACK: PrepareAttackMontage prefers an assigned
	 * montage asset, and AM_IG_HouChengGanTiao_W is assigned, so these two values
	 * do not affect what actually plays.  They are kept consistent with
	 * WhiteBackVaultDuration anyway so both paths describe the same motion.
	 *
	 * The Jump_Over value is deliberately the **flight time** of that leg
	 * (MHR id 159, ~1.487 s), not the clip's own length (1.5833 s).  The pose is
	 * meant to outlive the flight and be cut by the landing, exactly as in Rise
	 * -- see WhiteBackVaultDuration for the measurement behind that choice.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage", meta = (ForceUnits = "s"))
	float WhiteBackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vault|Fallback Montage", meta = (ForceUnits = "s"))
	float WhiteBackJumpOverDuration = 1.487f;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

	/**
	 * 给**本方向**灌两条 profile（无白灯与白灯）。每个方向自己的构造调用一次。
	 *
	 * 放在基类而不是各个子类里，是因为四个方向的差别只有「曲线从哪来」与
	 * 「用哪两条 clip」；把它拆成四份等于把同一段逻辑抄四遍，而抄错一条的后果
	 * 是某个方向**静默地用上别人的曲线**。
	 *
	 * 为什么不由基类构造统一灌：那会灌给每一个子类。后撑杆跳的 GA 上就会混进
	 * 前/左/右六条，`GetVaultDirection()` 要求所有条目同向 ⇒ 返回 None ⇒
	 * 后撑杆跳**直接不出招**。这条踩过。
	 */
	void BuildDirectionProfiles(EDirectionalInput Direction);

private:
	/**
	 * 没有指派蒙太奇资产时，运行时**兜底**拼一条。
	 *
	 * **只对后撑杆跳有效**：它认的是 `BackJumpSequence` 那一组字段，所以函数入口
	 * 会先核对方向是不是 `Back`。别的方向没有兜底 ⇒ 激活被拒（**不出招**），
	 * 而不是拿后撑杆跳的动画演一遍前撑杆跳 —— 后者难查得多。
	 */
	bool BuildBackVaultMontage();

	/**
	 * Makes the first (Montage-root-motion-owned) Jump segment airborne before
	 * its first root-motion delta is extracted.  The matching restore path is
	 * deliberately owned by this GA rather than by the later CurvedVault task:
	 * the ability can be interrupted before that task exists.
	 */
	bool BeginBackVaultInitialFlight(ACharacter& Character);
	/** Restore the pre-vault locomotion mode after an interrupted flight phase. */
	void RestoreBackVaultInitialFlight(ACharacter& Character);
	/**
	 * 播一条撑杆跳蒙太奇并接管它的实例：播 + 绑中断回调 + 注册 + `bEnableAutoBlendOut = false`。
	 *
	 * ⚠ **起手段那一条不要绑 `OnCompleted`** —— 它的长度就是 `JumpDuration`，必然在
	 * 动作中途"完成"，接了会把 `bVisualFinished` 提前置位，`TryFinishBackVault` 就会在
	 * 弧段姿势播完之前结束整个动作。`OnCompleted` 只给弧段那条（`bBindCompleted`）。
	 *
	 * `bEnableAutoBlendOut = false` 是**交叉淡入的前提**：它让蒙太奇在自身长度处不终止、
	 * 保持终末姿势，于是播弧段那条时 `StopAllMontagesByGroupName` 才有一份姿势可以淡出。
	 */
	bool PlayVaultMontage(ACharacter& Character, UAnimMontage* Montage, FName StartSection,
		float BlendInTime, const TCHAR* TaskName, bool bBindCompleted,
		TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait>& OutTask);

	/** Schedules the exact Jump -> JumpOver ownership boundary. */
	bool ScheduleJumpOverMovementHandoff();
	/** Runs at the JumpOver section boundary and gives its physical path to CurvedVault. */
	bool StartBackVaultMovement();
	/**
	 * Rebuilds the action-owned prefix of the recorded path as a MoveToForce
	 * offset curve.
	 *
	 * OutHandoffVelocityLocal receives the analytic tangent of that same path at
	 * FreeFallHandoffProgress, in the path's facing frame (X forward, Y right,
	 * Z up) and in cm/s.  Sampling it from the recorded keys is what makes the
	 * free-fall hand-off independent of the frame on which the movement task
	 * happens to observe the root-motion source completing.
	 */
	UCurveVector* BuildTrajectoryCurve(const TArray<FVaultTrajectoryKey>& Keys,
		const TArray<FVaultClipDriftKey>& DriftKeys,
		float StartProgress,
		float FreeFallHandoffProgress, float TotalDistance, float ApexHeight,
		float CurveDuration, float& OutHandoffDistance,
		FVector& OutHandoffVelocityLocal) const;
	void TryFinishBackVault();

	UFUNCTION()
	void HandleBackVaultVisualCompleted();

	UFUNCTION()
	void HandleBackVaultVisualInterrupted();

	UFUNCTION()
	void HandleJumpOverMovementHandoff();

	UFUNCTION()
	void HandleBackVaultMovementFinished(const FWeaponMovementResult& MovementResult);

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> BackVaultVisualTask;

	/** 弧段那条蒙太奇的任务。与起手段分开持有，`EndAbility` 两条都要收。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> BackVaultArcVisualTask;

	/** Keeps the section-boundary delay inside the ability lifecycle. */
	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> JumpOverHandoffTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> BackVaultMovementTask;

	/** Keeps the runtime curve alive for the exact lifetime of the movement task. */
	UPROPERTY(Transient)
	TObjectPtr<UCurveVector> ActiveTrajectoryCurve;

	bool bUseWhiteBackVault = false;
	/** Balanced explicitly after Jump has handed movement ownership to CurvedVault. */
	bool bBackVaultVisualRootMotionDisabled = false;
	bool bVisualFinished = false;
	bool bMovementFinished = false;
	/** Set only after an unobstructed action phase has released movement while still airborne. */
	bool bBeginFreeFallAfterEnd = false;
	/**
	 * True only when the arc reached the ground under its own path, so this Action
	 * owns the landing pose even though the Host never started a free fall.
	 * Mutually exclusive with bBeginFreeFallAfterEnd.
	 */
	bool bPlayLandedPresentation = false;
	/** True from the first Jump frame until CurvedVault hands the action to Falling, or cleanup restores it. */
	bool bBackVaultInitialFlightOwned = false;
	/** Movement state to restore if the GA ends before its intentional Falling hand-off. */
	uint8 PreBackVaultMovementMode = 0;
	uint8 PreBackVaultCustomMovementMode = 0;
	bool bIsEndingBackVault = false;
};
