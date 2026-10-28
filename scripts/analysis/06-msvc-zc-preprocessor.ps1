<#
.SYNOPSIS
  Run 06: MSVC build with the standard-conforming preprocessor /Zc:preprocessor.

.DESCRIPTION
  A full rebuild with /W4 like run 01, but without /analyze, so its warnings can be compared
  with the C4xxx warnings of run 01.
  /permissive- and /Zc:__cplusplus are already set by the Qt 6 targets in every build.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\06-msvc-zc-preprocessor.ps1

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-zc-preprocessor in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.

.PARAMETER Yes
  Start without asking; a build directory left from an earlier run is deleted.
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-zc-preprocessor" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot

if (-not (Confirm-Run 'Analysis run 06: MSVC with /Zc:preprocessor' @(
    'A full build of everything with /W4 like run 01, but with the standard-conforming'
    'preprocessor /Zc:preprocessor and without /analyze, so its warnings can be compared with'
    'those of run 01. It takes about as long as a normal build.'
    "Build directory: $BuildDir"
) "$OutDir\06-msvc-zc-preprocessor.log" $BuildDir)) { Write-Host 'Not started.'; exit 1 }

$rc = Invoke-ConformanceRun '06' 'msvc-zc-preprocessor' 'MSVC with /Zc:preprocessor' '/Zc:preprocessor' `
    '' $OutDir $BuildDir $VcpkgRoot
Write-RunEnd $rc @("Log: $OutDir\06-msvc-zc-preprocessor.log")
exit $rc
