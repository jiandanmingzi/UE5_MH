// Copyright MHGZ Project. All Rights Reserved.

#include "Animation/MHGZPoseSearchTelemetryExport.h"

#include "HAL/FileManager.h"
#include "MHGZ.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchCustomVersion.h"
#include "PoseSearch/Trace/PoseSearchTraceLogger.h"
#include "Serialization/MemoryReader.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/DataStream.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace
{
using namespace UE::PoseSearch;

constexpr int32 PoseSearchTraceCandidateLimitPerDatabase = 256;
constexpr int32 CSVFlushRowCount = 2048;

FString ToCSVString(const FString& Value)
{
	FString Escaped = Value;
	// Full_Vertical channel labels may contain line breaks. A quoted CSV field may
	// legally contain those, but keeping every diagnostic record on one physical
	// line is essential for command-line tools and model readers.
	Escaped.ReplaceInline(TEXT("\r"), TEXT(" "));
	Escaped.ReplaceInline(TEXT("\n"), TEXT(" / "));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

FString ToCSVFloat(const float Value)
{
	return FString::Printf(TEXT("%.6f"), Value);
}

FString ToCSVDouble(const double Value)
{
	return FString::Printf(TEXT("%.6f"), Value);
}

FString ToCandidateFlagsString(const EPoseCandidateFlags Flags)
{
	TArray<FString> Names;
	const auto AddIfSet = [&Names, Flags](const EPoseCandidateFlags Flag, const TCHAR* Name)
	{
		if (EnumHasAnyFlags(Flags, Flag))
		{
			Names.Add(Name);
		}
	};
	AddIfSet(EPoseCandidateFlags::Valid_Pose, TEXT("ValidPose"));
	AddIfSet(EPoseCandidateFlags::Valid_ContinuingPose, TEXT("ValidContinuingPose"));
	AddIfSet(EPoseCandidateFlags::Valid_CurrentPose, TEXT("ValidCurrentPose"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_PoseJumpThresholdTime, TEXT("DiscardedPoseJumpThreshold"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_PoseReselectHistory, TEXT("DiscardedPoseReselectHistory"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_BlockTransition, TEXT("DiscardedBlockTransition"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_PoseFilter, TEXT("DiscardedPoseFilter"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_AssetIdxFilter, TEXT("DiscardedAssetFilter"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_Search, TEXT("DiscardedSearch"));
	AddIfSet(EPoseCandidateFlags::DiscardedBy_AssetReselection, TEXT("DiscardedAssetReselection"));
	return Names.IsEmpty() ? TEXT("None") : FString::Join(Names, TEXT("|"));
}

float SumRange(const TConstArrayView<float> Values, const int32 Offset, const int32 Cardinality)
{
	if (Offset < 0 || Cardinality <= 0 || Offset + Cardinality > Values.Num())
	{
		return 0.0f;
	}

	float Result = 0.0f;
	for (int32 Index = Offset; Index < Offset + Cardinality; ++Index)
	{
		Result += Values[Index];
	}
	return Result;
}

class FBufferedCSVWriter
{
public:
	bool Initialize(const FString& InFilePath, const FString& Header)
	{
		FilePath = InFilePath;
		return FFileHelper::SaveStringToFile(Header, *FilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool Add(const FString& Row)
	{
		Rows.Add(Row);
		return Rows.Num() < CSVFlushRowCount || Flush();
	}

	bool Flush()
	{
		if (Rows.IsEmpty())
		{
			return true;
		}

		const FString Text = FString::Join(Rows, LINE_TERMINATOR) + LINE_TERMINATOR;
		const bool bSuccess = FFileHelper::SaveStringToFile(Text, *FilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		if (bSuccess)
		{
			Rows.Reset();
		}
		return bSuccess;
	}

private:
	FString FilePath;
	TArray<FString> Rows;
};

struct FPoseSearchTraceSample
{
	double TraceTimeSeconds = 0.0;
	FTraceMotionMatchingStateMessage Message;
};

class FMHGZPoseSearchTraceAnalyzer final : public UE::Trace::IAnalyzer
{
public:
	explicit FMHGZPoseSearchTraceAnalyzer(TArray<FPoseSearchTraceSample>& InSamples)
		: Samples(InSamples)
	{
	}

private:
	enum : uint16
	{
		RouteId_MotionMatchingState,
		RouteId_MotionMatchingState2,
		RouteId_MotionMatchingState3,
	};

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
	{
		FInterfaceBuilder& Builder = Context.InterfaceBuilder;
		Builder.RouteEvent(RouteId_MotionMatchingState, "PoseSearch", "MotionMatchingState");
		Builder.RouteEvent(RouteId_MotionMatchingState2, "PoseSearch", "MotionMatchingState2");
		Builder.RouteEvent(RouteId_MotionMatchingState3, "PoseSearch", "MotionMatchingState3");
	}

	virtual bool OnEvent(const uint16 RouteId, const EStyle Style, const FOnEventContext& Context) override
	{
		FCustomVersionContainer CustomVersions;
		switch (RouteId)
		{
		case RouteId_MotionMatchingState:
			break;
		case RouteId_MotionMatchingState2:
			CustomVersions.SetVersion(FPoseSearchCustomVersion::GUID,
				FPoseSearchCustomVersion::DeprecatedTrajectoryTypes, TEXT("Dev-PoseSearch-Version"));
			break;
		case RouteId_MotionMatchingState3:
			CustomVersions.SetVersion(FPoseSearchCustomVersion::GUID,
				FPoseSearchCustomVersion::AddedInterruptModeToDebugger, TEXT("Dev-PoseSearch-Version"));
			break;
		default:
			return false;
		}

		FPoseSearchTraceSample& Sample = Samples.AddDefaulted_GetRef();
		FMemoryReaderView Archive(Context.EventData.GetArrayView<uint8>("Data"));
		Archive.SetCustomVersions(CustomVersions);
		Archive << Sample.Message;
		Sample.TraceTimeSeconds = Context.EventTime.AsSeconds(Sample.Message.Cycle);
		return true;
	}

	TArray<FPoseSearchTraceSample>& Samples;
};

bool WriteStatusFile(const FString& OutputDirectory, const FString& Status)
{
	return FFileHelper::SaveStringToFile(Status, *FPaths::Combine(OutputDirectory,
		TEXT("PoseSearchDetail-Status.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

bool FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(const FString& TraceFilePath,
	const FString& OutputDirectory, const int32 TopCandidatesPerDatabase,
	const TMap<uint64, FString>& DatabasePathOverrides)
{
	if (!FPaths::FileExists(TraceFilePath))
	{
		UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Pose Search detail trace was not found: %s"),
			*TraceFilePath);
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not create Pose Search detail directory: %s"),
			*OutputDirectory);
		return false;
	}

	UE::Trace::FFileDataStream DataStream;
	if (!DataStream.Open(*TraceFilePath))
	{
		UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not open Pose Search detail trace: %s"),
			*TraceFilePath);
		return false;
	}

	TArray<FPoseSearchTraceSample> Samples;
	FMHGZPoseSearchTraceAnalyzer Analyzer(Samples);
	UE::Trace::FAnalysisContext AnalysisContext;
	AnalysisContext.AddAnalyzer(Analyzer);
	UE::Trace::FAnalysisProcessor Processor = AnalysisContext.Process(DataStream);
	Processor.Wait();

	const FString CandidateFilePath = FPaths::Combine(OutputDirectory, TEXT("PoseSearchCandidates.csv"));
	const FString ChannelCostFilePath = FPaths::Combine(OutputDirectory, TEXT("PoseSearchChannelCosts.csv"));
	FBufferedCSVWriter CandidateWriter;
	FBufferedCSVWriter ChannelWriter;
	const FString CandidateHeader = TEXT("TraceTimeSeconds,RecordingTimeSeconds,AnimInstanceTraceId,NodeId,DatabaseTraceId,DatabasePath,Rank,TotalCandidatesCaptured,CandidateCaptureLimit,PoseIndex,AnimationAsset,AssetTime,Flags,FlagMask,TotalCost,DissimilarityCost,NotifyCostAddend,ContinuingPoseCostAddend,ContinuingInteractionCostAddend") LINE_TERMINATOR;
	const FString ChannelHeader = TEXT("TraceTimeSeconds,NodeId,DatabaseTraceId,DatabasePath,Rank,PoseIndex,ChannelDepth,ChannelLabel,ChannelClass,DataOffset,Cardinality,FeatureCost") LINE_TERMINATOR;
	if (!CandidateWriter.Initialize(CandidateFilePath, CandidateHeader)
		|| !ChannelWriter.Initialize(ChannelCostFilePath, ChannelHeader))
	{
		UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not create Pose Search detail CSV output."));
		return false;
	}

	const int32 MaxCandidates = FMath::Clamp(TopCandidatesPerDatabase, 1,
		PoseSearchTraceCandidateLimitPerDatabase);
	TMap<uint64, const UPoseSearchDatabase*> LoadedOverrideDatabases;
	for (const TPair<uint64, FString>& DatabasePathOverride : DatabasePathOverrides)
	{
		UPoseSearchDatabase* const Database = LoadObject<UPoseSearchDatabase>(nullptr,
			*DatabasePathOverride.Value);
		if (!Database)
		{
			UE_LOG(LogMHGZMM, Warning,
				TEXT("[RuntimeTelemetry] Could not load Pose Search database override %llu: %s"),
				static_cast<unsigned long long>(DatabasePathOverride.Key), *DatabasePathOverride.Value);
			continue;
		}

		const ERequestAsyncBuildFlag BuildFlags = ERequestAsyncBuildFlag::NewRequest
			| ERequestAsyncBuildFlag::WaitForCompletion;
		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, BuildFlags)
			!= EAsyncBuildIndexResult::Success)
		{
			UE_LOG(LogMHGZMM, Warning,
				TEXT("[RuntimeTelemetry] Could not build Pose Search database override %llu: %s"),
				static_cast<unsigned long long>(DatabasePathOverride.Key), *DatabasePathOverride.Value);
			continue;
		}
		LoadedOverrideDatabases.Add(DatabasePathOverride.Key, Database);
	}
	int32 ExportedCandidates = 0;
	int32 ExportedChannelRows = 0;
	int32 UnresolvedDatabaseEntries = 0;

	for (const FPoseSearchTraceSample& Sample : Samples)
	{
		for (const FTraceMotionMatchingStateDatabaseEntry& DatabaseEntry : Sample.Message.DatabaseEntries)
		{
			const uint64 DatabaseTraceId = static_cast<uint64>(DatabaseEntry.DatabaseId);
			const UPoseSearchDatabase* Database =
				FTraceMotionMatchingStateMessage::GetObjectFromId<UPoseSearchDatabase>(DatabaseEntry.DatabaseId);
			if (!Database)
			{
				if (const UPoseSearchDatabase* const* const DatabaseOverride =
					LoadedOverrideDatabases.Find(DatabaseTraceId))
				{
					Database = *DatabaseOverride;
				}
			}
			const FString DatabasePath = Database ? Database->GetPathName()
				: FString::Printf(TEXT("UnresolvedTraceObject_%llu"),
					static_cast<unsigned long long>(DatabaseTraceId));
			if (!Database)
			{
				++UnresolvedDatabaseEntries;
			}

			TArray<const FTraceMotionMatchingStatePoseEntry*> SortedCandidates;
			SortedCandidates.Reserve(DatabaseEntry.PoseEntries.Num());
			for (const FTraceMotionMatchingStatePoseEntry& PoseEntry : DatabaseEntry.PoseEntries)
			{
				if (PoseEntry.DbPoseIdx != INDEX_NONE)
				{
					SortedCandidates.Add(&PoseEntry);
				}
			}
			SortedCandidates.Sort([](const FTraceMotionMatchingStatePoseEntry& A,
				const FTraceMotionMatchingStatePoseEntry& B)
			{
				return static_cast<float>(A.Cost) < static_cast<float>(B.Cost);
			});

			TConstArrayView<float> DynamicWeightsSqrt;
			TAlignedArray<float> DynamicWeightsSqrtBuffer;
			const FSearchIndex* SearchIndex = nullptr;
			if (Database)
			{
				SearchIndex = &Database->GetSearchIndex();
				DynamicWeightsSqrtBuffer.SetNumUninitialized(SearchIndex->WeightsSqrt.Num());
				DynamicWeightsSqrt = Database->CalculateDynamicWeightsSqrt(DynamicWeightsSqrtBuffer);
			}

			const int32 OutputCandidateCount = FMath::Min(MaxCandidates, SortedCandidates.Num());
			for (int32 CandidateIndex = 0; CandidateIndex < OutputCandidateCount; ++CandidateIndex)
			{
				const FTraceMotionMatchingStatePoseEntry& PoseEntry = *SortedCandidates[CandidateIndex];
				const float TotalCost = static_cast<float>(PoseEntry.Cost);
				float NotifyCostAddend = 0.0f;
				float ContinuingPoseCostAddend = 0.0f;
				float ContinuingInteractionCostAddend = 0.0f;
#if WITH_EDITORONLY_DATA
				NotifyCostAddend = PoseEntry.Cost.GetNotifyCostAddend();
				ContinuingPoseCostAddend = PoseEntry.Cost.GetContinuingPoseCostAddend();
				ContinuingInteractionCostAddend = PoseEntry.Cost.GetContinuingInteractionCostAddend();
#endif
				const float DissimilarityCost = FPoseSearchCost::IsCostValid(TotalCost)
					? TotalCost - NotifyCostAddend - ContinuingPoseCostAddend - ContinuingInteractionCostAddend
					: TotalCost;

				FString AnimationAssetPath;
				float AssetTime = 0.0f;
				TArray<float> CostVector;
				if (Database && SearchIndex && SearchIndex->PoseMetadata.IsValidIndex(PoseEntry.DbPoseIdx))
				{
					const uint32 AssetIndex = SearchIndex->PoseMetadata[PoseEntry.DbPoseIdx].GetAssetIndex();
					if (const UObject* AnimationAsset = Database->GetAnimationAsset(AssetIndex))
					{
						AnimationAssetPath = AnimationAsset->GetPathName();
					}
					AssetTime = Database->GetRealAssetTime(PoseEntry.DbPoseIdx);

					TArray<float> ReconstructedPoseValues;
					const TConstArrayView<float> PoseValues = SearchIndex->GetPoseValuesSafe(PoseEntry.DbPoseIdx,
						ReconstructedPoseValues);
					if (DatabaseEntry.QueryVector.Num() == PoseValues.Num()
						&& DynamicWeightsSqrt.Num() == PoseValues.Num())
					{
						CostVector.SetNumZeroed(PoseValues.Num());
						CompareFeatureVectors(PoseValues, DatabaseEntry.QueryVector, DynamicWeightsSqrt, CostVector);
					}
				}

				const uint32 FlagMask = static_cast<uint32>(PoseEntry.PoseCandidateFlags);
				const FString CandidateRow = FString::Printf(TEXT("%s,%s,%llu,%d,%llu,%s,%d,%d,%d,%d,%s,%s,%s,%u,%s,%s,%s,%s,%s"),
					*ToCSVDouble(Sample.TraceTimeSeconds), *ToCSVFloat(Sample.Message.RecordingTime),
					static_cast<unsigned long long>(Sample.Message.AnimInstanceId), Sample.Message.NodeId,
					static_cast<unsigned long long>(DatabaseEntry.DatabaseId), *ToCSVString(DatabasePath),
					CandidateIndex + 1, SortedCandidates.Num(), PoseSearchTraceCandidateLimitPerDatabase,
					PoseEntry.DbPoseIdx, *ToCSVString(AnimationAssetPath), *ToCSVFloat(AssetTime),
					*ToCSVString(ToCandidateFlagsString(PoseEntry.PoseCandidateFlags)), FlagMask,
					*ToCSVFloat(TotalCost), *ToCSVFloat(DissimilarityCost), *ToCSVFloat(NotifyCostAddend),
					*ToCSVFloat(ContinuingPoseCostAddend), *ToCSVFloat(ContinuingInteractionCostAddend));
				if (!CandidateWriter.Add(CandidateRow))
				{
					UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not append Pose Search candidate CSV."));
					return false;
				}
				++ExportedCandidates;

				if (!Database || CostVector.IsEmpty())
				{
					continue;
				}

				const auto WriteChannelCost = [&](const auto& Self, const UPoseSearchFeatureChannel* Channel,
					const int32 Depth) -> bool
				{
					if (!Channel)
					{
						return true;
					}
					TLabelBuilder LabelBuilder;
					const FString ChannelLabel = Channel->GetLabel(LabelBuilder, ELabelFormat::Full_Vertical).ToString();
					const FString ChannelRow = FString::Printf(TEXT("%s,%d,%llu,%s,%d,%d,%d,%s,%s,%d,%d,%s"),
						*ToCSVDouble(Sample.TraceTimeSeconds), Sample.Message.NodeId,
						static_cast<unsigned long long>(DatabaseEntry.DatabaseId), *ToCSVString(DatabasePath),
						CandidateIndex + 1, PoseEntry.DbPoseIdx, Depth, *ToCSVString(ChannelLabel),
						*ToCSVString(Channel->GetClass()->GetPathName()), Channel->GetChannelDataOffset(),
						Channel->GetChannelCardinality(), *ToCSVFloat(SumRange(CostVector,
							Channel->GetChannelDataOffset(), Channel->GetChannelCardinality())));
					if (!ChannelWriter.Add(ChannelRow))
					{
						return false;
					}
					++ExportedChannelRows;
					for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannel : Channel->GetSubChannels())
					{
						if (!Self(Self, SubChannel.Get(), Depth + 1))
						{
							return false;
						}
					}
					return true;
				};
				for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Database->Schema->GetChannels())
				{
					if (!WriteChannelCost(WriteChannelCost, Channel.Get(), 0))
					{
						UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not append Pose Search channel-cost CSV."));
						return false;
					}
				}
			}
		}
	}

	if (!CandidateWriter.Flush() || !ChannelWriter.Flush())
	{
		UE_LOG(LogMHGZMM, Error, TEXT("[RuntimeTelemetry] Could not flush Pose Search detail CSV output."));
		return false;
	}

	const FString Status = FString::Printf(
		TEXT("Status: Exported\nTrace: %s\nTrace search samples: %d\nCandidate rows: %d\nChannel-cost rows: %d\nUnresolved database entries: %d\n")
		TEXT("Candidate ranking uses the engine-recorded total cost. Channel costs are reconstructed from the trace query vector and current PSS dynamic weights; keep PSS/schema weights unchanged until export.\n"),
		*TraceFilePath, Samples.Num(), ExportedCandidates, ExportedChannelRows, UnresolvedDatabaseEntries);
	WriteStatusFile(OutputDirectory, Status);
	UE_LOG(LogMHGZMM, Log, TEXT("[RuntimeTelemetry] Exported Pose Search detail: %d candidates, %d channel rows."),
		ExportedCandidates, ExportedChannelRows);
	return true;
}

#else

bool FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(const FString& TraceFilePath,
	const FString& OutputDirectory, const int32 TopCandidatesPerDatabase)
{
	return false;
}

#endif // WITH_EDITOR
