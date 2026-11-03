<#
.SYNOPSIS
  Setup 2: the tools the analysis runs in scripts\analysis need beyond the build.

.DESCRIPTION
  Offers the tools in a menu; each step can be run on its own, 0 runs all of them in order, 9
  shows for every analysis run whether its tools are there:
    1. Visual Studio components, added with the Visual Studio Installer: "C++ Clang Compiler for
       Windows" (clang-cl and the sanitizer runtime, runs 07 and 09), "C++ AddressSanitizer"
       (stl_asan.lib, which the MSVC standard library asks for under ASan, run 09; libFuzzer and
       ASan of MSVC, run 10) and the .NET
       Framework 4.8.1 SDK (run 04).
    2. cppcheck (run 03), installed with winget into its standard folder C:\Program Files\Cppcheck.
    3. EDG (run 04): installs nothing itself, but starts the separate script 03-setup-edg.ps1
       with its own menu.
    4. WSL with Ubuntu and GCC (run 08): installs nothing itself, but starts the separate script
       04-setup-wsl-gcc.ps1 with its own menu.
    5. The debugger cdb (run 12), from the "Debugging Tools for Windows" of the Windows SDK. The
       Visual Studio Installer installs the SDK without them and cannot add them; the SDK's own
       setup, kept in the Package Cache, can: this step opens it to change the installation.
       Application Verifier itself comes with Windows 11.
  clang-tidy (run 02) comes with Visual Studio itself; the build itself is set up by
  01-setup-windows.ps1. The output also goes to logs\02-setup-analysis.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\02-setup-analysis.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER Yes
  With -Steps: answer every question with yes (install).
#>
param(
    [int[]]$Steps = @(),
    [switch]$Yes
)
. "$PSScriptRoot\..\support\common.ps1"
. "$PSScriptRoot\support\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\02-setup-analysis.log"
New-Log $log 'setup 2: analysis tools' -KeepPrevious

$ClangComponent = 'Microsoft.VisualStudio.Component.VC.Llvm.Clang'
$AsanComponent = 'Microsoft.VisualStudio.Component.VC.ASAN'
$NetSdkComponent = 'Microsoft.Net.Component.4.8.1.SDK'
$CppcheckFolder = "$env:ProgramFiles\Cppcheck"

function Test-ClangCl { return [bool]($Vs -and (Test-Path "$LlvmBin\clang-cl.exe")) }
function Test-ClangRuntime {
    return [bool]((Test-ClangCl) -and
        (Test-Path "$LlvmBin\..\lib\clang\*\lib\windows\clang_rt.asan_dynamic-x86_64.lib"))
}
# The MSVC standard library links stl_asan.lib when built with ASan; it comes with the
# component "C++ AddressSanitizer", not with the Clang compiler.
function Test-StlAsan { return [bool]($Vs -and (Test-Path "$Vs\VC\Tools\MSVC\*\lib\x64\stl_asan.lib")) }
# libFuzzer of MSVC (run 10), from the same component.
function Test-MsvcFuzzer {
    return [bool]($Vs -and (Test-Path "$Vs\VC\Tools\MSVC\*\lib\x64\clang_rt.fuzzer_MD-x86_64.lib"))
}
function Test-Sanitizers { return ((Test-ClangRuntime) -and (Test-StlAsan)) }
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
    $vsNote = 'Visual Studio with the C++ tools (01-setup-windows.ps1)'
    $msvc = if ($Vs) { 'ready' } else { "missing: $vsNote" }
    $tidy = if (-not $Vs) { "missing: $vsNote" } elseif (Test-Path "$LlvmBin\clang-tidy.exe") { 'ready' } `
            else { 'missing: clang-tidy in Visual Studio (update Visual Studio)' }
    $cppcheck = if (Find-Cppcheck) { 'ready' } else { 'missing: cppcheck (step 2)' }
    $edgMissing = @()
    if (-not (Test-NetSdk)) { $edgMissing += '.NET Framework 4.8.1 SDK (step 1)' }
    if (-not (Get-Cpfe)) { $edgMissing += 'EDG (step 3)' }
    $edg = if ($edgMissing) { "missing: $($edgMissing -join ', ')" } else { 'ready' }
    $clang = if (Test-ClangCl) { 'ready' } else { 'missing: Visual Studio component "C++ Clang Compiler for Windows" (step 1)' }
    $sanitizers = if (Test-Sanitizers) { 'ready' } elseif (-not (Test-ClangRuntime)) { $clang } `
                  else { 'missing: Visual Studio component "C++ AddressSanitizer" (step 1)' }
    $fuzzing = if (-not $Vs) { "missing: $vsNote" } elseif (Test-MsvcFuzzer) { 'ready' } `
               else { 'missing: Visual Studio component "C++ AddressSanitizer" (step 1)' }
    $gcc = if (Test-UbuntuPackages) { 'ready' } `
           elseif (Test-WslDistro) { "missing: the packages in $WslDistro (step 4)" } `
           elseif (Test-Wsl) { "missing: $WslDistro in WSL (step 4)" } else { 'missing: WSL (step 4)' }
    $verifier = if (-not (Find-AppVerif)) { 'missing: Application Verifier (Windows SDK, feature "Application Verifier For Windows")' } `
                elseif (Find-Cdb) { 'ready' } else { 'missing: cdb (step 5)' }
    $hasVcpkg = $env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)
    $vcpkg = if ($hasVcpkg) { 'ready' } else { 'missing: VCPKG_ROOT (01-setup-windows.ps1, step 4)' }
    @(
        'Analysis runs and what they need:'
        "  01, 05, 06  MSVC              $msvc"
        "  02          clang-tidy        $tidy"
        "  03          cppcheck          $cppcheck"
        "  04          EDG               $edg"
        "  07          clang-cl          $clang"
        "  08          GCC               $gcc"
        "  09          sanitizers        $sanitizers"
        "  10          fuzzing           $fuzzing"
        "  12          app verifier      $verifier"
        "  all builds  vcpkg             $vcpkg"
        ''
        'The tools, their versions and where they are:'
    )
    # Each tool once, also when several runs use it. The version only where it costs no start of
    # the tool: from the file's version resource, or from Visual Studio's own records. A missing
    # tool shows "-"; the table above says which step adds it.
    $tools = @(
        @{ Name = 'Visual Studio'; Path = $Vs
           Version = { & "$vswhereDir\vswhere.exe" -path $Vs -property catalog_productDisplayVersion } }
        @{ Name = 'MSVC toolset'
           Path = $(if ($Vs) { Get-ChildItem "$Vs\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
                              Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty FullName })
           Version = { Split-Path $path -Leaf } }
        @{ Name = 'LLVM (clang-cl, clang-tidy)'; Path = $(if (Test-ClangCl) { $LlvmBin })
           Version = { (Get-Item "$path\clang-cl.exe").VersionInfo.ProductVersion } }
        @{ Name = 'cppcheck'; Path = (Find-Cppcheck); Version = { (Get-Item $path).VersionInfo.ProductVersion } }
        @{ Name = 'EDG'; Path = $(if (Get-Cpfe) { $env:EDG_ROOT }) }
        @{ Name = 'GCC'; Path = $(if (Test-UbuntuPackages) { "WSL, $WslDistro" }) }
        @{ Name = 'cdb'; Path = (Find-Cdb); Version = { (Get-Item $path).VersionInfo.ProductVersion } }
        @{ Name = 'Application Verifier'; Path = (Find-AppVerif); Version = { (Get-Item $path).VersionInfo.ProductVersion } }
        @{ Name = 'vcpkg'; Path = $(if ($hasVcpkg) { $env:VCPKG_ROOT }) }
    )
    foreach ($t in $tools) {
        $path = $t.Path
        $version = if ($path -and $t.Version) { "$(& $t.Version)".Trim() } else { '' }
        if (-not $path) { $path = '-' }
        '  {0,-28} {1,-16} {2}' -f $t.Name, $version, $path
    }
}

$stepComponents = {
    $instance = Get-AnyVsInstance
    if (-not $instance) { Write-Host 'Visual Studio is missing: run 01-setup-windows.ps1 first.' -ForegroundColor Red; return $false }
    $missing = @()
    if (-not (Test-ClangRuntime)) { $missing += $ClangComponent }
    if (-not (Test-StlAsan)) { $missing += $AsanComponent }
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
    $edgSetup = "$PSScriptRoot\03-setup-edg.ps1"
    Write-Host 'Starts the separate script 03-setup-edg.ps1, which has its own menu and installs from there.'
    Add-LogLine $log "# $(Get-Stamp) $edgSetup"
    if ($Unattended) { & $edgSetup -Steps 0 -Yes:$AssumeYes } else { & $edgSetup }
    $rc = $LASTEXITCODE
    Add-LogLine $log "# Exit code: $rc"
    return ($rc -eq 0 -and (Get-Cpfe))
}

$stepWslGcc = {
    $wslSetup = "$PSScriptRoot\04-setup-wsl-gcc.ps1"
    Write-Host 'Starts the separate script 04-setup-wsl-gcc.ps1, which has its own menu and installs from there.'
    Add-LogLine $log "# $(Get-Stamp) $wslSetup"
    if ($Unattended) { & $wslSetup -Steps 0 -Yes:$AssumeYes } else { & $wslSetup }
    $rc = $LASTEXITCODE
    Add-LogLine $log "# Exit code: $rc"
    return ($rc -eq 0 -and (Test-UbuntuPackages))
}

# The setup of the newest installed Windows SDK, as Windows keeps it for changing the installation
# (Settings > Apps), or $null.
function Find-WindowsSdkSetup {
    $keys = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    $sdk = Get-ItemProperty $keys -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like 'Windows Software Development Kit - Windows 10.*' -and
                       $_.BundleCachePath -and (Test-Path $_.BundleCachePath) } |
        Sort-Object { [version]$_.DisplayVersion } | Select-Object -Last 1
    if ($sdk) { return $sdk.BundleCachePath }
    return $null
}

$stepDebugger = {
    $found = Find-Cdb
    if ($found) { Write-Host "cdb: $found"; return $true }
    $setup = Find-WindowsSdkSetup
    if (-not $setup) {
        Write-Host 'No installed Windows SDK found. 01-setup-windows.ps1 installs it with Visual Studio;' -ForegroundColor Yellow
        Write-Host 'then run this step again.' -ForegroundColor Yellow
        return $false
    }
    Write-Host 'The Visual Studio Installer installs the Windows SDK without the "Debugging Tools for Windows"'
    Write-Host "and cannot add them. This step opens the SDK's own setup to change the installation:"
    Write-Host "  $setup"
    Write-Host 'There tick "Debugging Tools for Windows", leave the other ticks as they are, and click'
    Write-Host '"Change". The setup downloads them; Windows asks for administrator rights.'
    if (-not (Read-YesNo 'Open the setup now?' $true)) { return $false }
    Add-LogLine $log "# $(Get-Stamp) $setup /modify"
    # /modify, as Windows itself passes for "Modify" in Settings > Apps. Without it, the setup takes
    # the SDK as not installed and offers to install it anew or only to download it into a folder.
    # The script waits until the setup is closed.
    $p = Start-Process -FilePath $setup -ArgumentList '/modify' -Wait -PassThru
    Add-LogLine $log "# Exit code: $($p.ExitCode)"
    $found = Find-Cdb
    if (-not $found) { Write-Host 'cdb is still missing: tick "Debugging Tools for Windows" in the setup.' -ForegroundColor Red; return $false }
    Write-Host "cdb: $found"
    return $true
}

$menu = @(
    @{ Title = 'Visual Studio components: Clang compiler (runs 07, 09), AddressSanitizer (runs 09, 10), .NET Framework 4.8.1 SDK (run 04)'
       Done = { (Test-Sanitizers) -and (Test-NetSdk) }; Run = $stepComponents }
    @{ Title = "cppcheck (run 03), into $CppcheckFolder"
       Done = { [bool](Find-Cppcheck) }; Run = $stepCppcheck }
    @{ Title = 'EDG (run 04): opens the separate script 03-setup-edg.ps1'
       Done = { [bool](Get-Cpfe) }; Run = $stepEdg }
    @{ Title = 'WSL with Ubuntu and GCC (run 08): opens the separate script 04-setup-wsl-gcc.ps1'
       Done = { Test-UbuntuPackages }; Run = $stepWslGcc }
    @{ Title = 'Debugger cdb (run 12): opens the setup of the Windows SDK to add the "Debugging Tools for Windows"'
       Done = { [bool](Find-Cdb) }; Run = $stepDebugger }
)
$extra = @(
    @{ Key = '9'; Title = 'Show which analysis runs are ready and what they still need'; Run = $status }
)
# Unattended, the overview goes first, as there is no menu to ask for it.
$info = if ($Unattended) { { & $status; "Log: $log" } } else { { "Log: $log" } }
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the analysis tools' $info $menu $Steps $extra
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
