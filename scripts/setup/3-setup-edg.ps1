<#
.SYNOPSIS
  Setup for analysis run 04: gets and builds the EDG C++ front end and the SoftFloat library it needs.

.DESCRIPTION
  Needed once before scripts\analysis\04-edg.ps1, and again after updating EDG.
    1. Clones EDG (github.com/edgcpp/compiler) and Berkeley SoftFloat 3 at the commits below,
       each only if its folder doesn't exist yet. Existing checkouts are used as they are;
       their commit is written to the log.
    2. Builds SoftFloat as softfloat.lib with the CMake file in edg-softfloat\, into
       <SoftFloatDir>\build-msvc. SoftFloat itself only ships makefiles for GCC and MinGW.
    3. Configures EDG with its preset windows-msvc-release and builds it with Ninja, into
       <EdgDir>\build\msvc-release. EDG finds SoftFloat through EDG_SOFTFLOAT_INCLUDE_PATH and
       EDG_SOFTFLOAT_LIB_PATH, which are set here for the build only.
    4. Runs cpfe.exe on a small C++17 file in strict standard mode, as a check.
  Later runs only rebuild what changed.

    powershell -ExecutionPolicy Bypass -File scripts\setup\3-setup-edg.ps1 -EdgDir D:\edgcpp

.PARAMETER EdgDir
  The folder for the EDG checkout. Default: the EDG_ROOT environment variable.

.PARAMETER SoftFloatDir
  The folder for the SoftFloat checkout. Default: softfloat next to the EDG folder.

.PARAMETER Python
  python.exe for the EDG build (3.6 or later). Default: the newest one vcpkg downloaded to
  %VCPKG_ROOT%\downloads\tools\python, else python on PATH.

.PARAMETER OutDir
  Where the log goes. Default: logs in this folder.
#>
param(
    [string]$EdgDir = '',
    [string]$SoftFloatDir = '',
    [string]$Python = '',
    [string]$OutDir = ''
)
. "$PSScriptRoot\..\analysis\common.ps1"
if (-not $OutDir) { $OutDir = "$PSScriptRoot\logs" }
if (-not $EdgDir) { $EdgDir = $env:EDG_ROOT }
if (-not $EdgDir) { throw 'No folder for EDG: pass -EdgDir or set EDG_ROOT.' }
$EdgDir = [IO.Path]::GetFullPath($EdgDir)
if (-not $SoftFloatDir) { $SoftFloatDir = Join-Path (Split-Path $EdgDir) 'softfloat' }
$SoftFloatDir = [IO.Path]::GetFullPath($SoftFloatDir)
$log = "$OutDir\3-setup-edg.log"

# The commits this setup was made and tested with (2026-10-27).
$EdgUrl = 'https://github.com/edgcpp/compiler.git'
$EdgCommit = '35b7c4776c1693c51df00adef14033e056da4c31'
$SoftFloatUrl = 'https://github.com/ucb-bar/berkeley-softfloat-3.git'
# The commit EDG pins in dev_tools\docker\softfloat\SOFTFLOAT_COMMIT.
$SoftFloatCommit = 'a0c6494cdc11865811dec815d5c0049fba9d82a8'

$EdgBuildDir = "$EdgDir\build\msvc-release"
$SoftFloatBuildDir = "$SoftFloatDir\build-msvc"

# Python: the Microsoft Store placeholder "python.exe" doesn't run, so check that it does.
if (-not $Python -and $env:VCPKG_ROOT) {
    $Python = Get-ChildItem "$env:VCPKG_ROOT\downloads\tools\python\*\python.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $Python) { $Python = 'python' }
$pyVersion = cmd /c "`"$Python`" --version 2>&1"
if ($LASTEXITCODE -ne 0 -or "$pyVersion" -notmatch '^Python 3') {
    throw "No working Python found ('$Python'): pass -Python <python.exe>."
}
$Python = (Get-Command $Python).Source

# EDG links mscoree.lib for its C++/CLI support; it comes with the .NET Framework SDK.
$mscoree = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\NETFXSDK\*\Lib\um\x64\mscoree.lib" -ErrorAction SilentlyContinue
if (-not $mscoree) {
    throw 'mscoree.lib not found: install the ".NET Framework 4.8.1 SDK" with the Visual Studio Installer.'
}

# "<commit> (<date>)" of a git checkout, plus its uncommitted changes.
function Get-GitState([string]$Dir) {
    $state = git -C $Dir log -1 --format='%H (%cs)'
    $changes = @(git -C $Dir status --porcelain)
    if ($changes.Count) { $state += "; working tree: $($changes -join '; ')" }
    return $state
}

function Get-Checkout([string]$Dir, [string]$Url, [string]$Commit, [string]$CloneArgs) {
    if (Test-Path $Dir) {
        Add-LogLine $log "# $Dir exists, used as it is"
        return
    }
    Add-LogLine $log "# Clone: git clone $CloneArgs $Url $Dir; checkout $Commit"
    cmd /c "git clone -q $CloneArgs $Url `"$Dir`" >> `"$log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "git clone of $Url failed, see $log" }
    cmd /c "git -C `"$Dir`" checkout -q $Commit >> `"$log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "git checkout $Commit in $Dir failed, see $log" }
}

New-Log $log 'setup 3: EDG C++ front end and SoftFloat' (@(
    "# Compiler: $(Get-ClVersion)"
) + (Get-ToolVersions) + @(
    "# $pyVersion ($Python)"
    "# mscoree.lib: $($mscoree[0].FullName)"
))
Get-Checkout $EdgDir $EdgUrl $EdgCommit '--filter=blob:none'
Get-Checkout $SoftFloatDir $SoftFloatUrl $SoftFloatCommit ''
Add-LogLine $log "# EDG: $(Get-GitState $EdgDir)"
Add-LogLine $log "# SoftFloat: $(Get-GitState $SoftFloatDir)"

# --fresh: the build directory may still name an older place of this script folder.
$sfConfigureCmd = "cmake --fresh -S `"$PSScriptRoot\edg-softfloat`" -B `"$SoftFloatBuildDir`" -G Ninja " +
                  "-DCMAKE_BUILD_TYPE=Release -DSOFTFLOAT_DIR=`"$($SoftFloatDir -replace '\\', '/')`""
$sfBuildCmd = "cmake --build `"$SoftFloatBuildDir`""
Add-LogLine $log "`r`n# SoftFloat configure: $sfConfigureCmd`r`n"
$rc = Invoke-VsLogged $sfConfigureCmd $log
if ($rc -eq 0) {
    Add-LogLine $log "`r`n# SoftFloat build: $sfBuildCmd`r`n"
    $rc = Invoke-VsLogged $sfBuildCmd $log
}
Add-LogLine $log "`r`n# SoftFloat exit code: $rc"

if ($rc -eq 0) {
    # Read by EDG's cmake\softfloat.cmake when it configures.
    $env:EDG_SOFTFLOAT_INCLUDE_PATH = "$SoftFloatDir\source\include"
    $env:EDG_SOFTFLOAT_LIB_PATH = "$SoftFloatBuildDir\softfloat.lib"
    $env:PATH = "$(Split-Path $Python);$env:PATH"
    $edgConfigureCmd = "cd /d `"$EdgDir`" && cmake --preset windows-msvc-release"
    $edgBuildCmd = "ninja -C `"$EdgBuildDir`""
    Add-LogLine $log "`r`n# EDG_SOFTFLOAT_INCLUDE_PATH=$env:EDG_SOFTFLOAT_INCLUDE_PATH"
    Add-LogLine $log "# EDG_SOFTFLOAT_LIB_PATH=$env:EDG_SOFTFLOAT_LIB_PATH"
    Add-LogLine $log "# EDG configure: $edgConfigureCmd`r`n"
    $rc = Invoke-VsLogged $edgConfigureCmd $log
    if ($rc -eq 0) {
        Add-LogLine $log "`r`n# EDG build: $edgBuildCmd`r`n"
        $rc = Invoke-VsLogged $edgBuildCmd $log
    }
    Add-LogLine $log "`r`n# EDG exit code: $rc"
}

if ($rc -eq 0) {
    # A check that the front end runs: C++17 code that must pass in strict standard mode.
    $checkDir = "$OutDir\3-setup-edg-check"
    New-Item -ItemType Directory -Force $checkDir | Out-Null
    $check = "$checkDir\check.cpp"
    [IO.File]::WriteAllText($check, (@(
        'template <typename T> constexpr T twice(T v) { return v + v; }'
        'static_assert(twice(21) == 42);'
        'int main() { if constexpr (sizeof(int) >= 2) return 0; else return 1; }'
    ) -join "`r`n") + "`r`n", $Utf8)
    $cpfeCmd = "`"$EdgBuildDir\bin\cpfe.exe`" --no_code_gen --c++17 --strict `"$check`""
    Add-LogLine $log "`r`n# Check: $cpfeCmd`r`n"
    $rc = Invoke-VsLogged $cpfeCmd $log
    Add-LogLine $log "# Check exit code: $rc (0: the front end works)"
}
Add-LogLine $log "# End: $(Get-Stamp)"
if ($rc -eq 0) { Write-Host "EDG is ready: $EdgBuildDir\bin\cpfe.exe" }
else { Write-Host "Setup failed (exit code $rc), see $log" }
exit $rc
