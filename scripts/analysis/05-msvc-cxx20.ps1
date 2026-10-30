<#
.SYNOPSIS
  Run 05: MSVC build with C++20 instead of C++17.

.DESCRIPTION
  A full rebuild with /W4 like run 01, but without /analyze, so its warnings can be compared
  with the C4xxx warnings of run 01. The project sets CMAKE_CXX_STANDARD 17 as a normal
  variable, so C++20 is switched on through support\std-cxx20.cmake.
  /permissive- and /Zc:__cplusplus are already set by the Qt 6 targets in every build.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\05-msvc-cxx20.ps1

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-cxx20 in the repository.

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
if (-not $BuildDir) { $BuildDir = "$Root\build-cxx20" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$include = "$PSScriptRoot\support\std-cxx20.cmake" -replace '\\', '/'

if (-not (Confirm-Run 'Analysis run 05: MSVC with C++20' @(
    'A full build of everything with /W4 like run 01, but in C++20 instead of C++17 and without'
    '/analyze, so its warnings can be compared with those of run 01. It takes about as long as a'
    'normal build.'
    "Build directory: $BuildDir"
) "$OutDir\05-msvc-cxx20.log" $BuildDir)) { Write-Host 'Not started.'; exit 1 }

$rc = Invoke-ConformanceRun '05' 'msvc-cxx20' 'MSVC with /std:c++20 instead of C++17' '' `
    "-DCMAKE_PROJECT_INCLUDE=$include" $OutDir $BuildDir $VcpkgRoot
Write-RunEnd $rc @("Log: $OutDir\05-msvc-cxx20.log")
exit $rc
