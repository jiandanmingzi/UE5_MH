// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMM4AssetReportCommandlet.h"

#if WITH_EDITOR

#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace
{
constexpr TCHAR SchemaPath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR SheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move");
constexpr TCHAR UnsheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Move.PSD_MH_UnSh_Move");

constexpr float FloatTolerance = 0.001f;

bool Expect(const bool bCondition, const TCHAR* Message)
{
	if (!bCondition)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Report] %s"), Message);
	}
	return bCondition;
}

bool ExpectNear(const float Actual, const float Expected, const TCHAR* Label)
{
	if (!FMath::IsNearlyEqual(Actual, Expected, FloatTolerance))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Report] %s expected %.3f but found %.3f."), Label, Expected, Actual);
		return false;
	}
	return true;
}

bool ReportSchema(const UPoseSearchSchema& Schema)
{
	bool bValid = true;
	UE_LOG(LogTemp, Display, TEXT("[PMM4Report] Schema=%s SampleRate=%d Cardinality=%d Channels=%d"), *Schema.GetPathName(), Schema.SampleRate, Schema.SchemaCardinality, Schema.GetChannels().Num());
	bValid &= Expect(Schema.SampleRate == 60, TEXT("PSS_MH_Move SampleRate must be 60."));
	bValid &= Expect(Schema.DataPreprocessor == EPoseSearchDataPreprocessor::Normalize, TEXT("PSS_MH_Move DataPreprocessor must be Normalize."));
	bValid &= Expect(Schema.GetChannels().Num() == 4, TEXT("PSS_MH_Move must contain exactly Pose, Trajectory and two Curve channels."));
	bValid &= Expect(Schema.SchemaCardinality == 30, TEXT("PSS_MH_Move must use 30 dimensions: full Position at four trajectory samples."));

	int32 PoseChannelCount = 0;
	int32 TrajectoryChannelCount = 0;
	int32 IntentChannelCount = 0;
	int32 DistanceChannelCount = 0;
	for (const UPoseSearchFeatureChannel* Channel : Schema.GetChannels())
	{
		if (const UPoseSearchFeatureChannel_Pose* Pose = Cast<UPoseSearchFeatureChannel_Pose>(Channel))
		{
			++PoseChannelCount;
			bValid &= ExpectNear(Pose->Weight, 2.0f, TEXT("Pose Channel Weight"));
			bValid &= Expect(Pose->InputQueryPose == EInputQueryPose::UseContinuingPose, TEXT("Pose Channel InputQueryPose must use continuing pose."));
			bValid &= Expect(Pose->SampledBones.Num() == 2, TEXT("Pose Channel must contain exactly two foot bones."));
			UE_LOG(LogTemp, Display, TEXT("[PMM4Report] PoseChannel Class=%s Weight=%.3f Bones=%d InputQueryPose=%d"), *Pose->GetClass()->GetPathName(), Pose->Weight, Pose->SampledBones.Num(), static_cast<int32>(Pose->InputQueryPose));
			for (const FPoseSearchBone& Bone : Pose->SampledBones)
			{
				const FName BoneName = Bone.Reference.BoneName;
				bValid &= Expect(BoneName == TEXT("R_Foot_IK_end") || BoneName == TEXT("L_Foot_IK_end"), TEXT("Pose Channel contains a non-foot bone."));
				bValid &= Expect(Bone.Flags == (int32(EPoseSearchBoneFlags::Position) | int32(EPoseSearchBoneFlags::Velocity) | int32(EPoseSearchBoneFlags::Phase)), TEXT("Each foot must use Position + Velocity + Phase."));
				bValid &= ExpectNear(Bone.Weight, 1.0f, TEXT("Foot Pose Weight"));
				UE_LOG(LogTemp, Display, TEXT("[PMM4Report]   PoseBone=%s Flags=%d Weight=%.3f"), *Bone.Reference.BoneName.ToString(), Bone.Flags, Bone.Weight);
			}
		}
		else if (const UPoseSearchFeatureChannel_Trajectory* Trajectory = Cast<UPoseSearchFeatureChannel_Trajectory>(Channel))
		{
			++TrajectoryChannelCount;
			bValid &= ExpectNear(Trajectory->Weight, 6.0f, TEXT("Trajectory Channel Weight"));
			bValid &= Expect(Trajectory->Samples.Num() == 4, TEXT("Trajectory Channel must contain four forward samples."));
			const TArray<float> ExpectedOffsets = { 0.2f, 0.5f, 0.8f, 1.0f };
			UE_LOG(LogTemp, Display, TEXT("[PMM4Report] TrajectoryChannel Class=%s Weight=%.3f Samples=%d"), *Trajectory->GetClass()->GetPathName(), Trajectory->Weight, Trajectory->Samples.Num());
			for (int32 SampleIndex = 0; SampleIndex < Trajectory->Samples.Num(); ++SampleIndex)
			{
				const FPoseSearchTrajectorySample& Sample = Trajectory->Samples[SampleIndex];
				if (ExpectedOffsets.IsValidIndex(SampleIndex))
				{
					bValid &= ExpectNear(Sample.Offset, ExpectedOffsets[SampleIndex], TEXT("Trajectory Sample Offset"));
				}
				bValid &= Expect(Sample.Flags == int32(EPoseSearchTrajectoryFlags::Position), TEXT("Trajectory samples must use full Position."));
				bValid &= ExpectNear(Sample.Weight, 1.0f, TEXT("Trajectory Sample Weight"));
				UE_LOG(LogTemp, Display, TEXT("[PMM4Report]   TrajectorySample Offset=%.3f Flags=%d Weight=%.3f"), Sample.Offset, Sample.Flags, Sample.Weight);
			}
		}
		else if (const UPoseSearchFeatureChannel_Curve* Curve = Cast<UPoseSearchFeatureChannel_Curve>(Channel))
		{
			if (Curve->CurveName == TEXT("MM_Intent"))
			{
				++IntentChannelCount;
				bValid &= ExpectNear(Curve->Weight, 10.0f, TEXT("MM_Intent Weight"));
			}
			else if (Curve->CurveName == TEXT("MM_DistanceToStop"))
			{
				++DistanceChannelCount;
				bValid &= ExpectNear(Curve->Weight, 4.0f, TEXT("MM_DistanceToStop Weight"));
			}
			else
			{
				bValid &= Expect(false, TEXT("PSS_MH_Move contains an unexpected Curve Channel."));
			}
			bValid &= ExpectNear(Curve->SampleTimeOffset, 0.0f, TEXT("Curve Channel SampleTimeOffset"));
			bValid &= Expect(Curve->InputQueryPose == EInputQueryPose::UseContinuingPose, TEXT("Curve Channel InputQueryPose must use continuing pose."));
			UE_LOG(LogTemp, Display, TEXT("[PMM4Report] CurveChannel Class=%s Curve=%s Weight=%.3f Offset=%.3f InputQueryPose=%d"), *Curve->GetClass()->GetPathName(), *Curve->CurveName.ToString(), Curve->Weight, Curve->SampleTimeOffset, static_cast<int32>(Curve->InputQueryPose));
		}
	}

	bValid &= Expect(PoseChannelCount == 1 && TrajectoryChannelCount == 1 && IntentChannelCount == 1 && DistanceChannelCount == 1, TEXT("PSS_MH_Move must contain exactly one of each required PMM-4 channel."));
	return bValid;
}

bool ReportDatabase(const UPoseSearchDatabase& Database, const UPoseSearchSchema& ExpectedSchema, const int32 ExpectedEntryCount)
{
	using namespace UE::PoseSearch;
	bool bValid = true;
	// Commandlets do not tick long enough for the PostLoad request to leave the
	// Notstarted state. A fresh, synchronous request is required before reading
	// GetSearchIndex() in this process.
	const ERequestAsyncBuildFlag BuildFlags = ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion;
	if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(&Database, BuildFlags) != EAsyncBuildIndexResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Report] Could not build a current Pose Search index for %s."), *Database.GetPathName());
		return false;
	}

	const UE::PoseSearch::FSearchIndex& SearchIndex = Database.GetSearchIndex();
	bValid &= Expect(Database.Schema == &ExpectedSchema, TEXT("Pose Search database must reference PSS_MH_Move."));
	bValid &= Expect(Database.PoseSearchMode == EPoseSearchMode::BruteForce, TEXT("Pose Search Mode must be Brute Force during PMM-4."));
	bValid &= ExpectNear(Database.ContinuingPoseCostBias, -0.05f, TEXT("Continuing Pose Cost Bias"));
	bValid &= ExpectNear(Database.LoopingCostBias, 0.0f, TEXT("Looping Cost Bias"));
	bValid &= ExpectNear(Database.ExcludeFromDatabaseParameters.Min, 0.0f, TEXT("Exclude From Database Start"));
	bValid &= ExpectNear(Database.ExcludeFromDatabaseParameters.Max, -0.05f, TEXT("Exclude From Database End"));
	bValid &= Expect(Database.GetNumAnimationAssets() == ExpectedEntryCount, TEXT("Pose Search database has an unexpected number of entries."));
	bValid &= Expect(SearchIndex.GetNumPoses() > 0, TEXT("Pose Search database index contains no poses."));
	bValid &= Expect(SearchIndex.WeightsSqrt.Num() == ExpectedSchema.SchemaCardinality, TEXT("Pose Search index dimensionality does not match PSS_MH_Move."));
	bValid &= Expect(SearchIndex.bAnyBlockTransition, TEXT("Pose Search index is missing Block Transition metadata."));
	UE_LOG(LogTemp, Display, TEXT("[PMM4Report] Database=%s Mode=%d Continuing=%.3f Looping=%.3f Exclude=[%.3f,%.3f] Entries=%d IndexedPoses=%d IndexDimensions=%d BlockTransition=%d"),
		*Database.GetPathName(), static_cast<int32>(Database.PoseSearchMode), Database.ContinuingPoseCostBias, Database.LoopingCostBias,
		Database.ExcludeFromDatabaseParameters.Min, Database.ExcludeFromDatabaseParameters.Max, Database.GetNumAnimationAssets(),
		SearchIndex.GetNumPoses(), SearchIndex.WeightsSqrt.Num(), SearchIndex.bAnyBlockTransition);

	for (int32 Index = 0; Index < Database.GetNumAnimationAssets(); ++Index)
	{
		const FPoseSearchDatabaseSequence* Sequence = Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(Index);
		if (!Sequence)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PMM4Report]   Entry=%d is not an animation sequence."), Index);
			bValid = false;
			continue;
		}

		bValid &= Expect(Sequence->Sequence != nullptr, TEXT("Pose Search database contains a null animation sequence."));
		bValid &= Expect(Sequence->bDisableReselection, TEXT("Every PMM-4 candidate must disable same-asset global reselection; continuing-pose evaluation remains available."));
		bValid &= Expect(Sequence->MirrorOption == EPoseSearchMirrorOption::UnmirroredOnly, TEXT("All PMM-4 entries must use Original Only mirror mode."));

		UE_LOG(LogTemp, Display, TEXT("[PMM4Report]   Entry=%d Sequence=%s DisableReselection=%d Mirror=%d Range=[%.3f,%.3f]"),
			Index, *GetPathNameSafe(Sequence->Sequence), Sequence->bDisableReselection, static_cast<int32>(Sequence->MirrorOption),
			Sequence->SamplingRange.Min, Sequence->SamplingRange.Max);
	}
	return bValid;
}
}

#endif

UMHGZPMM4AssetReportCommandlet::UMHGZPMM4AssetReportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMM4AssetReportCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	UPoseSearchDatabase* SheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, SheathedDatabasePath);
	UPoseSearchDatabase* UnsheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, UnsheathedDatabasePath);
	if (!Schema || !SheathedDatabase || !UnsheathedDatabase)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Report] Failed to load one or more PMM-4 assets."));
		return 1;
	}

	return ReportSchema(*Schema)
		&& ReportDatabase(*SheathedDatabase, *Schema, 11)
		&& ReportDatabase(*UnsheathedDatabase, *Schema, 4) ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMM4Report] This commandlet requires an editor build."));
	return 1;
#endif
}
