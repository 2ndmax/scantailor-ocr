<#
.SYNOPSIS
  Run 01: full MSVC build with /W4 and the compiler's static code analysis (/analyze).

.DESCRIPTION
  Configures a fresh build directory with ENABLE_CODE_ANALYSIS=ON and builds everything,
  including the tests and the dev tools. The analysis results are also written as SARIF,
  one file per translation unit; they are collected into <OutDir>\01-msvc-analyze-sarif.
  The build directory is reused by the clang-tidy and cppcheck runs (its compile_commands.json).

    powershell -ExecutionPolicy Bypass -File scripts\analysis\msvc-analyze.ps1

.PARAMETER OutDir
  Where the log and the SARIF files go. Default: ANALYSE in the repository.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-analysis in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = ''
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = "$Root\ANALYSE" }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$log = "$OutDir\01-msvc-analyze.log"
$sarif = "$OutDir\01-msvc-analyze-sarif"

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
exit $rc
