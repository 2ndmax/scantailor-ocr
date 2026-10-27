<#
.SYNOPSIS
  Setup 1: everything BUILDING.md describes for Windows, from Visual Studio to the finished build.

.DESCRIPTION
  Asks for the vcpkg folder, then shows the steps in a menu; each can be run on its own, 0 runs
  all of them in order:
    1. Visual Studio: checks for the C++ tools, the CMake tools and the Windows SDK. Installs
       Visual Studio Community (or the Build Tools) with winget when it is missing, or adds the
       missing components with the Visual Studio Installer. Both go to their standard folder.
    2. vcpkg: gets it (git clone, or the ZIP without Git), builds it and installs the packages of
       vcpkg-packages.txt (an hour or more the first time).
    3. VCPKG_ROOT: sets the variable for your user account.
    4. Build: configures and builds ScanTailor OCR in the build folder of the repository; then
       offers to download the OCR languages German and English and to run the tests.
  Each step skips what is already there. The output also goes to logs\1-setup-windows.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\1-setup-windows.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER VcpkgDir
  The vcpkg folder. Default: VCPKG_ROOT if set, else D:\vcpkg if drive D: exists, else C:\vcpkg.

.PARAMETER Yes
  With -Steps: answer every question with yes (install, download, run the tests).
#>
param(
    [int[]]$Steps = @(),
    [string]$VcpkgDir = '',
    [switch]$Yes
)
. "$PSScriptRoot\..\common.ps1"
. "$PSScriptRoot\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\1-setup-windows.log"
$BuildDir = "$Root\build"

# The components BUILDING.md asks for; the workload is added for the installer's dependencies.
$VsComponents = @(
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    'Microsoft.VisualStudio.Component.VC.CMake.Project'
    'Microsoft.VisualStudio.Component.Windows11SDK.26100'
)
$VsEditions = @(
    @{ Name = 'Visual Studio Community 2026'; Id = 'Microsoft.VisualStudio.Community'
       Workload = 'Microsoft.VisualStudio.Workload.NativeDesktop'
       Folder = "$env:ProgramFiles\Microsoft Visual Studio\18\Community" }
    @{ Name = 'Build Tools for Visual Studio 2026 (no editor, smaller)'; Id = 'Microsoft.VisualStudio.BuildTools'
       Workload = 'Microsoft.VisualStudio.Workload.VCTools'
       Folder = 'the standard folder of the Visual Studio Installer' }
)

Write-Intro 'ScanTailor OCR - set up the build on Windows' @(
    'This script sets up everything needed to build ScanTailor OCR from this repository:'
    'Visual Studio, vcpkg with the libraries, the variable VCPKG_ROOT and the build itself.'
    "Repository: $Root (the folder this script is in; the build goes to its subfolder build)"
    'After the folder question it shows the steps in a menu; nothing is installed without asking.'
    ''
    'vcpkg is the package manager that builds Qt and the other libraries; with them its folder'
    'needs at least 35 GB. An existing vcpkg folder is used as it is.'
)
if (-not $VcpkgDir) {
    $default = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path (Get-DefaultBase) 'vcpkg' }
    if ($env:VCPKG_ROOT -and -not $Unattended) { Write-Host 'The suggestion is the folder in VCPKG_ROOT, which is set already.' }
    $VcpkgDir = Read-Folder 'Folder for vcpkg' $default
} else {
    $VcpkgDir = [IO.Path]::GetFullPath($VcpkgDir).TrimEnd('\')
}
New-Log $log 'setup 1: Windows build' @("# vcpkg: $VcpkgDir", "# Build directory: $BuildDir")

# --- Step 1: Visual Studio --------------------------------------------------------------------

# The C++ components that the newest installed Visual Studio lacks (all of them without one).
function Get-MissingVsComponents {
    $missing = @()
    foreach ($id in $VsComponents) {
        if ($id -like '*Windows11SDK*') {
            # Any Windows 10/11 SDK will do; it is found by its headers, whatever its version.
            if (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\*\um\windows.h") { continue }
        } elseif ($Vs -and (& "$vswhereDir\vswhere.exe" -latest -products * -requires $id -property installationPath)) {
            continue
        }
        $missing += $id
    }
    return $missing
}

function Install-VisualStudio {
    $edition = $VsEditions[(Read-Choice 'Which edition of Visual Studio?' ($VsEditions | ForEach-Object { $_.Name }))]
    Write-Host "Installs $($edition.Name) into $($edition.Folder), with the C++ tools, the CMake tools"
    Write-Host 'and the Windows 11 SDK. This means accepting the license terms of Microsoft, which winget'
    Write-Host 'and the installer show. Windows asks for administrator rights; the download is several'
    Write-Host 'gigabytes and takes 20 to 60 minutes.'
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host 'winget (App Installer) is not available. Install Visual Studio from' -ForegroundColor Yellow
        Write-Host 'https://visualstudio.microsoft.com/vs/community/ as described in BUILDING.md.' -ForegroundColor Yellow
        return $false
    }
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    $adds = (@($edition.Workload) + $VsComponents | ForEach-Object { "--add $_" }) -join ' '
    $override = "--wait --passive --norestart $adds"
    Add-LogLine $log "# $(Get-Stamp) winget install --id $($edition.Id) --exact --source winget --override `"$override`""
    # Run directly in the console, so the questions of winget can be answered.
    & winget install --id $edition.Id --exact --source winget --override $override
    $rc = $LASTEXITCODE
    Add-LogLine $log "# Exit code: $rc"
    return ($rc -eq 0)
}

$stepVisualStudio = {
    $missing = @(Get-MissingVsComponents)
    if (-not $missing.Count) {
        Write-Host "Visual Studio with the C++ tools: $Vs"
    } else {
        $instance = Get-AnyVsInstance
        $ok = if ($instance) { Add-VsComponents $instance $missing $log } else { Install-VisualStudio }
        if (-not $ok) { return $false }
        Find-VisualStudio
        $missing = @(Get-MissingVsComponents)
        if ($missing.Count) {
            Write-Host "Still missing: $($missing -join ', ')" -ForegroundColor Red
            return $false
        }
        Write-Host "Visual Studio with the C++ tools: $Vs"
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Host 'Git is not installed. It is not needed for the build (vcpkg then comes as a ZIP),' -ForegroundColor Yellow
        Write-Host 'but for EDG (3-setup-edg.ps1): https://git-scm.com/download/win' -ForegroundColor Yellow
    }
    return $true
}

# --- Step 2: vcpkg ----------------------------------------------------------------------------

function Get-Vcpkg {
    if (Test-Path "$VcpkgDir\bootstrap-vcpkg.bat") { return $true }
    if ((Test-Path $VcpkgDir) -and (Get-ChildItem $VcpkgDir -Force | Select-Object -First 1)) {
        Write-Host "$VcpkgDir exists, but holds no vcpkg. Choose an empty or new folder." -ForegroundColor Red
        return $false
    }
    if (Get-Command git -ErrorAction SilentlyContinue) {
        return ((Invoke-Shown "git clone https://github.com/microsoft/vcpkg.git `"$VcpkgDir`"" $log) -eq 0)
    }
    # Without Git: the ZIP of the repository, unpacked next to the folder and renamed.
    $zip = "$env:TEMP\vcpkg-master.zip"
    $parent = Split-Path $VcpkgDir
    Write-Host "Downloading vcpkg as a ZIP to $zip"
    Add-LogLine $log "# $(Get-Stamp) Download https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip"
    Invoke-WebRequest -UseBasicParsing 'https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip' -OutFile $zip
    New-Item -ItemType Directory -Force $parent | Out-Null
    Expand-Archive $zip -DestinationPath $parent -Force
    if (Test-Path $VcpkgDir) { Remove-Item $VcpkgDir }
    Rename-Item "$parent\vcpkg-master" (Split-Path $VcpkgDir -Leaf)
    Remove-Item $zip
    return $true
}

$stepVcpkg = {
    if (-not $Vs) { Write-Host 'Visual Studio is missing; run step 1 first.' -ForegroundColor Red; return $false }
    if (-not (Get-Vcpkg)) { return $false }
    if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) {
        if ((Invoke-Shown "cd /d `"$VcpkgDir`" && bootstrap-vcpkg.bat -disableMetrics" $log) -ne 0) { return $false }
    }
    $packages = @(Get-Content "$PSScriptRoot\vcpkg-packages.txt" | Where-Object { $_ -and $_ -notmatch '^#' })
    Write-Host "Installs $($packages.Count) packages (list: scripts\setup\vcpkg-packages.txt). The first time,"
    Write-Host 'vcpkg compiles Qt and everything else from source: an hour or more and at least 35 GB.'
    Write-Host 'Packages that are already installed are skipped.'
    if (-not (Read-YesNo 'Install the packages now?' $true)) { return $false }
    $quoted = ($packages | ForEach-Object { "`"$_`"" }) -join ' '
    return ((Invoke-Shown "`"$VcpkgDir\vcpkg.exe`" install --recurse $quoted" $log) -eq 0)
}

# --- Step 3: VCPKG_ROOT -----------------------------------------------------------------------

$stepVcpkgRoot = {
    if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) { Write-Host 'vcpkg is missing; run step 2 first.' -ForegroundColor Red; return $false }
    # For the user account (like setx, without its length limit); new windows see it.
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgDir, 'User')
    $env:VCPKG_ROOT = $VcpkgDir
    Add-LogLine $log "# $(Get-Stamp) VCPKG_ROOT=$VcpkgDir (user environment)"
    Write-Host "VCPKG_ROOT = $VcpkgDir for your user account; windows opened from now on see it."
    return $true
}

# --- Step 4: build ----------------------------------------------------------------------------

$stepBuild = {
    if (-not $Vs) { Write-Host 'Visual Studio is missing; run step 1 first.' -ForegroundColor Red; return $false }
    if (-not (Test-Path "$VcpkgDir\installed\x64-windows")) {
        Write-Host 'The vcpkg packages are missing; run step 2 first.' -ForegroundColor Red
        return $false
    }
    $toolchain = "$VcpkgDir\scripts\buildsystems\vcpkg.cmake"
    $configure = "cmake -S `"$Root`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release " +
                 "-DCMAKE_TOOLCHAIN_FILE=`"$toolchain`""
    if ((Invoke-Shown $configure $log -InVsEnv) -ne 0) { return $false }
    if ((Invoke-Shown "cmake --build `"$BuildDir`"" $log -InVsEnv) -ne 0) { return $false }
    Write-Host "Built: $BuildDir\scantailor-ocr.exe"

    $tessdata = "$BuildDir\tessdata"
    $languages = @('deu', 'eng') | Where-Object { -not (Test-Path "$tessdata\$_.traineddata") }
    if ($languages -and (Read-YesNo 'Download the OCR languages German and English (about 24 MB)?' $true)) {
        New-Item -ItemType Directory -Force $tessdata | Out-Null
        foreach ($language in $languages) {
            $url = "https://github.com/tesseract-ocr/tessdata_best/raw/main/$language.traineddata"
            Write-Host "Downloading $url"
            Add-LogLine $log "# $(Get-Stamp) Download $url"
            Invoke-WebRequest -UseBasicParsing $url -OutFile "$tessdata\$language.traineddata"
        }
    }
    if (Read-YesNo 'Run the tests (a few minutes)?' $false) {
        if ((Invoke-Shown "ctest --test-dir `"$BuildDir`" --output-on-failure" $log -InVsEnv) -ne 0) { return $false }
    }
    return $true
}

$menu = @(
    @{ Title = 'Visual Studio: check, install or add the C++ components'
       Done = { $Vs -and -not @(Get-MissingVsComponents).Count }; Run = $stepVisualStudio }
    @{ Title = 'vcpkg: get it and build the libraries (an hour or more the first time)'
       Done = { Test-Path "$VcpkgDir\installed\x64-windows\share\qtbase" }; Run = $stepVcpkg }
    @{ Title = 'Set VCPKG_ROOT for your user account'
       Done = { [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User') -eq $VcpkgDir }; Run = $stepVcpkgRoot }
    @{ Title = 'Configure and build ScanTailor OCR'
       Done = { Test-Path "$BuildDir\scantailor-ocr.exe" }; Run = $stepBuild }
)
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the build on Windows' @(
    "vcpkg:      $VcpkgDir"
    "Repository: $Root"
    "Log:        $log"
) $menu $Steps
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
