<#
.SYNOPSIS
  Setup 2: the tools the analysis runs in scripts\analysis need beyond the build.

.DESCRIPTION
  Shows for every analysis run whether its tools are there, then offers the missing ones in a
  menu; each step can be run on its own, 0 runs all of them in order:
    1. Visual Studio components, added with the Visual Studio Installer: "C++ Clang Compiler for
       Windows" (clang-cl and the sanitizer runtime, runs 07 and 08) and the .NET Framework
       4.8.1 SDK (run 04).
    2. cppcheck (run 03), installed with winget into its standard folder C:\Program Files\Cppcheck.
    3. EDG (run 04): starts 3-setup-edg.ps1.
  clang-tidy (run 02) comes with Visual Studio itself; the build itself is set up by
  1-setup-windows.ps1. The output also goes to logs\2-setup-analysis.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\2-setup-analysis.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER Yes
  With -Steps: answer every question with yes (install).
#>
param(
    [int[]]$Steps = @(),
    [switch]$Yes
)
. "$PSScriptRoot\..\common.ps1"
. "$PSScriptRoot\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\2-setup-analysis.log"
New-Log $log 'setup 2: analysis tools'

$ClangComponent = 'Microsoft.VisualStudio.Component.VC.Llvm.Clang'
$NetSdkComponent = 'Microsoft.Net.Component.4.8.1.SDK'
$CppcheckFolder = "$env:ProgramFiles\Cppcheck"

function Test-ClangCl { return [bool]($Vs -and (Test-Path "$LlvmBin\clang-cl.exe")) }
function Test-Sanitizers {
    return [bool]((Test-ClangCl) -and
        (Test-Path "$LlvmBin\..\lib\clang\*\lib\windows\clang_rt.asan_dynamic-x86_64.lib"))
}
function Test-NetSdk {
    return [bool](Test-Path "${env:ProgramFiles(x86)}\Windows Kits\NETFXSDK\*\Lib\um\x64\mscoree.lib")
}
function Get-Cpfe {
    if (-not $env:EDG_ROOT) { $env:EDG_ROOT = [Environment]::GetEnvironmentVariable('EDG_ROOT', 'User') }
    if (-not $env:EDG_ROOT) { return $null }
    $cpfe = "$env:EDG_ROOT\build\msvc-release\bin\cpfe.exe"
    if (Test-Path $cpfe) { return $cpfe }
    return $null
}

# One line per run: ready, or what is missing.
$status = {
    $vsNote = 'Visual Studio with the C++ tools (1-setup-windows.ps1)'
    $msvc = if ($Vs) { 'ready' } else { "missing: $vsNote" }
    $tidy = if (-not $Vs) { "missing: $vsNote" } elseif (Test-Path "$LlvmBin\clang-tidy.exe") { 'ready' } `
            else { 'missing: clang-tidy in Visual Studio (update Visual Studio)' }
    $cppcheck = if (Find-Cppcheck) { "ready ($(Find-Cppcheck))" } else { 'missing: cppcheck (step 2)' }
    $edgMissing = @()
    if (-not (Test-NetSdk)) { $edgMissing += '.NET Framework 4.8.1 SDK (step 1)' }
    if (-not (Get-Cpfe)) { $edgMissing += 'EDG (step 3)' }
    $edg = if ($edgMissing) { "missing: $($edgMissing -join ', ')" } else { "ready ($env:EDG_ROOT)" }
    $clang = if (Test-ClangCl) { 'ready' } else { 'missing: Visual Studio component "C++ Clang Compiler for Windows" (step 1)' }
    $sanitizers = if (Test-Sanitizers) { 'ready' } else { $clang }
    $vcpkg = if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) { "ready ($env:VCPKG_ROOT)" } `
             else { 'missing: VCPKG_ROOT (1-setup-windows.ps1, step 3)' }
    @(
        'Analysis runs and what they need:'
        "  01, 05, 06  MSVC              $msvc"
        "  02          clang-tidy        $tidy"
        "  03          cppcheck          $cppcheck"
        "  04          EDG               $edg"
        "  07          clang-cl          $clang"
        "  08          sanitizers        $sanitizers"
        "  all builds  vcpkg             $vcpkg"
        "Log: $log"
    )
}

$stepComponents = {
    $instance = Get-AnyVsInstance
    if (-not $instance) { Write-Host 'Visual Studio is missing: run 1-setup-windows.ps1 first.' -ForegroundColor Red; return $false }
    $missing = @()
    if (-not (Test-Sanitizers)) { $missing += $ClangComponent }
    if (-not (Test-NetSdk)) { $missing += $NetSdkComponent }
    if (-not $missing.Count) { Write-Host 'The components are there.'; return $true }
    if (-not (Add-VsComponents $instance $missing $log)) { return $false }
    Find-VisualStudio
    if (-not ((Test-Sanitizers) -and (Test-NetSdk))) {
        Write-Host 'The components are still missing; see the Visual Studio Installer.' -ForegroundColor Red
        return $false
    }
    return $true
}

$stepCppcheck = {
    $found = Find-Cppcheck
    if ($found) { Write-Host "cppcheck: $found"; return $true }
    Write-Host "Installs cppcheck with winget into its standard folder $CppcheckFolder."
    Write-Host 'Windows asks for administrator rights.'
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host 'winget (App Installer) is not available. Install cppcheck from' -ForegroundColor Yellow
        Write-Host 'https://cppcheck.sourceforge.io/ into its standard folder.' -ForegroundColor Yellow
        return $false
    }
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    Add-LogLine $log "# $(Get-Stamp) winget install --id Cppcheck.Cppcheck --exact --source winget"
    # Run directly in the console, so the questions of winget can be answered.
    & winget install --id Cppcheck.Cppcheck --exact --source winget
    Add-LogLine $log "# Exit code: $LASTEXITCODE"
    $found = Find-Cppcheck
    if (-not $found) { Write-Host "cppcheck is not in $CppcheckFolder." -ForegroundColor Red; return $false }
    Write-Host "cppcheck: $found"
    return $true
}

$stepEdg = {
    $edgSetup = "$PSScriptRoot\3-setup-edg.ps1"
    Add-LogLine $log "# $(Get-Stamp) $edgSetup"
    if ($Unattended) { & $edgSetup -Steps 0 -Yes:$AssumeYes } else { & $edgSetup }
    $rc = $LASTEXITCODE
    Add-LogLine $log "# Exit code: $rc"
    return ($rc -eq 0 -and (Get-Cpfe))
}

$menu = @(
    @{ Title = 'Visual Studio components: Clang compiler (runs 07, 08), .NET Framework 4.8.1 SDK (run 04)'
       Done = { (Test-Sanitizers) -and (Test-NetSdk) }; Run = $stepComponents }
    @{ Title = "cppcheck (run 03), into $CppcheckFolder"
       Done = { [bool](Find-Cppcheck) }; Run = $stepCppcheck }
    @{ Title = 'EDG (run 04): start 3-setup-edg.ps1'
       Done = { [bool](Get-Cpfe) }; Run = $stepEdg }
)
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the analysis tools' $status $menu $Steps
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
