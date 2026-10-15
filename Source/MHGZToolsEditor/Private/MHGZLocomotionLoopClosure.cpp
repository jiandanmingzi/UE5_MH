// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"

namespace
{
// Cubic correction fades to zero with zero tangent at the inner edge. Its
// endpoint and adjacent KEY are exact constraints (not an approximate tangent).
FVector BoundaryOffset(const FVector& Endpoint, const FVector& Adjacent, double U, double KeyU)
{
	auto H0 = [](double T) { return 2*T*T*T - 3*T*T + 1; };
	auto H1 = [](double T) { return T*T*T - 2*T*T + T; };
	return Endpoint * H0(U) + (Adjacent - Endpoint * H0(KeyU)) * (H1(U) / H1(KeyU));
}

FVector QuatLog(FQuat Q)
{
	Q.Normalize();
	if (Q.W < 0) { Q = Q * -1.; }
	const FQuat L = Q.Log();
	return FVector(L.X, L.Y, L.Z);
}
FQuat QuatExp(const FVector& V) { return FQuat(V.X,V.Y,V.Z,0).Exp().GetNormalized(); }

struct FLoopTrack
{
	FName Name;
	TArray<FTransform> Keys;
};
}

bool UMHGZAnimationSeamLibrary::CloseLocomotionLoop(UAnimSequence* Sequence, float WindowSeconds)
{
	if (!Sequence || !Sequence->bLoop || !Sequence->GetSkeleton() || !FMath::IsFinite(WindowSeconds)
		|| WindowSeconds <= 0 || !Sequence->GetName().StartsWith(TEXT("AS_"))
		|| !Sequence->GetName().Contains(TEXT("_Loop"))) { return false; }
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	if (!Model || Model->GetNumberOfFrames() < 8) { return false; }
	const int32 N = Model->GetNumberOfFrames();
	const int32 Window = FMath::Clamp(FMath::CeilToInt(WindowSeconds * Model->GetFrameRate().AsDecimal()),2,N/3);
	const FName Root = Sequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0);
	TArray<FName> Names;
	Model->GetBoneTrackNames(Names);
	if (!Names.Contains(Root)) { return false; }
	TArray<FLoopTrack> Tracks;
	for (FName Name : Names)
	{
		FLoopTrack& Track = Tracks.AddDefaulted_GetRef();
		Track.Name = Name;
		Model->GetBoneTrackTransforms(Name, Track.Keys);
		if (Track.Keys.Num() != N+1) { return false; }
		const TArray<FTransform> Original = Track.Keys;
		const FTransform& A = Original[0];
		const FTransform& B = Original[N];
		const bool bRoot = Name == Root;
		// These locomotion clips translate but do not accumulate a heading/scale change.
		// A turning loop needs an explicit different contract, not a root rotation reset.
		if (bRoot && (!A.GetRotation().Equals(B.GetRotation(), .0001)
			|| !A.GetScale3D().Equals(B.GetScale3D(), .0001))) { return false; }
		const FVector P = .5 * (A.GetTranslation() + B.GetTranslation());
		const FVector V = .5 * (Original[1].GetTranslation()-A.GetTranslation()
			+ B.GetTranslation()-Original[N-1].GetTranslation());
		const FVector S = .5 * (A.GetScale3D() + B.GetScale3D());
		const FVector SV = .5 * (Original[1].GetScale3D()-A.GetScale3D()
			+ B.GetScale3D()-Original[N-1].GetScale3D());
		const FQuat Q = FQuat::Slerp(A.GetRotation(),B.GetRotation(),.5).GetNormalized();
		const FQuat QV = QuatExp(.5 * (QuatLog(Original[1].GetRotation()*A.GetRotation().Inverse())
			+ QuatLog(B.GetRotation()*Original[N-1].GetRotation().Inverse())));
		for (int32 End = 0; End < 2; ++End)
		{
			const int32 E = End ? N : 0;
			const int32 Adj = End ? N-1 : 1;
			const double Sign = End ? -1. : 1.;
			const FVector TargetP = bRoot ? Original[E].GetTranslation() : P;
			const FVector D0 = TargetP-Original[E].GetTranslation();
			const FVector D1 = TargetP+Sign*V-Original[Adj].GetTranslation();
			const FVector S0 = S-Original[E].GetScale3D();
			const FVector S1 = S+Sign*SV-Original[Adj].GetScale3D();
			const FVector R0 = QuatLog(Q*Original[E].GetRotation().Inverse());
			const FVector R1 = QuatLog((End ? QV.Inverse()*Q : QV*Q)*Original[Adj].GetRotation().Inverse());
			for (int32 K = 0; K < Window; ++K)
			{
				const int32 I = End ? N-K : K;
				const double U = double(K)/Window, KeyU = 1./Window;
				Track.Keys[I].SetTranslation(Original[I].GetTranslation()+BoundaryOffset(D0,D1,U,KeyU));
				if (!bRoot)
				{
					Track.Keys[I].SetRotation((QuatExp(BoundaryOffset(R0,R1,U,KeyU))*Original[I].GetRotation()).GetNormalized());
					Track.Keys[I].SetScale3D(Original[I].GetScale3D()+BoundaryOffset(S0,S1,U,KeyU));
				}
			}
		}
	}
	Sequence->Modify();
	// GetController obtains the controller FROM this model, including Sequencer
	// models. Never bind an AnimDataController to an AnimationSequencerDataModel.
	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Close locomotion loop boundary pose and velocity")),false);
	bool bSuccess = true;
	for (const FLoopTrack& Track : Tracks)
	{
		TArray<FVector3f> P,S;
		TArray<FQuat4f> R;
		for (const FTransform& Key : Track.Keys)
		{ P.Add(FVector3f(Key.GetTranslation())); R.Add(FQuat4f(Key.GetRotation())); S.Add(FVector3f(Key.GetScale3D())); }
		bSuccess &= Controller.SetBoneTrackKeys(Track.Name,P,R,S,false);
	}
	Controller.CloseBracket(false);
	Sequence->MarkPackageDirty();
	return bSuccess;
}
