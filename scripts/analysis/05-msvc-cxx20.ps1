<#
.SYNOPSIS
  Run 05: MSVC build with C++20 instead of C++17.

.DESCRIPTION
  A full rebuild with /W4 like run 01, but without /analyze, so its warnings can be compared
  with the C4xxx warnings of run 01. The project sets CMAKE_CXX_STANDARD 17 as a normal
  variable, so C++20 is switched on through std-cxx20.cmake.
  /permissive- and /Zc:__cplusplus are already set by the Qt 6 targets in every build.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\05-msvc-cxx20.ps1

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-cxx20 in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = ''
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-cxx20" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$include = "$PSScriptRoot\std-cxx20.cmake" -replace '\\', '/'

exit (Invoke-ConformanceRun '05' 'msvc-cxx20' 'MSVC with /std:c++20 instead of C++17' '' `
    "-DCMAKE_PROJECT_INCLUDE=$include" $OutDir $BuildDir $VcpkgRoot)
