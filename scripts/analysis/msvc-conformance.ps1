<#
.SYNOPSIS
  Runs 04 and 05: MSVC builds with stricter conformance switches.

.DESCRIPTION
  Each run is a full rebuild in its own build directory, with /W4 (dependency headers external,
  their warnings suppressed) like run 01, but without /analyze, so their warnings can be compared
  with the C4xxx warnings of run 01.
    04: C++20 instead of C++17 (through std-cxx20.cmake)       -> build-cxx20
    05: the standard-conforming preprocessor /Zc:preprocessor  -> build-zc-preprocessor
  /permissive- and /Zc:__cplusplus are already set by the Qt 6 targets in every build.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\msvc-conformance.ps1

.PARAMETER Only
  04 or 05 to run just one of them.

.PARAMETER OutDir
  Where the logs go. Default: ANALYSE in the repository.

.PARAMETER BuildRoot
  The folder in which the two build directories are created. Default: the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.
#>
param(
    [string]$Only = '',
    [string]$OutDir = '',
    [string]$BuildRoot = '',
    [string]$VcpkgRoot = ''
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = "$Root\ANALYSE" }
if (-not $BuildRoot) { $BuildRoot = $Root }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$common = '/W4 /external:W0 /external:anglebrackets'
$include = "$PSScriptRoot\std-cxx20.cmake" -replace '\\', '/'

$runs = @(
    @{ Id = '04'; Name = 'msvc-cxx20'; Dir = 'build-cxx20'; Title = 'MSVC with /std:c++20 instead of C++17'
       Flags = $common; Extra = "-DCMAKE_PROJECT_INCLUDE=$include" }
    @{ Id = '05'; Name = 'msvc-zc-preprocessor'; Dir = 'build-zc-preprocessor'; Title = 'MSVC with /Zc:preprocessor'
       Flags = "$common /Zc:preprocessor"; Extra = '' }
)

$rcAll = 0
foreach ($r in $runs) {
    if ($Only -and $Only -ne $r.Id) { continue }
    $bd = "$BuildRoot\$($r.Dir)"
    $log = "$OutDir\$($r.Id)-$($r.Name).log"
    $env:CFLAGS = $r.Flags
    $env:CXXFLAGS = $r.Flags
    New-Log $log "$($r.Id): $($r.Title), full rebuild in $($r.Dir)" (@(
        "# Compiler: $(Get-ClVersion)"
    ) + (Get-ToolVersions) + @("# CFLAGS/CXXFLAGS env: $($r.Flags)"))
    $rc = Invoke-Build $log $bd "$(Get-ConfigureArgs $bd $VcpkgRoot) -DENABLE_CODE_ANALYSIS=OFF $($r.Extra)"
    if (Test-Path "$bd\compile_commands.json") {
        # Records the language standard actually used, as a check of the override.
        Add-LogLine $log "# Standard switches in compile_commands.json: $(Get-StdSwitches $bd)"
    }
    Add-LogLine $log "# End: $(Get-Stamp)"
    if ($rc -ne 0) { $rcAll = $rc }
}
exit $rcAll
