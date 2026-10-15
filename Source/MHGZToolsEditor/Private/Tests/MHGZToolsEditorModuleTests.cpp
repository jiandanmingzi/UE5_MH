// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commandlets/Commandlet.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZToolsEditorClassPathsTest,
	"MHGZ.Maintenance.ToolsEditor.ClassPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZToolsEditorClassPathsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TCHAR* ToolNames[] = {
		TEXT("MHGZActionVisualPitchGraphCommandlet"),
		TEXT("MHGZAerialHandoffSetupCommandlet"),
		TEXT("MHGZAirDodgeMontageSetupCommandlet"),
		TEXT("MHGZDodgeBlendInSetupCommandlet"),
		TEXT("MHGZE4ActionExitAuditCommandlet"),
		TEXT("MHGZE4ActionIdleAssetSetupCommandlet"),
		TEXT("MHGZE4ActionIdleRoutingCommandlet"),
		TEXT("MHGZE4AnimBlueprintAuditCommandlet"),
		TEXT("MHGZE4ExitAssetSetupCommandlet"),
		TEXT("MHGZE4ExitCallbackPolishCommandlet"),
		TEXT("MHGZE4ExitTransitionCommandlet"),
		TEXT("MHGZE4HandoffNotifySetupCommandlet"),
		TEXT("MHGZE4PreSearchRoutingCommandlet"),
		TEXT("MHGZE4SheathedExitGraphFixupCommandlet"),
		TEXT("MHGZE4SheathedMacroNamingCommandlet"),
		TEXT("MHGZE4TailStartTimeRepairCommandlet"),
		TEXT("MHGZE64DamageNumberSetupCommandlet"),
		TEXT("MHGZKinsectActionLayerCommandlet"),
		TEXT("MHGZM421LoopCandidateAssetSetupCommandlet"),
		TEXT("MHGZM5DescendingThrustAssetSetupCommandlet"),
		TEXT("MHGZM5KinsectPiercerAssetSetupCommandlet"),
		TEXT("MHGZM5KinsectPiercerCurveSetupCommandlet"),
		TEXT("MHGZM5KinsectSlashAssetSetupCommandlet"),
		TEXT("MHGZM5KinsectSlashCurveSetupCommandlet"),
		TEXT("MHGZM631MarkConfigCommandlet"),
		TEXT("MHGZM636VisualAssetSetupCommandlet"),
		TEXT("MHGZM63PowderVortexAssetSetupCommandlet"),
		TEXT("MHGZM64UISubsystemAuditCommandlet"),
		TEXT("MHGZM6AssetSetupCommandlet"),
		TEXT("MHGZM6BaselineAuditCommandlet"),
		TEXT("MHGZM6RecordedHunterFlightSetupCommandlet"),
		TEXT("MHGZM6ReleaseFacingNotifyCommandlet"),
		TEXT("MHGZPMM4AssetFixupCommandlet"),
		TEXT("MHGZPMM4AssetReportCommandlet"),
		TEXT("MHGZPMM5AnimGraphFixupCommandlet"),
		TEXT("MHGZPMM7AssetFixupCommandlet"),
		TEXT("MHGZPMMAssetFixupCommandlet"),
		TEXT("MHGZPMMExtendedStopCommandlet"),
		TEXT("MHGZPMMNotifyTrackRepairCommandlet"),
		TEXT("MHGZPoleVaultMontageSetupCommandlet"),
		TEXT("MHGZPoseSearchTelemetryExportCommandlet"),
		TEXT("MHGZStripPresentationRootMotionCommandlet"),
		TEXT("MHGZWuTaMontageSetupCommandlet")
	};
	for (const TCHAR* ToolName : ToolNames)
	{
		const FString NewPath = FString(TEXT("/Script/MHGZToolsEditor.")) + ToolName;
		UClass* ToolClass = FindObject<UClass>(nullptr, *NewPath);
		if (!TestNotNull(*NewPath, ToolClass))
		{
			continue;
		}
		TestEqual(TEXT("Commandlet belongs to editor tools module"), ToolClass->GetPathName(), NewPath);
		TestEqual(TEXT("Short commandlet class name remains unchanged"), ToolClass->GetName(), FString(ToolName));
		TestTrue(TEXT("Registered tool is a commandlet"), ToolClass->IsChildOf(UCommandlet::StaticClass()));

		// Serialized/soft references use CoreRedirects. Literal StaticLoadClass names
		// (including Python load_class) must be updated by their callers instead.
		FSoftClassPath LegacyPath(FString(TEXT("/Script/MHGZ.")) + ToolName);
		TestTrue(TEXT("Legacy class redirect is registered"), LegacyPath.FixupCoreRedirects());
		TestEqual(TEXT("Legacy path redirects exactly"), LegacyPath.ToString(), NewPath);
		TestTrue(TEXT("Redirected legacy class resolves"), LegacyPath.TryLoadClass<UCommandlet>() == ToolClass);

		// Inspect defaults only. Never execute a migrated asset-writing Main here.
		const UCommandlet* Defaults = ToolClass->GetDefaultObject<UCommandlet>();
		if (TestNotNull(TEXT("Commandlet defaults exist"), Defaults))
		{
			const UCommandlet* BaseDefaults = GetDefault<UCommandlet>();
			const bool bUsesBaseDefaults = FString(ToolName) == TEXT("MHGZStripPresentationRootMotionCommandlet");
			// Preserve the two historical constructor exceptions, not standardize them.
			TestEqual(TEXT("Commandlet editor default remains unchanged"), bool(Defaults->IsEditor),
				bUsesBaseDefaults ? bool(BaseDefaults->IsEditor) : true);
			TestEqual(TEXT("Commandlet client default remains unchanged"), bool(Defaults->IsClient),
				bUsesBaseDefaults ? bool(BaseDefaults->IsClient) : false);
			TestEqual(TEXT("Commandlet console default remains unchanged"), bool(Defaults->LogToConsole),
				bUsesBaseDefaults ? bool(BaseDefaults->LogToConsole) : true);
			const bool bExpectedServer = bUsesBaseDefaults
				|| FString(ToolName) == TEXT("MHGZActionVisualPitchGraphCommandlet")
				? bool(BaseDefaults->IsServer) : false;
			TestEqual(TEXT("Commandlet server default remains unchanged"), bool(Defaults->IsServer), bExpectedServer);
		}
	}

	const TCHAR* RuntimePaths[] = {
		TEXT("/Script/MHGZ.MHGZMainHUDWidget"),
		TEXT("/Script/MHGZ.MHGZAttributeBarWidget"),
		TEXT("/Script/MHGZ.MHGZCrosshairWidget"),
		TEXT("/Script/MHGZ.MHGZInsectGlaiveResourceWidget"),
		TEXT("/Script/MHGZ.MHGZUIAssetSetupLibrary"),
		TEXT("/Script/MHGZ.MHGZMotionMatchingAnimInstance"),
		TEXT("/Script/MHGZ.MHGZAttackAbility")
	};
	for (const TCHAR* Path : RuntimePaths)
	{
		UClass* RuntimeClass = FindObject<UClass>(nullptr, Path);
		if (TestNotNull(Path, RuntimeClass))
		{
			TestEqual(TEXT("Runtime class path remains unchanged"), RuntimeClass->GetPathName(), FString(Path));
		}
	}
	return true;
}

#endif
