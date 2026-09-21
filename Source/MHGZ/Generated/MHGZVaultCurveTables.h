// Copyright MHGZ Project. All Rights Reserved.
//
// ================== 生成物，请勿手改 ==================
//
// 由 `Scripts/MHRise/build_vault_curves.py` 从 MHR 实机录制生成。
// 改数请改生成器，或改原始录制后重跑生成器。校验：`--check`。
//
// 本文件**不含生成时间** —— 那会让每次重跑都「有差异」，`--check` 就废了。
// 可追溯性由每组的 CSV sha256 承担。
//
// 【为什么是 POD 而不是 FVaultTrajectoryKey】
// 后者是 USTRUCT：反射与构造让它无法 constexpr，只能运行时逐个灌值。
// 这里的 POD 镜像让曲线**在编译期就是常量**。转换在配对的
// `MHGZVaultCurveTables.cpp`（**手写**）里做，不在本文件里。
//
// 【后撑杆跳不在这里】UMHGZBackVaultAbility 的两张表仍是手抄常量，
// 本次迁移有意不动它们（计划第 2 步）。所以本表只覆盖 前/左/右 × 无/有白灯。
//
// 【弦向的符号约定 —— 踩过】`ChordYawDeg` 是「**朝左为正**」（MHR / 文档约定），
// 而 UE 的 `FRotator(0, Yaw, 0).Vector()` 正 Yaw 朝**右**。两个约定正方向相反，
// 所以消费方**必须取负**。判据见 `docs/reference/撑杆跳曲线.md` 的方向基准一节，
// 用法见 `UMHGZPoleVaultAbility::StartBackVaultMovement` 里 `DirectionSnapshot` 那段。
// 后撑杆跳的弦向恰好 180（取负等于不取负）、向前只有 0.56°，所以写反了只会在
// 左右两个方向上暴露。
//
// 【落地校验】每组只接受 `|链尾抬升| <= 30 cm` 的试次 —— 链尾抬升是
// 「落地处与起跳处的地形高差」。被剔除的是**从崖边跳下去**的那几次
// （链里多一段下坠，末值低 1~10 m），它们的归一化轨迹根本不是撑杆跳的形状。
// 每组实际用了哪一次、剔了几次，见下面各组自己的注释。
//
// ================= 各组实际所用的试次 =================
//
// Forward_NoWhite  `141` / 238 帧 / 弧 1.987 s（窗口 1.753 s）/ 总长 584.32 cm / 顶点 580.10 cm / 弦向 +0.56° / 链尾 -5.49 cm
//                  可用 49/56 次（剔除 7 次坠崖）  CSV sha256 3f68048e169435a2…
// Left_NoWhite     `144` / 238 帧 / 弧 1.987 s（窗口 1.753 s）/ 总长 562.88 cm / 顶点 591.23 cm / 弦向 +86.06° / 链尾 +17.39 cm
//                  可用 16/26 次（剔除 10 次坠崖）  CSV sha256 548e5e1b0d69fd2f…
// Right_NoWhite    `145` / 239 帧 / 弧 1.995 s（窗口 1.753 s）/ 总长 635.39 cm / 顶点 580.10 cm / 弦向 -84.21° / 链尾 -18.90 cm
//                  可用 16/21 次（剔除 5 次坠崖）  CSV sha256 5023e1f2ae56208b…
// Forward_White    `155` / 257 帧 / 弧 2.145 s（窗口 2.145 s）/ 总长 738.68 cm / 顶点 752.26 cm / 弦向 -0.65° / 链尾 +4.84 cm
//                  可用 25/38 次（剔除 13 次坠崖）  CSV sha256 011f27325322bf48…
// Left_White       `144` / 257 帧 / 弧 2.145 s（窗口 2.145 s）/ 总长 595.25 cm / 顶点 749.47 cm / 弦向 +90.12° / 链尾 -3.26 cm
//                  可用 2/10 次（剔除 8 次坠崖）  CSV sha256 fe48fa926a814109…
// Right_White      `145` / 257 帧 / 弧 2.145 s（窗口 2.145 s）/ 总长 691.58 cm / 顶点 749.47 cm / 弦向 -88.03° / 链尾 +1.55 cm
//                  可用 6/10 次（剔除 4 次坠崖）  CSV sha256 421c8e8d34cb2211…
//
// 曲线局部 +Y 与 CSV y_lateral 的换算符号：Y_curve = -y_lateral（见生成器里的 CURVE_Y_SIGN 注释）。
//

#pragma once

#include "CoreMinimal.h"

namespace MHGZ::VaultCurves
{
/**
 * One point from a recorded vault trajectory.
 *
 * X = travel progress, Y = lateral offset divided by total travel distance,
 * Z = height divided by measured apex height.  Mirrors FVaultTrajectoryKey.
 */
struct FKeyPOD
{
	float Time;
	float X;
	float Y;
	float Z;
};

/**
 * Forward travel recovered from a JumpOver clip's own root track, as a
 * fraction of the profile's Distance.  Mirrors FVaultClipDriftKey.
 *
 * All values are currently zero: with the root track locked
 * (bForceRootLock = True) the recorded path alone already delivers the
 * reference travel, so there is nothing to recover.  The mechanism is kept
 * because that was measured on the *back* vault only -- see verification
 * item 14, which re-measures it for these clips.
 */
struct FDriftPOD
{
	float CurveTime;
	float ForwardFraction;
};

/** Per-variant scalars.  Trial is for traceability only, never for logic. */
struct FMeta
{
	/** Length of the whole airborne prefix, in seconds -- the normalization domain. */
	float ArcDuration;
	/**
	 * Length of Jump + JumpOver only, in seconds -- the CurvedVault window.  Never
	 * use it as the normalization domain: that puts the Jump boundary at the wrong
	 * progress and hands the Jump ~30 cm of travel that belongs to JumpOver.
	 *
	 * Equal to ArcDuration for the white variants, which have no separate descent
	 * segment; the non-white shortfall is the measured fall clip.
	 */
	float VaultDuration;
	/** Total travel distance in cm. */
	float Distance;
	/** Measured apex height in cm. */
	float Apex;
	/**
	 * Whole-chain chord direction minus takeoff facing, in degrees.  This is the
	 * value DirectionSnapshot is built from.  Not an integer +-90: an integer
	 * approximation would tilt the world path by several degrees and push the
	 * curve's final lateral offset from 0 to ~30 cm.
	 */
	float ChordYawDeg;
	/** Source trial, "<takeoff id>/<frames>f".  Traceability only. */
	const TCHAR* Trial;
};

// ── 无白灯 · 向前 ──────────────────────────────
inline constexpr FKeyPOD Forward_NoWhite[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.028550f, 0.014037f, 0.000000f },
	{ 0.083333f, 0.048134f, 0.013623f, 0.000000f },
	{ 0.125000f, 0.061946f, 0.005424f, 0.000000f },
	{ 0.166667f, 0.087988f, -0.007293f, 0.000000f },
	{ 0.208333f, 0.174954f, -0.025565f, 0.059711f },
	{ 0.250000f, 0.248982f, -0.026557f, 0.130241f },
	{ 0.291667f, 0.265565f, -0.027808f, 0.137094f },
	{ 0.333333f, 0.281501f, -0.028184f, 0.212247f },
	{ 0.375000f, 0.325618f, -0.017681f, 0.408192f },
	{ 0.416667f, 0.372006f, -0.009628f, 0.577108f },
	{ 0.458333f, 0.419522f, -0.005738f, 0.716942f },
	{ 0.500000f, 0.467974f, -0.004379f, 0.828807f },
	{ 0.541667f, 0.516684f, -0.003766f, 0.912891f },
	{ 0.583333f, 0.565159f, -0.003260f, 0.968742f },
	{ 0.625000f, 0.612708f, -0.002798f, 0.996564f },
	{ 0.666667f, 0.658963f, -0.002356f, 0.996417f },
	{ 0.708333f, 0.703061f, -0.001915f, 0.968299f },
	{ 0.750000f, 0.745639f, -0.001498f, 0.912212f },
	{ 0.791667f, 0.788052f, -0.001082f, 0.828155f },
	{ 0.833333f, 0.830464f, -0.000666f, 0.716128f },
	{ 0.875000f, 0.872877f, -0.000250f, 0.576130f },
	{ 0.916667f, 0.915185f, -0.000350f, 0.408087f },
	{ 0.958333f, 0.957473f, -0.000524f, 0.212049f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Forward_NoWhite_Count = 25;

inline constexpr FDriftPOD Forward_NoWhite_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Forward_NoWhite_Drift_Count = 5;

inline constexpr FMeta Forward_NoWhite_Meta = { 1.986644f, 1.752922f, 584.32f, 580.10f, 0.56f, TEXT("141/238f") };

// ── 无白灯 · 向左 ──────────────────────────────
inline constexpr FKeyPOD Left_NoWhite[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.017740f, 0.009944f, 0.001968f },
	{ 0.083333f, 0.056964f, 0.024814f, 0.006896f },
	{ 0.125000f, 0.097295f, 0.036907f, 0.012227f },
	{ 0.166667f, 0.118111f, 0.043006f, 0.014990f },
	{ 0.208333f, 0.206526f, 0.016845f, 0.077918f },
	{ 0.250000f, 0.282732f, 0.009533f, 0.146709f },
	{ 0.291667f, 0.299682f, 0.006827f, 0.153370f },
	{ 0.333333f, 0.315817f, 0.004925f, 0.227079f },
	{ 0.375000f, 0.349453f, 0.004381f, 0.419383f },
	{ 0.416667f, 0.385880f, 0.005657f, 0.584291f },
	{ 0.458333f, 0.426731f, 0.008054f, 0.721841f },
	{ 0.500000f, 0.470194f, 0.011190f, 0.831694f },
	{ 0.541667f, 0.514454f, 0.014386f, 0.914103f },
	{ 0.583333f, 0.558809f, 0.017462f, 0.969037f },
	{ 0.625000f, 0.603176f, 0.019622f, 0.996551f },
	{ 0.666667f, 0.647399f, 0.020413f, 0.996546f },
	{ 0.708333f, 0.691523f, 0.018963f, 0.969036f },
	{ 0.750000f, 0.735484f, 0.015944f, 0.914067f },
	{ 0.791667f, 0.779496f, 0.012747f, 0.831468f },
	{ 0.833333f, 0.823415f, 0.009560f, 0.721573f },
	{ 0.875000f, 0.867335f, 0.006372f, 0.584236f },
	{ 0.916667f, 0.911799f, 0.003018f, 0.419330f },
	{ 0.958333f, 0.956336f, -0.000365f, 0.226895f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Left_NoWhite_Count = 25;

inline constexpr FDriftPOD Left_NoWhite_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Left_NoWhite_Drift_Count = 5;

inline constexpr FMeta Left_NoWhite_Meta = { 1.986644f, 1.752922f, 562.88f, 591.23f, 86.06f, TEXT("144/238f") };

// ── 无白灯 · 向右 ──────────────────────────────
inline constexpr FKeyPOD Right_NoWhite[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.040685f, -0.012139f, 0.000000f },
	{ 0.083333f, 0.085838f, -0.019693f, 0.000000f },
	{ 0.125000f, 0.139092f, -0.021013f, 0.000000f },
	{ 0.166667f, 0.173560f, -0.025944f, 0.000000f },
	{ 0.208333f, 0.255648f, -0.035107f, 0.062021f },
	{ 0.250000f, 0.322490f, -0.029979f, 0.130769f },
	{ 0.291667f, 0.337234f, -0.029811f, 0.137284f },
	{ 0.333333f, 0.353326f, -0.028879f, 0.219285f },
	{ 0.375000f, 0.401896f, -0.025721f, 0.415042f },
	{ 0.416667f, 0.447931f, -0.024611f, 0.582593f },
	{ 0.458333f, 0.490417f, -0.025137f, 0.721936f },
	{ 0.500000f, 0.530697f, -0.026685f, 0.833073f },
	{ 0.541667f, 0.570341f, -0.028376f, 0.916004f },
	{ 0.583333f, 0.609873f, -0.029999f, 0.970727f },
	{ 0.625000f, 0.649324f, -0.030735f, 0.997244f },
	{ 0.666667f, 0.688660f, -0.030230f, 0.995554f },
	{ 0.708333f, 0.727766f, -0.027710f, 0.965658f },
	{ 0.750000f, 0.766765f, -0.023872f, 0.907529f },
	{ 0.791667f, 0.805740f, -0.019926f, 0.821209f },
	{ 0.833333f, 0.844773f, -0.015974f, 0.706480f },
	{ 0.875000f, 0.883748f, -0.012028f, 0.563702f },
	{ 0.916667f, 0.922235f, -0.007970f, 0.392567f },
	{ 0.958333f, 0.960763f, -0.003927f, 0.193244f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Right_NoWhite_Count = 25;

inline constexpr FDriftPOD Right_NoWhite_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Right_NoWhite_Drift_Count = 5;

inline constexpr FMeta Right_NoWhite_Meta = { 1.994992f, 1.752922f, 635.39f, 580.10f, -84.21f, TEXT("145/239f") };

// ── 有白灯 · 向前 ──────────────────────────────
inline constexpr FKeyPOD Forward_White[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.030781f, 0.005681f, 0.000757f },
	{ 0.083333f, 0.060739f, 0.009665f, 0.001386f },
	{ 0.125000f, 0.090842f, 0.009932f, 0.001969f },
	{ 0.166667f, 0.129389f, -0.009545f, 0.001969f },
	{ 0.208333f, 0.211096f, -0.018326f, 0.033921f },
	{ 0.250000f, 0.300977f, -0.023303f, 0.133942f },
	{ 0.291667f, 0.391701f, -0.027604f, 0.149862f },
	{ 0.333333f, 0.441615f, -0.030610f, 0.291454f },
	{ 0.375000f, 0.477818f, -0.032144f, 0.472626f },
	{ 0.416667f, 0.513870f, -0.033265f, 0.627164f },
	{ 0.458333f, 0.549707f, -0.033495f, 0.754854f },
	{ 0.500000f, 0.585335f, -0.032019f, 0.855869f },
	{ 0.541667f, 0.620776f, -0.029270f, 0.930426f },
	{ 0.583333f, 0.655861f, -0.025870f, 0.978021f },
	{ 0.625000f, 0.690727f, -0.022379f, 0.998912f },
	{ 0.666667f, 0.725642f, -0.018970f, 0.993104f },
	{ 0.708333f, 0.760259f, -0.015696f, 0.960623f },
	{ 0.750000f, 0.794427f, -0.012028f, 0.901429f },
	{ 0.791667f, 0.828160f, -0.008417f, 0.815568f },
	{ 0.833333f, 0.861793f, -0.005407f, 0.703051f },
	{ 0.875000f, 0.895564f, -0.002875f, 0.563652f },
	{ 0.916667f, 0.929568f, -0.000988f, 0.397721f },
	{ 0.958333f, 0.964129f, 0.000043f, 0.205021f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Forward_White_Count = 25;

inline constexpr FDriftPOD Forward_White_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Forward_White_Drift_Count = 5;

inline constexpr FMeta Forward_White_Meta = { 2.145242f, 2.145242f, 738.68f, 752.26f, -0.65f, TEXT("155/257f") };

// ── 有白灯 · 向左 ──────────────────────────────
inline constexpr FKeyPOD Left_White[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.018181f, 0.011540f, 0.000000f },
	{ 0.083333f, 0.058470f, 0.030511f, 0.000000f },
	{ 0.125000f, 0.094937f, 0.044224f, 0.000000f },
	{ 0.166667f, 0.127867f, 0.042439f, 0.000009f },
	{ 0.208333f, 0.228546f, 0.029668f, 0.077232f },
	{ 0.250000f, 0.277382f, 0.028114f, 0.105059f },
	{ 0.291667f, 0.285086f, 0.026939f, 0.121555f },
	{ 0.333333f, 0.326119f, 0.027062f, 0.288817f },
	{ 0.375000f, 0.372513f, 0.027138f, 0.470578f },
	{ 0.416667f, 0.418379f, 0.026963f, 0.625583f },
	{ 0.458333f, 0.463184f, 0.026558f, 0.753784f },
	{ 0.500000f, 0.505887f, 0.025873f, 0.855213f },
	{ 0.541667f, 0.547078f, 0.024816f, 0.929957f },
	{ 0.583333f, 0.587361f, 0.023436f, 0.977820f },
	{ 0.625000f, 0.627524f, 0.021763f, 0.998879f },
	{ 0.666667f, 0.667786f, 0.020052f, 0.993182f },
	{ 0.708333f, 0.708234f, 0.018091f, 0.960731f },
	{ 0.750000f, 0.748158f, 0.015566f, 0.901448f },
	{ 0.791667f, 0.788189f, 0.012493f, 0.815412f },
	{ 0.833333f, 0.828986f, 0.009291f, 0.702619f },
	{ 0.875000f, 0.870349f, 0.006210f, 0.563042f },
	{ 0.916667f, 0.912502f, 0.003473f, 0.396624f },
	{ 0.958333f, 0.955751f, 0.001413f, 0.203206f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Left_White_Count = 25;

inline constexpr FDriftPOD Left_White_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Left_White_Drift_Count = 5;

inline constexpr FMeta Left_White_Meta = { 2.145242f, 2.145242f, 595.25f, 749.47f, 90.12f, TEXT("144/257f") };

// ── 有白灯 · 向右 ──────────────────────────────
inline constexpr FKeyPOD Right_White[] = {
	{ 0.000000f, 0.000000f, 0.000000f, 0.000000f },
	{ 0.041667f, 0.039159f, -0.014344f, 0.000000f },
	{ 0.083333f, 0.084602f, -0.024141f, 0.000000f },
	{ 0.125000f, 0.135805f, -0.028786f, 0.000000f },
	{ 0.166667f, 0.173890f, -0.039090f, 0.000005f },
	{ 0.208333f, 0.260865f, -0.047295f, 0.077226f },
	{ 0.250000f, 0.302927f, -0.047273f, 0.105059f },
	{ 0.291667f, 0.309580f, -0.048068f, 0.121550f },
	{ 0.333333f, 0.342066f, -0.047316f, 0.288817f },
	{ 0.375000f, 0.379545f, -0.045987f, 0.470577f },
	{ 0.416667f, 0.417455f, -0.044445f, 0.625582f },
	{ 0.458333f, 0.456282f, -0.042661f, 0.753784f },
	{ 0.500000f, 0.496920f, -0.040565f, 0.855210f },
	{ 0.541667f, 0.538900f, -0.038095f, 0.929880f },
	{ 0.583333f, 0.581611f, -0.035333f, 0.977776f },
	{ 0.625000f, 0.624407f, -0.032346f, 0.998868f },
	{ 0.666667f, 0.667117f, -0.029321f, 0.993205f },
	{ 0.708333f, 0.709719f, -0.026053f, 0.960707f },
	{ 0.750000f, 0.752657f, -0.022305f, 0.901428f },
	{ 0.791667f, 0.795530f, -0.018080f, 0.815384f },
	{ 0.833333f, 0.837753f, -0.013784f, 0.702585f },
	{ 0.875000f, 0.879502f, -0.009605f, 0.562844f },
	{ 0.916667f, 0.920559f, -0.005741f, 0.396439f },
	{ 0.958333f, 0.960742f, -0.002493f, 0.203278f },
	{ 1.000000f, 1.000000f, 0.000000f, 0.000000f },
};
inline constexpr int32 Right_White_Count = 25;

inline constexpr FDriftPOD Right_White_Drift[] = {
	{ 0.000000f, 0.000000f },
	{ 0.090000f, 0.000000f },
	{ 0.230000f, 0.000000f },
	{ 0.320000f, 0.000000f },
	{ 1.000000f, 0.000000f },
};
inline constexpr int32 Right_White_Drift_Count = 5;

inline constexpr FMeta Right_White_Meta = { 2.145242f, 2.145242f, 691.58f, 749.47f, -88.03f, TEXT("145/257f") };

// Deliberately no enum here: the ability dispatches on (EDirectionalInput,
// bWhite), and an extra parallel enum would be a third naming of the same
// two axes.  Lookup is by pair.
} // namespace MHGZ::VaultCurves
