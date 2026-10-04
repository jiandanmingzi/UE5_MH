<#
M3 regression harness. Requires generated assets and a closed Unreal Editor.
Audits first, then verifies bare-run refusal and repeat-Apply byte stability.
Does not generate damage windows or run PIE. Reports pending E1 separately.
#>
param(
    [string]$EngineRoot = 'C:\apps\UE5\UE_5.6',
    [string]$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
)
$ErrorActionPreference = 'Stop'
$telemetrySavedDir = Join-Path $ProjectRoot 'Saved'
$telemetryLogDir = Join-Path $telemetrySavedDir 'Logs'
$editorCommand = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectFile = Join-Path $ProjectRoot 'MHGZ.uproject'
if (!(Test-Path -LiteralPath $editorCommand) -or !(Test-Path -LiteralPath $projectFile)) {
    throw 'Engine executable or MHGZ.uproject not found; provide -EngineRoot/-ProjectRoot.'
}
if (Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor and other commandlet processes before the regression harness.'
}
$sequenceNames = @(
    'AS_Unsh_CaoChongChuanCi', 'AS_Unsh_CaoChongChuanci_Flying',
    'AS_Unsh_CaoChongChuanCI_Flying_Loop', 'AS_Unsh_CaoChongChuanCi_Over'
)
$assetFiles = @(
    'Content/Blueprints/Ability/InsectGlaive/GA_IG_KinsectPiercer.uasset',
    'Content/Weapons/InsectGlaive/Anims/Montage/AM_IG_KinsectPiercer.uasset',
    'Content/Weapons/InsectGlaive/Anims/Montage/AM_IG_KinsectPiercer_Over.uasset',
    'Content/Weapons/InsectGlaive/Data/Curves/CV_IG_KinsectPiercer_Start.uasset',
    'Content/Weapons/InsectGlaive/Data/Curves/CV_IG_KinsectPiercer_Dive.uasset',
    'Content/Weapons/InsectGlaive/Data/Curves/CV_IG_KinsectPiercer_Landing.uasset'
)
foreach ($sequenceName in $sequenceNames) {
    $assetFiles += "Content/Weapons/InsectGlaive/Anims/Sequences/Imported/$sequenceName.uasset"
    $assetFiles += "Content/Weapons/InsectGlaive/Anims/Sequences/Generated/${sequenceName}_IP.uasset"
}
$assetFiles += 'Content/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_Unsh_CaoChongChuanCi_Over_RM.uasset'
# Includes the actually referenced InputProfile and checks unrelated weapon DAs too.
$dataDir = Join-Path $ProjectRoot 'Content/Weapons/InsectGlaive/Data'
foreach ($asset in Get-ChildItem -LiteralPath $dataDir -File -Filter '*.uasset') {
    $assetFiles += [IO.Path]::GetRelativePath($ProjectRoot, $asset.FullName)
}
$stableAssetFiles = @($assetFiles | Sort-Object -Unique)
if ($stableAssetFiles.Count -ne 21) { throw "Expected exactly 21 stable packages, found $($stableAssetFiles.Count)." }
function Get-AssetHashes {
    $result = [ordered]@{}
    foreach ($relative in $stableAssetFiles) {
        $absolute = Join-Path $ProjectRoot $relative
        if (!(Test-Path -LiteralPath $absolute)) { throw "Required asset missing: $relative" }
        $result[$relative] = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash
    }
    return $result
}
$runs = [Collections.Generic.List[object]]::new()
function Invoke-SetupCheck([string]$runClass, [string]$mode, [int]$expectedExit, [string]$label) {
    $argsForEditor = @(
        $projectFile, "-run=$runClass", '-unattended', '-nop4', '-nosplash', '-NullRHI',
        '-DDC=InstalledNoZenLocalFallback', '-NoZenAutoLaunch', '-NoCrashDialog', '-stdout',
        "-abslog=$(Join-Path $telemetryLogDir "KinsectPiercer_M3_$label.txt")"
    )
    if ($mode) { $argsForEditor += "-$mode" }
    & $editorCommand @argsForEditor 2>&1 | Out-Null
    $actualExit = $LASTEXITCODE
    $runs.Add([ordered]@{ label = $label; commandlet = $runClass; mode = $mode; exit_code = $actualExit; expected_exit = $expectedExit })
    Write-Host "$label : exit $actualExit (expected $expectedExit)"
    if ($actualExit -ne $expectedExit) { throw "Unexpected commandlet result: $label; inspect its Saved/Logs log." }
    $currentHashes = Get-AssetHashes
    foreach ($key in $script:before.Keys) {
        if ($script:before[$key] -ne $currentHashes[$key]) { throw "$label changed stable package bytes: $key" }
    }
    if ($runClass -eq 'MHGZM5KinsectPiercerAssetSetup' -and $mode) {
        $suffix = if ($mode -eq 'Apply') { 'apply' } else { 'audit' }
        $assetAudit = Get-Content -LiteralPath (Join-Path $telemetrySavedDir "_kinsect_piercer_asset_setup_$suffix.json") -Raw | ConvertFrom-Json
        $rootMotion = $assetAudit.landing203RootMotion
        if (!$assetAudit.success -or @($assetAudit.savedPackages).Count -ne 0 -or
            !$rootMotion.sourceRootValid -or !$rootMotion.finite -or !$rootMotion.nonzeroHorizontal -or !$rootMotion.minimalZ -or
            @($rootMotion.sampleTrajectory).Count -lt 3 -or
            @($rootMotion.completeTransform.translationCm).Count -ne 3 -or
            @($rootMotion.completeTransform.rotationQuaternionXYZW).Count -ne 4) {
            throw "$label failed the nonmutating Landing203 extraction audit."
        }
        $landing = $assetAudit.presentationSequences[3]
        $landingSegment = $assetAudit.landingMontage.slots[0].segments[0]
        if (!$landing.generated.EndsWith('AS_Unsh_CaoChongChuanCi_Over_RM.AS_Unsh_CaoChongChuanCi_Over_RM') -or
            !$landing.rootMotionEnabled -or !$landing.forceRootLock -or $landing.rootMotionRootLock -ne 1 -or
            $landing.sequenceRateScale -ne 2 -or !$landing.duplicatePreservesSourceAnimationAndSkeleton -or
            !$assetAudit.landingMontage.layoutMatches -or $landingSegment.sequence -ne $landing.generated -or
            [Math]::Abs($landingSegment.runtimeSecondsReadback - 1.566650) -gt 0.000001) {
            throw "$label failed the _RM settings/layout/runtime contract."
        }
    }
}
$oldDdcPath = [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath', 'Process')
$report = [ordered]@{ success = $false; runs = $runs; hashes_before = $null; hashes_after = $null; error = $null }
try {
    [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', (Join-Path $telemetrySavedDir 'AcceptanceDDC'), 'Process')
    $before = Get-AssetHashes
    $report.hashes_before = $before
    Invoke-SetupCheck 'MHGZM5KinsectPiercerCurveSetup' 'AuditOnly' 0 'Curve_Harness_PreAudit'
    Invoke-SetupCheck 'MHGZM5KinsectPiercerAssetSetup' 'AuditOnly' 0 'Asset_Harness_PreAudit'
    foreach ($commandlet in @('MHGZM5KinsectPiercerCurveSetup', 'MHGZM5KinsectPiercerAssetSetup')) {
        $prefix = if ($commandlet -like '*Curve*') { 'Curve' } else { 'Asset' }
        Invoke-SetupCheck $commandlet '' 1 "${prefix}_BareRefused"
        Invoke-SetupCheck $commandlet 'Apply' 0 "${prefix}_Harness_Reapply"
        Invoke-SetupCheck $commandlet 'AuditOnly' 0 "${prefix}_Harness_FinalAudit"
    }
    $after = Get-AssetHashes
    $report.hashes_after = $after
    foreach ($key in $before.Keys) {
        if ($before[$key] -ne $after[$key]) { throw "Audit/repeat Apply changed package bytes: $key" }
    }
    if ($runs.Count -ne 8) { throw "Expected exactly eight commandlet checks, found $($runs.Count)." }
    $report.success = $true
    Write-Host "PASS: $($before.Count) asset hashes unchanged; source sequences and user notification packages preserved."
}
catch { $report.error = $_.Exception.Message; throw }
finally {
    [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', $oldDdcPath, 'Process')
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $telemetrySavedDir '_kinsect_piercer_setup_regression.json') -Encoding utf8
}
