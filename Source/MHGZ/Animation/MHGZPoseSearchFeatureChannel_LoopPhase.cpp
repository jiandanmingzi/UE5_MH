// Copyright MHGZ Project. All Rights Reserved.
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchHistory.h"
#if WITH_EDITOR
#include "PoseSearch/PoseSearchAssetIndexer.h"
#endif

FVector2D UMHGZPoseSearchFeatureChannel_LoopPhase::EncodePhase(float Time, float Length)
{
	if (!FMath::IsFinite(Time) || !FMath::IsFinite(Length) || Length <= SMALL_NUMBER)
	{ return FVector2D::ZeroVector; }
	const float Angle = 2.f * PI * Time / Length;
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
}

bool UMHGZPoseSearchFeatureChannel_LoopPhase::Finalize(UPoseSearchSchema* Schema)
{
	FeatureLayoutVersion = 2;
	ChannelDataOffset = Schema->SchemaCardinality;
	ChannelCardinality = 17; // phase XY + feet position/velocity XYZ + root locomotion velocity XYZ
	Schema->SchemaCardinality += ChannelCardinality;
	FBoneReference Left, Right;
	Left.BoneName = TEXT("L_Foot_IK_end");
	Right.BoneName = TEXT("R_Foot_IK_end");
	LeftFootIdx = Schema->AddBoneReference(Left, UE::PoseSearch::DefaultRole);
	RightFootIdx = Schema->AddBoneReference(Right, UE::PoseSearch::DefaultRole);
	return LeftFootIdx != UE::PoseSearch::InvalidSchemaBoneIdx && RightFootIdx != UE::PoseSearch::InvalidSchemaBoneIdx;
}

float UMHGZPoseSearchFeatureChannel_LoopPhase::EntryMatchCost(TConstArrayView<float> A,
	TConstArrayView<float> B, float FootVelocityHorizon, float RootVelocityHorizon)
{
	check(A.Num() >= 17 && B.Num() >= 17);
	float Cost = 0.f;
	for (int32 I = 2; I < 17; ++I)
	{
		const float Scale = I < 8 ? 1.f : I < 14 ? FootVelocityHorizon : RootVelocityHorizon;
		Cost += FMath::Square((A[I] - B[I]) * Scale);
	}
	return Cost;
}

void UMHGZPoseSearchFeatureChannel_LoopPhase::BuildQuery(UE::PoseSearch::FSearchContext& Context) const
{
	using namespace UE::PoseSearch;
	auto Query = Context.EditFeatureVector().Slice(ChannelDataOffset, ChannelCardinality);
	for (float& Value : Query) { Value = 0.f; }
	auto* Role = Context.GetContext(DefaultRole);
	auto* Anim = Role ? Cast<UMHGZMotionMatchingAnimInstance>(Role->GetFirstObjectParam()) : nullptr;
	const UPoseSearchDatabase* DB = Anim ? Anim->GetLoopQueryDatabase(bUnsheathed) : nullptr;
	if (!Anim || !DB || DB->Schema != GetTypedOuter<UPoseSearchSchema>()) { return; }
	const FVector2D Phase = bUnsheathed ? Anim->MMUnsheathedLoopPhaseQuery : Anim->MMLoopPhaseQuery;
	const bool bMatchFeet = Anim->WantsLoopEntryMatch(bUnsheathed) || Phase.SizeSquared() < 0.5f;
	FName Source = TEXT("CycleClock");
	if (bMatchFeet)
	{
		const IPoseHistory* History = Context.GetPoseHistory(DefaultRole);
		if (!History || History->IsEmpty())
		{
			Anim->PublishLoopPhaseQuery(bUnsheathed, FVector2D::ZeroVector, TEXT("MissingFinalPoseHistory"));
			return;
		}
		for (int32 I = 0; I < 2; ++I)
		{
			const int8 Bone = I == 0 ? LeftFootIdx : RightFootIdx;
			FFeatureVectorHelper::EncodeVector(Query, 2 + I * 3,
				Context.GetSamplePosition(0.f, 0.f, Bone, RootSchemaBoneIdx, DefaultRole, DefaultRole), EComponentStrippingVector::None, false);
			FFeatureVectorHelper::EncodeVector(Query, 8 + I * 3,
				Context.GetSampleVelocity(0.f, 0.f, Bone, RootSchemaBoneIdx, DefaultRole, DefaultRole, true), EComponentStrippingVector::None, false);
		}
		FFeatureVectorHelper::EncodeVector(Query, 14,
			Context.GetSampleVelocity(0.f, 0.f, RootSchemaBoneIdx, RootSchemaBoneIdx,
				DefaultRole, DefaultRole, false), EComponentStrippingVector::None, false);
		// Query histories are in world seconds; indexed velocities are in source
		// seconds. Normalize velocity only, never feet positions or cyclic phase.
		const float Rate = Anim->GetLocomotionPlaybackRateScale(bUnsheathed);
		for (int32 I = 8; I < 17; ++I) { Query[I] /= Rate; }
		Source = TEXT("FinalPoseFeetEntry");
	}
	const FSearchIndex& Index = DB->GetSearchIndex();
	float BestCost = MAX_flt;
	FVector2D BestPhase = FVector2D::ZeroVector;
	TArray<float> Scratch;
	for (int32 I = 0; I < Index.GetNumPoses(); ++I)
	{
		if (Index.PoseMetadata[I].IsBlockTransition()) { continue; }
		const auto Pose = Index.GetPoseValuesSafe(I, Scratch).Slice(ChannelDataOffset, ChannelCardinality);
		const FVector2D P = FFeatureVectorHelper::DecodeVector2D(Pose, 0);
		const float Cost = bMatchFeet ? EntryMatchCost(Pose, Query, EntryVelocityHorizon, EntryRootVelocityHorizon)
			: float((P - Phase).SizeSquared());
		if (Cost < BestCost) { BestCost = Cost; BestPhase = P; }
	}
	// Quantize once to the actual nearest indexed pose. The hard filter is narrow
	// enough that trajectory cannot choose an earlier/later foot phase instead.
	FFeatureVectorHelper::EncodeVector2D(Query, 0, BestPhase);
	Anim->PublishLoopPhaseQuery(bUnsheathed, BestPhase, Source);
}

bool UMHGZPoseSearchFeatureChannel_LoopPhase::IsFilterValid(TConstArrayView<float> Pose,
	TConstArrayView<float> Query, int32 PoseIdx, const UE::PoseSearch::FPoseMetadata& Metadata) const
{
	const FVector2D Q = UE::PoseSearch::FFeatureVectorHelper::DecodeVector2D(Query, ChannelDataOffset);
	// No compatible source loop (e.g. Start/Exit): ordinary pose/velocity search,
	// not an invented whole-action normalized phase or a forced default foot.
	if (Q.SizeSquared() < 0.5f || MaxCycleError <= 0.f) { return true; }
	const FVector2D P = UE::PoseSearch::FFeatureVectorHelper::DecodeVector2D(Pose, ChannelDataOffset);
	return FVector2D::DotProduct(P, Q) >= FMath::Cos(2.f * PI * MaxCycleError);
}

#if WITH_EDITOR
void UMHGZPoseSearchFeatureChannel_LoopPhase::FillWeights(TArrayView<float> Weights) const
{
	for (int32 I = 0; I < ChannelCardinality; ++I) { Weights[ChannelDataOffset + I] = I < 2 ? 1.f : 0.f; }
}

bool UMHGZPoseSearchFeatureChannel_LoopPhase::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const
{
	if (Indexer.GetPlayLength() <= SMALL_NUMBER) { return false; }
	for (int32 I = Indexer.GetBeginSampleIdx(); I < Indexer.GetEndSampleIdx(); ++I)
	{
		UE::PoseSearch::FFeatureVectorHelper::EncodeVector2D(Indexer.GetPoseVector(I), ChannelDataOffset,
			EncodePhase(Indexer.CalculateSampleTime(I), Indexer.GetPlayLength()));
		for (int32 F = 0; F < 2; ++F)
		{
			FVector Position, Velocity;
			const int8 Bone = F == 0 ? LeftFootIdx : RightFootIdx;
			if (!Indexer.GetSamplePosition(Position, 0.f, 0.f, I, Bone, UE::PoseSearch::RootSchemaBoneIdx,
				UE::PoseSearch::DefaultRole, UE::PoseSearch::DefaultRole)
				|| !Indexer.GetSampleVelocity(Velocity, 0.f, 0.f, I, Bone, UE::PoseSearch::RootSchemaBoneIdx,
					UE::PoseSearch::DefaultRole, UE::PoseSearch::DefaultRole, true)) { return false; }
			UE::PoseSearch::FFeatureVectorHelper::EncodeVector(Indexer.GetPoseVector(I), ChannelDataOffset + 2 + F * 3, Position, EComponentStrippingVector::None, false);
			UE::PoseSearch::FFeatureVectorHelper::EncodeVector(Indexer.GetPoseVector(I), ChannelDataOffset + 8 + F * 3, Velocity, EComponentStrippingVector::None, false);
		}
		FVector RootVelocity;
		if (!Indexer.GetSampleVelocity(RootVelocity, 0.f, 0.f, I, UE::PoseSearch::RootSchemaBoneIdx,
			UE::PoseSearch::RootSchemaBoneIdx, UE::PoseSearch::DefaultRole, UE::PoseSearch::DefaultRole, false)) { return false; }
		UE::PoseSearch::FFeatureVectorHelper::EncodeVector(Indexer.GetPoseVector(I), ChannelDataOffset + 14,
			RootVelocity, EComponentStrippingVector::None, false);
	}
	return true;
}
#endif
