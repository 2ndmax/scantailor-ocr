<#
.SYNOPSIS
  Setup 1: everything BUILDING.md describes for Windows, from Visual Studio to the finished build.

.DESCRIPTION
  Shows the steps in a menu; each can be run on its own, 0 runs all of them in order. The vcpkg
  folder is asked for when a step first needs it (3, 4 or 5):
    1. Visual Studio: checks for the C++ tools, the CMake tools and the Windows SDK. Installs
       Visual Studio Community (or the Build Tools) with winget when it is missing, or adds the
       missing components with the Visual Studio Installer. Both go to their standard folder.
    2. Git and GitHub CLI: installs what is missing with winget; then points out the Git
       settings and the GitHub sign-in that are still to do (it does not do them itself).
    3. vcpkg: gets it (git clone, or the ZIP without Git), builds it and installs the packages of
       vcpkg-packages.txt (an hour or more the first time).
    4. VCPKG_ROOT: sets the variable for your user account.
    5. Build: configures and builds ScanTailor OCR in the build folder of the repository; then
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
# SizeGb: about what the installation takes on drive C:, with the components above.
$VsEditions = @(
    @{ Name = 'Visual Studio Community 2026 (with the editor, about 10 GB)'; Id = 'Microsoft.VisualStudio.Community'
       Workload = 'Microsoft.VisualStudio.Workload.NativeDesktop'; SizeGb = 10
       Folder = "$env:ProgramFiles\Microsoft Visual Studio\18\Community" }
    @{ Name = 'Build Tools for Visual Studio 2026 (no editor, about 4 GB)'; Id = 'Microsoft.VisualStudio.BuildTools'
       Workload = 'Microsoft.VisualStudio.Workload.VCTools'; SizeGb = 4
       Folder = 'the standard folder of the Visual Studio Installer' }
)
# vcpkg with all the packages, the first time.
$VcpkgSizeGb = 35

Write-Intro 'ScanTailor OCR - set up the build on Windows' @(
    'This script sets up everything needed to build ScanTailor OCR from this repository:'
    'Visual Studio, vcpkg with the libraries, the variable VCPKG_ROOT and the build itself.'
    "Repository: $Root (the folder this script is in; the build goes to its subfolder build)"
    'It shows the steps in a menu; nothing is installed without asking.'
    ''
    'vcpkg is the package manager that builds Qt and the other libraries; with them its folder'
    "needs at least $VcpkgSizeGb GB. Its folder is asked for when a step first needs it; an existing"
    'one is used as it is.'
)
# Until the folder is asked for, the suggestion stands in for it (the [done] marks of the menu).
$VcpkgAsked = [bool]$VcpkgDir
$VcpkgDir = if ($VcpkgDir) { [IO.Path]::GetFullPath($VcpkgDir).TrimEnd('\') }
            elseif ($env:VCPKG_ROOT) { $env:VCPKG_ROOT }
            else { Join-Path (Get-DefaultBase) 'vcpkg' }
# A drive root such as D:\ (D: after the trim) would put vcpkg's files loose on the drive.
if ($VcpkgAsked -and $VcpkgDir.Length -le 2) { throw "vcpkg needs a folder of its own, not the drive itself: $VcpkgDir\" }
$header = @("# Build directory: $BuildDir")
if ($VcpkgAsked) { $header = @("# vcpkg: $VcpkgDir") + $header }
New-Log $log 'setup 1: Windows build' $header -KeepPrevious

# Asks for the vcpkg folder the first time a step needs it; -VcpkgDir answers it beforehand.
function Confirm-VcpkgDir {
    if ($script:VcpkgAsked) { return }
    if ($env:VCPKG_ROOT -and -not $Unattended) { Write-Host 'The suggestion is the folder in VCPKG_ROOT, which is set already.' }
    while ($true) {
        $folder = Read-Folder 'Folder for vcpkg' $script:VcpkgDir
        # Read-Folder keeps the backslash of a drive root: D:\ is 3 characters.
        if ($folder.Length -gt 3) { break }
        if ($Unattended) { throw "vcpkg needs a folder of its own, not the drive itself: $folder" }
        Write-Host "vcpkg needs a folder of its own, not the drive itself, for example $(Join-Path $folder 'vcpkg')." -ForegroundColor Yellow
    }
    $script:VcpkgDir = $folder
    $script:VcpkgAsked = $true
    Add-LogLine $log "# $(Get-Stamp) vcpkg: $script:VcpkgDir"
}

# Warns when the drive of $Path has less than $NeededGb free. Returns $false when the user stops.
function Confirm-FreeSpace([string]$Path, [int]$NeededGb) {
    $drive = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))
    $info = New-Object IO.DriveInfo $drive
    # A missing drive is reported by the step itself, when it fails to write there.
    if (-not $info.IsReady) { return $true }
    $freeGb = [math]::Floor($info.AvailableFreeSpace / 1GB)
    Add-LogLine $log "# $(Get-Stamp) Free on $drive`: $freeGb GB, needed: about $NeededGb GB"
    if ($freeGb -ge $NeededGb) { return $true }
    Write-Host "Only $freeGb GB are free on $drive; this needs about $NeededGb GB." -ForegroundColor Yellow
    return (Read-YesNo 'Continue anyway?' $false)
}

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
    Write-Host "gigabytes and takes 20 to 60 minutes. It needs about $($edition.SizeGb) GB on drive C:, wherever"
    Write-Host 'vcpkg goes.'
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host 'winget (App Installer) is not available. Install Visual Studio from' -ForegroundColor Yellow
        Write-Host 'https://visualstudio.microsoft.com/vs/community/ as described in BUILDING.md.' -ForegroundColor Yellow
        return $false
    }
    if (-not (Confirm-FreeSpace $env:ProgramFiles $edition.SizeGb)) { return $false }
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
    return $true
}

# --- Step 2: Git and GitHub CLI ---------------------------------------------------------------

$GitTools = @(
    @{ Name = 'Git'; Command = 'git'; Id = 'Git.Git'; Folder = "$env:ProgramFiles\Git" }
    @{ Name = 'GitHub CLI (gh)'; Command = 'gh'; Id = 'GitHub.cli'; Folder = "$env:ProgramFiles\GitHub CLI" }
)

function Get-MissingGitTools {
    return @($GitTools | Where-Object { -not (Get-Command $_.Command -ErrorAction SilentlyContinue) })
}

# Reloads PATH from the registry, so what winget has just installed is found in this window too.
function Update-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}

$stepGit = {
    $missing = @(Get-MissingGitTools)
    if ($missing.Count) {
        Write-Host "Missing: $(($missing | ForEach-Object { $_.Name }) -join ', ')"
        Write-Host 'Git is not needed for the build, but with it step 3 gets vcpkg with its history, so it'
        Write-Host 'can be updated (without Git it comes as a ZIP); EDG (3-setup-edg.ps1) needs Git as well.'
        Write-Host 'gh is the command line for GitHub (pull requests, releases).'
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
            Write-Host 'winget (App Installer) is not available. Install them from https://git-scm.com/download/win' -ForegroundColor Yellow
            Write-Host 'and https://cli.github.com/.' -ForegroundColor Yellow
            return $false
        }
        Write-Host 'Installs them with winget into their standard folders:'
        $missing | ForEach-Object { Write-Host "  $($_.Name): $($_.Folder)" }
        Write-Host 'This means accepting the license terms, which winget shows; Windows may ask for'
        Write-Host 'administrator rights.'
        if (-not (Read-YesNo 'Install now?' $false)) { return $false }
        foreach ($tool in $missing) {
            Add-LogLine $log "# $(Get-Stamp) winget install --id $($tool.Id) --exact --source winget"
            # Run directly in the console, so the questions of winget can be answered.
            & winget install --id $tool.Id --exact --source winget
            $rc = $LASTEXITCODE
            Add-LogLine $log "# Exit code: $rc"
            if ($rc -ne 0) {
                Write-Host "Installing $($tool.Name) failed (winget exit code $rc)." -ForegroundColor Red
                return $false
            }
        }
        Update-Path
        $missing = @(Get-MissingGitTools)
        if ($missing.Count) {
            Write-Host "Installed, but not found: $(($missing | ForEach-Object { $_.Name }) -join ', '). Open a new window." -ForegroundColor Red
            return $false
        }
    }
    foreach ($tool in $GitTools) {
        $version = cmd /c "$($tool.Command) --version 2>nul" | Select-Object -First 1
        $folder = Split-Path (Get-Command $tool.Command).Source
        Write-Host "$($tool.Name): $version ($folder)"
    }
    # Only hints: both concern the user's own accounts.
    if (-not (cmd /c 'git config --global user.name 2>nul') -or -not (cmd /c 'git config --global user.email 2>nul')) {
        Write-Host 'Before the first commit, set your name and e-mail address for Git:' -ForegroundColor Yellow
        Write-Host '  git config --global user.name "Your Name"' -ForegroundColor Yellow
        Write-Host '  git config --global user.email "you@example.com"' -ForegroundColor Yellow
    }
    cmd /c 'gh auth status >nul 2>&1'
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'gh is not signed in to GitHub; to sign in, run: gh auth login' -ForegroundColor Yellow
    }
    return $true
}

# --- Step 3: vcpkg ----------------------------------------------------------------------------

# Downloads a file; curl.exe (Windows 10 1803 and later) is much faster than Invoke-WebRequest in Windows PowerShell 5.1.
function Save-Download([string]$Url, [string]$OutFile) {
    Write-Host "Downloading $Url"
    Add-LogLine $log "# $(Get-Stamp) Download $Url"
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        # --progress-bar: one bar with the percentage instead of curl's table of numbers.
        & curl.exe --location --fail --retry 3 --progress-bar --output $OutFile $Url
        $ok = ($LASTEXITCODE -eq 0)
    } else {
        # The progress bar slows Invoke-WebRequest down by orders of magnitude.
        $oldProgress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $OutFile
            $ok = $true
        } catch {
            Write-Host $_.Exception.Message -ForegroundColor Red
            $ok = $false
        } finally {
            $ProgressPreference = $oldProgress
        }
    }
    if (-not $ok) {
        # A partial file would look like a finished download on the next run.
        if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile }
        Write-Host "Download failed: $Url" -ForegroundColor Red
    }
    return $ok
}

function Get-Vcpkg {
    if (Test-Path "$VcpkgDir\bootstrap-vcpkg.bat") { return $true }
    if ((Test-Path $VcpkgDir) -and (Get-ChildItem $VcpkgDir -Force | Select-Object -First 1)) {
        Write-Host "$VcpkgDir exists, but holds no vcpkg. Choose an empty or new folder." -ForegroundColor Red
        return $false
    }
    if (Get-Command git -ErrorAction SilentlyContinue) {
        return ((Invoke-Shown "git clone https://github.com/microsoft/vcpkg.git `"$VcpkgDir`"" $log) -eq 0)
    }
    # Without Git: the ZIP of the repository, unpacked into a folder of its own next to the vcpkg
    # folder and moved from there. Next to it, so the move stays on the same drive; a folder of its
    # own, so that no folder of the user is touched (one named vcpkg-master may hold a vcpkg
    # unpacked by hand).
    $zip = "$env:TEMP\vcpkg-master.zip"
    $parent = Split-Path $VcpkgDir
    if (-not (Save-Download 'https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip' $zip)) { return $false }
    # A drive root such as D:\ always exists and cannot be created.
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $staging = Join-Path $parent ('vcpkg-unpack-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $staging | Out-Null
    Write-Host "Unpacking to $staging"
    if (Get-Command tar.exe -ErrorAction SilentlyContinue) {
        # tar.exe (bsdtar, Windows 10 1803 and later) reads ZIP files and is far faster than Expand-Archive in PowerShell 5.1.
        & tar.exe -xf $zip -C $staging
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Unpacking failed (tar exit code $LASTEXITCODE)." -ForegroundColor Red
            Remove-Item -LiteralPath $staging -Recurse -Force
            return $false
        }
    } else {
        # The .NET class unpacks without the per-file overhead of Expand-Archive.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $staging)
    }
    # The empty vcpkg folder (checked above) would block the move.
    if (Test-Path -LiteralPath $VcpkgDir) { Remove-Item -LiteralPath $VcpkgDir }
    Move-Item -LiteralPath (Join-Path $staging 'vcpkg-master') -Destination $VcpkgDir
    Remove-Item -LiteralPath $staging
    Remove-Item -LiteralPath $zip
    return $true
}

$stepVcpkg = {
    if (-not $Vs) { Write-Host 'Visual Studio is missing; run step 1 first.' -ForegroundColor Red; return $false }
    Confirm-VcpkgDir
    # Only the first time: later runs add little to the packages already built.
    if (-not (Test-Path "$VcpkgDir\installed\x64-windows\share\qtbase") -and
        -not (Confirm-FreeSpace $VcpkgDir $VcpkgSizeGb)) { return $false }
    if (-not (Get-Vcpkg)) { return $false }
    if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) {
        if ((Invoke-Shown "cd /d `"$VcpkgDir`" && bootstrap-vcpkg.bat -disableMetrics" $log) -ne 0) { return $false }
    }
    $packages = @(Get-Content "$PSScriptRoot\vcpkg-packages.txt" | Where-Object { $_ -and $_ -notmatch '^#' })
    Write-Host "Installs $($packages.Count) packages (list: scripts\setup\vcpkg-packages.txt). The first time,"
    Write-Host "vcpkg compiles Qt and everything else from source: an hour or more and at least $VcpkgSizeGb GB."
    Write-Host 'Packages that are already installed are skipped.'
    if (-not (Read-YesNo 'Install the packages now?' $true)) { return $false }
    $quoted = ($packages | ForEach-Object { "`"$_`"" }) -join ' '
    return ((Invoke-Shown "`"$VcpkgDir\vcpkg.exe`" install --recurse $quoted" $log) -eq 0)
}

# --- Step 4: VCPKG_ROOT -----------------------------------------------------------------------

$stepVcpkgRoot = {
    Confirm-VcpkgDir
    if (-not (Test-Path "$VcpkgDir\vcpkg.exe")) { Write-Host 'vcpkg is missing; run step 3 first.' -ForegroundColor Red; return $false }
    # For the user account (like setx, without its length limit); new windows see it.
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgDir, 'User')
    $env:VCPKG_ROOT = $VcpkgDir
    Add-LogLine $log "# $(Get-Stamp) VCPKG_ROOT=$VcpkgDir (user environment)"
    Write-Host "VCPKG_ROOT = $VcpkgDir for your user account; windows opened from now on see it."
    return $true
}

# --- Step 5: build ----------------------------------------------------------------------------

$stepBuild = {
    if (-not $Vs) { Write-Host 'Visual Studio is missing; run step 1 first.' -ForegroundColor Red; return $false }
    Confirm-VcpkgDir
    if (-not (Test-Path "$VcpkgDir\installed\x64-windows")) {
        Write-Host 'The vcpkg packages are missing; run step 3 first.' -ForegroundColor Red
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
            if (-not (Save-Download $url "$tessdata\$language.traineddata")) { return $false }
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
    @{ Title = 'Git and GitHub CLI: check or install'
       Done = { -not @(Get-MissingGitTools).Count }; Run = $stepGit }
    @{ Title = 'vcpkg: get it and build the libraries (an hour or more the first time)'
       Done = { Test-Path "$VcpkgDir\installed\x64-windows\share\qtbase" }; Run = $stepVcpkg }
    @{ Title = 'Set VCPKG_ROOT for your user account'
       Done = { [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User') -eq $VcpkgDir }; Run = $stepVcpkgRoot }
    @{ Title = 'Configure and build ScanTailor OCR'
       Done = { Test-Path "$BuildDir\scantailor-ocr.exe" }; Run = $stepBuild }
)
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the build on Windows' {
    "Repository: $Root"
    "Log:        $log"
    if ($VcpkgAsked) { "vcpkg:      $VcpkgDir" }
} $menu $Steps
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
