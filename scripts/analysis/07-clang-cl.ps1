<#
.SYNOPSIS
  Run 07: full build with clang-cl instead of MSVC cl.exe, as a second compiler.

.DESCRIPTION
  Needs the Visual Studio component "C++ Clang Compiler for Windows". C++17, /W4 (= -Wall
  -Wextra in clang-cl) with the dependency headers external like in the MSVC runs. clang-cl
  uses the MSVC ABI, so the MSVC-built vcpkg libraries (x64-windows) are used unchanged.
  clang-cl doesn't know /external:anglebrackets and says so once per file
  ("argument unused during compilation"); that is harmless.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\07-clang-cl.ps1

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER BuildDir
  The build directory, must not exist yet. Default: build-clang-cl in the repository.

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
if (-not $BuildDir) { $BuildDir = "$Root\build-clang-cl" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$clang = "$LlvmBin\clang-cl.exe"
if (-not (Test-Path $clang)) { throw "clang-cl not found in $LlvmBin; install the VS component 'C++ Clang Compiler for Windows'." }
$log = "$OutDir\07-clang-cl.log"
$env:CFLAGS = '/W4 /external:W0 /external:anglebrackets'
$env:CXXFLAGS = $env:CFLAGS

New-Log $log '07: clang-cl instead of MSVC cl.exe, full rebuild' (@(
    "# Build directory: $BuildDir"
    "# Compiler: $((& $clang --version | Select-Object -First 1))"
) + (Get-ToolVersions) + @("# CFLAGS/CXXFLAGS env: $env:CXXFLAGS"))
$cc = $clang -replace '\\', '/'
$rc = Invoke-Build $log $BuildDir ("$(Get-ConfigureArgs $BuildDir $VcpkgRoot) -DENABLE_CODE_ANALYSIS=OFF " +
    "-DCMAKE_C_COMPILER=`"$cc`" -DCMAKE_CXX_COMPILER=`"$cc`"")
if (Test-Path "$BuildDir\CMakeCache.txt") {
    $tools = (Get-Content "$BuildDir\CMakeCache.txt" |
        Select-String '^CMAKE_(CXX_COMPILER|LINKER|AR|RC_COMPILER|MT):' | ForEach-Object Line) -join '; '
    Add-LogLine $log "# Tools from CMakeCache: $tools"
}
Add-LogLine $log "# End: $(Get-Stamp)"
exit $rc
