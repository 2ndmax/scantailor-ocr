<#
.SYNOPSIS
  Setup 3: the EDG C++ front end for analysis run 04, with the SoftFloat library it needs.

.DESCRIPTION
  Shows the steps in a menu; each can be run on its own, 0 runs all of them in order. The folders
  of the two checkouts, EDG and SoftFloat, are asked for one after the other when a step first
  needs them:
    1. Requirements: Visual Studio with the C++ tools, the .NET Framework 4.8.1 SDK (EDG links
       mscoree.lib for its C++/CLI support), Python 3.6 or later, Git.
    2. Sources: clones EDG (github.com/edgcpp/compiler) and Berkeley SoftFloat 3. EDG either at
       the version this setup was tested with, or the newest one; SoftFloat always at the commit
       EDG pins.
    3. Build: SoftFloat as softfloat.lib with the CMake file in support\edg-softfloat\ (SoftFloat
       only ships makefiles for GCC and MinGW), then EDG with its preset windows-msvc-release (about
       ten minutes the first time). EDG finds SoftFloat through EDG_SOFTFLOAT_INCLUDE_PATH and
       EDG_SOFTFLOAT_LIB_PATH, which are set for the build only.
    4. EDG_ROOT: sets the variable for your user account; scripts\analysis\04-edg.ps1 reads it.
    5. Check: runs cpfe.exe on a small C++17 file in strict standard mode.
  The output also goes to logs\03-setup-edg.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\03-setup-edg.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER EdgDir
  The EDG checkout. Default: asked, suggesting EDG_ROOT, else D:\edgcpp if drive D: exists, else
  C:\edgcpp.

.PARAMETER SoftFloatDir
  The SoftFloat checkout. Default: asked, suggesting the folder softfloat next to EDG.

.PARAMETER Latest
  With -Steps: the newest EDG instead of the tested version.

.PARAMETER Python
  python.exe for the EDG build. Default: the newest one vcpkg downloaded to
  %VCPKG_ROOT%\downloads\tools\python, else python on PATH.

.PARAMETER Yes
  With -Steps: answer every question with yes.
#>
param(
    [int[]]$Steps = @(),
    [string]$EdgDir = '',
    [string]$SoftFloatDir = '',
    [switch]$Latest,
    [string]$Python = '',
    [switch]$Yes
)
. "$PSScriptRoot\..\support\common.ps1"
. "$PSScriptRoot\support\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\03-setup-edg.log"

# The versions this setup was made and tested with (2026-10-27).
$EdgUrl = 'https://github.com/edgcpp/compiler.git'
$EdgCommit = '35b7c4776c1693c51df00adef14033e056da4c31'
$EdgBranch = 'main'
$SoftFloatUrl = 'https://github.com/ucb-bar/berkeley-softfloat-3.git'
# The commit EDG pins in dev_tools\docker\softfloat\SOFTFLOAT_COMMIT.
$SoftFloatCommit = 'a0c6494cdc11865811dec815d5c0049fba9d82a8'

Write-Intro 'ScanTailor OCR - set up EDG for analysis run 04' @(
    'This script gets and builds the EDG C++ front end, which analysis run 04 uses to check the'
    'code, and the SoftFloat library EDG needs. It shows the steps in a menu; nothing is'
    'installed without asking.'
    ''
    'Steps 2 to 5 ask for the folders of EDG (about 4 GB with its build) and of SoftFloat, one'
    'after the other. Existing checkouts there are used as they are.'
)
# Until the folders are asked for, the suggestions stand in for them (the [done] marks of the menu).
$EdgAsked = [bool]$EdgDir
$SoftFloatAsked = [bool]$SoftFloatDir
$EdgDir = if ($EdgDir) { [IO.Path]::GetFullPath($EdgDir).TrimEnd('\') }
          elseif ($env:EDG_ROOT) { [IO.Path]::GetFullPath($env:EDG_ROOT).TrimEnd('\') }
          else { Join-Path (Get-DefaultBase) 'edgcpp' }
$SoftFloatDir = if ($SoftFloatDir) { [IO.Path]::GetFullPath($SoftFloatDir).TrimEnd('\') }
                else { Join-Path (Split-Path $EdgDir) 'softfloat' }

# The folders that follow from $EdgDir and $SoftFloatDir.
function Set-EdgPaths {
    $script:EdgBuildDir = "$script:EdgDir\build\msvc-release"
    $script:SoftFloatBuildDir = "$script:SoftFloatDir\build-msvc"
    $script:Cpfe = "$script:EdgBuildDir\bin\cpfe.exe"
}
Set-EdgPaths
$header = @()
if ($EdgAsked) { $header += "# EDG: $EdgDir" }
if ($SoftFloatAsked) { $header += "# SoftFloat: $SoftFloatDir" }
New-Log $log 'setup 3: EDG C++ front end and SoftFloat' $header -KeepPrevious

# Asks for a checkout folder; a drive root is refused, as git clones only into an empty folder.
function Read-CheckoutFolder([string]$Question, [string]$Default) {
    while ($true) {
        $folder = Read-Folder $Question $Default
        # Read-Folder keeps the backslash of a drive root: D:\ is 3 characters.
        if ($folder.Length -gt 3) { return $folder }
        if ($Unattended) { throw "The checkout needs a folder of its own, not the drive itself: $folder" }
        Write-Host 'The checkout needs a folder of its own, not the drive itself.' -ForegroundColor Yellow
    }
}

# Asks for the folders of EDG and then SoftFloat the first time a step needs them; -EdgDir and
# -SoftFloatDir answer them beforehand. -EdgOnly: for the steps that only use EDG.
function Confirm-EdgDir([switch]$EdgOnly) {
    if (-not $script:EdgAsked) {
        if ($env:EDG_ROOT -and -not $Unattended) { Write-Host 'The suggestion is the folder in EDG_ROOT, which is set already.' }
        $script:EdgDir = Read-CheckoutFolder 'Folder for EDG' $script:EdgDir
        $script:EdgAsked = $true
        Add-LogLine $log "# $(Get-Stamp) EDG: $script:EdgDir"
        # The suggestion follows the folder chosen for EDG.
        if (-not $script:SoftFloatAsked) { $script:SoftFloatDir = Join-Path (Split-Path $script:EdgDir) 'softfloat' }
    }
    if (-not $script:SoftFloatAsked -and -not $EdgOnly) {
        while ($true) {
            $script:SoftFloatDir = Read-CheckoutFolder 'Folder for SoftFloat' $script:SoftFloatDir
            if ($script:SoftFloatDir -ne $script:EdgDir) { break }
            if ($Unattended) { throw "SoftFloat needs a folder other than EDG's: $script:SoftFloatDir" }
            Write-Host "SoftFloat needs a folder other than EDG's." -ForegroundColor Yellow
            $script:SoftFloatDir = Join-Path (Split-Path $script:EdgDir) 'softfloat'
        }
        $script:SoftFloatAsked = $true
        Add-LogLine $log "# $(Get-Stamp) SoftFloat: $script:SoftFloatDir"
    }
    Set-EdgPaths
}

# python.exe (3.x) or $null. The Microsoft Store placeholder "python.exe" doesn't run, so each
# candidate is tried.
function Find-Python {
    $candidates = @()
    if ($Python) { $candidates += $Python }
    if ($env:VCPKG_ROOT) {
        $candidates += Get-ChildItem "$env:VCPKG_ROOT\downloads\tools\python\*\python.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object FullName
    }
    $candidates += 'python'
    foreach ($candidate in $candidates) {
        $version = cmd /c "`"$candidate`" --version 2>&1"
        if ($LASTEXITCODE -eq 0 -and "$version" -match '^Python 3') { return (Get-Command $candidate).Source }
    }
    return $null
}

function Find-Mscoree {
    return (Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\NETFXSDK\*\Lib\um\x64\mscoree.lib" -ErrorAction SilentlyContinue |
        Select-Object -First 1)
}

# "<commit> (<date>)" of a git checkout, plus its uncommitted changes.
function Get-GitState([string]$Dir) {
    $state = cmd /c "git -C `"$Dir`" log -1 --format=%H 2>nul"
    $changes = @(cmd /c "git -C `"$Dir`" status --porcelain 2>nul")
    if ($changes.Count) { $state += "; working tree: $($changes -join '; ')" }
    return $state
}

# --- Step 1: requirements ---------------------------------------------------------------------

$stepRequirements = {
    $ok = $true
    if ($Vs) { Write-Host "Visual Studio: $Vs" }
    else { Write-Host 'Visual Studio with the C++ tools is missing: run 01-setup-windows.ps1.' -ForegroundColor Red; $ok = $false }
    $mscoree = Find-Mscoree
    if ($mscoree) { Write-Host ".NET Framework SDK: $($mscoree.FullName)" }
    else {
        Write-Host 'The .NET Framework 4.8.1 SDK is missing: 02-setup-analysis.ps1 adds it, or the' -ForegroundColor Red
        Write-Host 'Visual Studio Installer ("Modify" -> "Individual components").' -ForegroundColor Red
        $ok = $false
    }
    $py = Find-Python
    if ($py) { Write-Host "Python: $py" }
    else {
        Write-Host 'No Python 3 found. vcpkg brings one along (01-setup-windows.ps1, step 3); else install' -ForegroundColor Red
        Write-Host 'Python 3 or pass -Python <python.exe>.' -ForegroundColor Red
        $ok = $false
    }
    if (Get-Command git -ErrorAction SilentlyContinue) { Write-Host "Git: $((Get-Command git).Source)" }
    else {
        Write-Host 'Git is missing: 01-setup-windows.ps1 installs it (step 2), or' -ForegroundColor Red
        Write-Host 'https://git-scm.com/download/win' -ForegroundColor Red
        $ok = $false
    }
    return $ok
}

# --- Step 2: sources --------------------------------------------------------------------------

$stepSources = {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Write-Host 'Git is missing; see step 1.' -ForegroundColor Red; return $false }
    Confirm-EdgDir
    $newest = if ($Unattended) { [bool]$Latest } else {
        (Read-Choice 'Which version of EDG?' @(
            "the version this setup was tested with ($($EdgCommit.Substring(0, 8)))"
            "the newest one (branch $EdgBranch; not tested with this setup)")) -eq 1
    }
    if (-not (Test-Path $EdgDir)) {
        if ((Invoke-Shown "git clone --filter=blob:none $EdgUrl `"$EdgDir`"" $log) -ne 0) { return $false }
        if (-not $newest -and (Invoke-Shown "git -C `"$EdgDir`" checkout -q $EdgCommit" $log) -ne 0) { return $false }
    } elseif ($newest) {
        $update = "git -C `"$EdgDir`" checkout -q $EdgBranch && git -C `"$EdgDir`" pull -q --ff-only"
        if ((Invoke-Shown $update $log) -ne 0) { return $false }
    } else {
        $head = cmd /c "git -C `"$EdgDir`" rev-parse HEAD 2>nul"
        if ($head -ne $EdgCommit -and (Read-YesNo "$EdgDir is at $head. Switch it to the tested version?" $true)) {
            if ((Invoke-Shown "git -C `"$EdgDir`" checkout -q $EdgCommit" $log) -ne 0) { return $false }
        }
    }
    if (-not (Test-Path $SoftFloatDir)) {
        if ((Invoke-Shown "git clone $SoftFloatUrl `"$SoftFloatDir`"" $log) -ne 0) { return $false }
        if ((Invoke-Shown "git -C `"$SoftFloatDir`" checkout -q $SoftFloatCommit" $log) -ne 0) { return $false }
    }
    Add-LogLine $log "# EDG: $(Get-GitState $EdgDir)"
    Add-LogLine $log "# SoftFloat: $(Get-GitState $SoftFloatDir)"
    Write-Host "EDG:       $EdgDir at $(Get-GitState $EdgDir)"
    Write-Host "SoftFloat: $SoftFloatDir at $(Get-GitState $SoftFloatDir)"
    return $true
}

# --- Step 3: build ----------------------------------------------------------------------------

$stepBuild = {
    if (-not $Vs) { Write-Host 'Visual Studio is missing; see step 1.' -ForegroundColor Red; return $false }
    if (-not (Find-Mscoree)) { Write-Host 'The .NET Framework 4.8.1 SDK is missing; see step 1.' -ForegroundColor Red; return $false }
    Confirm-EdgDir
    if (-not ((Test-Path $EdgDir) -and (Test-Path $SoftFloatDir))) {
        Write-Host 'The sources are missing; run step 2 first.' -ForegroundColor Red
        return $false
    }
    $py = Find-Python
    if (-not $py) { Write-Host 'No Python 3 found; see step 1.' -ForegroundColor Red; return $false }

    # --fresh: the build directory may still name an older place of this script folder.
    $sfConfigure = "cmake --fresh -S `"$PSScriptRoot\support\edg-softfloat`" -B `"$SoftFloatBuildDir`" -G Ninja " +
                   "-DCMAKE_BUILD_TYPE=Release -DSOFTFLOAT_DIR=`"$($SoftFloatDir -replace '\\', '/')`""
    if ((Invoke-Shown $sfConfigure $log -InVsEnv) -ne 0) { return $false }
    if ((Invoke-Shown "cmake --build `"$SoftFloatBuildDir`"" $log -InVsEnv) -ne 0) { return $false }

    # Read by EDG's cmake\softfloat.cmake when it configures.
    $env:EDG_SOFTFLOAT_INCLUDE_PATH = "$SoftFloatDir\source\include"
    $env:EDG_SOFTFLOAT_LIB_PATH = "$SoftFloatBuildDir\softfloat.lib"
    $env:PATH = "$(Split-Path $py);$env:PATH"
    Add-LogLine $log "# Python: $py"
    Add-LogLine $log "# EDG_SOFTFLOAT_INCLUDE_PATH=$env:EDG_SOFTFLOAT_INCLUDE_PATH"
    Add-LogLine $log "# EDG_SOFTFLOAT_LIB_PATH=$env:EDG_SOFTFLOAT_LIB_PATH"
    if ((Invoke-Shown "cd /d `"$EdgDir`" && cmake --preset windows-msvc-release" $log -InVsEnv) -ne 0) { return $false }
    if ((Invoke-Shown "ninja -C `"$EdgBuildDir`"" $log -InVsEnv) -ne 0) { return $false }
    Write-Host "Built: $Cpfe"
    return $true
}

# --- Step 4: EDG_ROOT -------------------------------------------------------------------------

$stepEdgRoot = {
    Confirm-EdgDir -EdgOnly
    if (-not (Test-Path $EdgDir)) { Write-Host 'EDG is missing; run step 2 first.' -ForegroundColor Red; return $false }
    [Environment]::SetEnvironmentVariable('EDG_ROOT', $EdgDir, 'User')
    $env:EDG_ROOT = $EdgDir
    Add-LogLine $log "# $(Get-Stamp) EDG_ROOT=$EdgDir (user environment)"
    Write-Host "EDG_ROOT = $EdgDir for your user account; windows opened from now on see it."
    return $true
}

# --- Step 5: check ----------------------------------------------------------------------------

$stepCheck = {
    Confirm-EdgDir -EdgOnly
    if (-not (Test-Path $Cpfe)) { Write-Host 'EDG is not built; run step 3 first.' -ForegroundColor Red; return $false }
    # C++17 code that must pass in strict standard mode.
    $checkDir = "$LogDir\03-setup-edg-check"
    New-Item -ItemType Directory -Force $checkDir | Out-Null
    $check = "$checkDir\check.cpp"
    [IO.File]::WriteAllText($check, (@(
        'template <typename T> constexpr T twice(T v) { return v + v; }'
        'static_assert(twice(21) == 42);'
        'int main() { if constexpr (sizeof(int) >= 2) return 0; else return 1; }'
    ) -join "`r`n") + "`r`n", $Utf8)
    if ((Invoke-Shown "`"$Cpfe`" --no_code_gen --c++17 --strict `"$check`"" $log) -ne 0) { return $false }
    Write-Host "EDG works: $Cpfe"
    return $true
}

$menu = @(
    @{ Title = 'Check the requirements (Visual Studio, .NET Framework 4.8.1 SDK, Python, Git)'
       Done = { $Vs -and (Find-Mscoree) -and (Get-Command git -ErrorAction SilentlyContinue) }; Run = $stepRequirements }
    @{ Title = 'Get EDG and SoftFloat'
       Done = { (Test-Path $EdgDir) -and (Test-Path $SoftFloatDir) }; Run = $stepSources }
    @{ Title = 'Build SoftFloat and EDG (about ten minutes the first time)'
       Done = { Test-Path $Cpfe }; Run = $stepBuild }
    @{ Title = 'Set EDG_ROOT for your user account'
       Done = { [Environment]::GetEnvironmentVariable('EDG_ROOT', 'User') -eq $EdgDir }; Run = $stepEdgRoot }
    @{ Title = 'Check the front end with a small test file'
       Run = $stepCheck }
)
$ok = Invoke-StepMenu 'ScanTailor OCR - set up EDG for analysis run 04' {
    "Log:       $log"
    if ($EdgAsked) { "EDG:       $EdgDir" }
    if ($SoftFloatAsked) { "SoftFloat: $SoftFloatDir" }
} $menu $Steps
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
