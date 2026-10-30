<#
.SYNOPSIS
  Run 01: full MSVC build with /W4 and the compiler's static code analysis (/analyze).

.DESCRIPTION
  Configures a fresh build directory with ENABLE_CODE_ANALYSIS=ON and builds everything,
  including the tests and the dev tools. The analysis results are also written as SARIF,
  one file per translation unit; they are collected into <OutDir>\01-msvc-analyze-sarif.
  The build directory is reused by the runs 02 (clang-tidy), 03 (cppcheck) and 04 (EDG), which
  read its compile_commands.json.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\01-msvc-analyze.ps1

.PARAMETER OutDir
  Where the log and the SARIF files go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-analysis in the repository.

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
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$log = "$OutDir\01-msvc-analyze.log"
$sarif = "$OutDir\01-msvc-analyze-sarif"

if (-not (Confirm-Run 'Analysis run 01: MSVC with /W4 and /analyze' @(
    'A full build of everything (program, tests, dev tools) with the warning level /W4 and the'
    'static code analysis of the compiler (/analyze). It takes considerably longer than a normal'
    'build. Its build directory is used by the runs 02, 03 and 04 afterwards.'
    "Build directory: $BuildDir"
) $log $BuildDir)) { Write-Host 'Not started.'; exit 1 }

# Picked up by CMake on the first configure and appended to the default flags.
$env:CFLAGS = '/analyze:log:format:sarif'
$env:CXXFLAGS = $env:CFLAGS

New-Log $log '01: MSVC /W4 /analyze via ENABLE_CODE_ANALYSIS=ON, full rebuild' (@(
    "# Build directory: $BuildDir"
    "# SARIF files (one per translation unit): $sarif"
    "# Compiler: $(Get-ClVersion)"
) + (Get-ToolVersions) + @("# CFLAGS/CXXFLAGS env: $env:CXXFLAGS"))

$rc = Invoke-Build $log $BuildDir "$(Get-ConfigureArgs $BuildDir $VcpkgRoot) -DENABLE_CODE_ANALYSIS=ON"
if ($rc -eq 0) {
    robocopy $BuildDir $sarif *.sarif /S /NJH /NJS /NDL /NFL /NP | Out-Null
}
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log", "SARIF files: $sarif")
exit $rc
