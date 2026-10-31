<#
.SYNOPSIS
  Shared helpers for the scripts in setup\ and analysis\. Dot-sourced by them.

.DESCRIPTION
  Finds Visual Studio (through vswhere), its bundled CMake, Ninja and LLVM tools, and vcpkg
  (VCPKG_ROOT), and provides the logging helpers: every log starts with "# ..." header lines
  (commit, tool versions, command lines) and ends with the exit codes and the end time.
  A missing Visual Studio leaves $Vs empty; the scripts that need it check that themselves.
#>

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Utf8 = New-Object Text.UTF8Encoding $false

$vswhereDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"

# A window opened before step 4 of the setup set VCPKG_ROOT for the user account doesn't have it
# (a process gets its environment once, at its start), so it is taken from the account here.
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
}

# Sets $Vs to the newest Visual Studio (or Build Tools) with the C++ tools, and the folders of its
# CMake, Ninja and LLVM tools. Called again by the setup after installing Visual Studio.
function Find-VisualStudio {
    $script:Vs = $null
    if (Test-Path "$vswhereDir\vswhere.exe") {
        $script:Vs = & "$vswhereDir\vswhere.exe" -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if ($script:Vs) {
        $script:LlvmBin = "$script:Vs\VC\Tools\Llvm\x64\bin"
        $script:CMakeBin = "$script:Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        $script:NinjaBin = "$script:Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
        # vcvars64.bat calls vswhere itself and complains when it isn't on PATH.
        $env:PATH = "$script:CMakeBin;$script:NinjaBin;$vswhereDir;$env:PATH"
    }
}
Find-VisualStudio

# cppcheck.exe from PATH or its standard folder (where winget installs it), or $null.
function Find-Cppcheck {
    $command = Get-Command cppcheck -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $standard = "$env:ProgramFiles\Cppcheck\cppcheck.exe"
    if (Test-Path $standard) { return $standard }
    return $null
}

# The Linux of the GCC build in WSL and the packages it needs there.
$WslDistro = 'Ubuntu-24.04'
$UbuntuPackages = @(Get-Content "$Root\scripts\setup\support\ubuntu-packages.txt" |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and $_ -notmatch '^#' })

# WSL itself, as "wsl --install" installs it. Not the wsl.exe in System32: before WSL is
# installed that one is a stub, which may start the installation when called.
$WslExe = "$env:ProgramFiles\WSL\wsl.exe"

function Test-Wsl { return (Test-Path $WslExe) }

# wsl.exe writes its own messages as UTF-16 unless WSL_UTF8 is set.
$env:WSL_UTF8 = '1'

# Runs WSL with $Arguments (one string, through cmd) and returns its exit code and output lines.
# Without WSL: exit code 1 and no lines.
function Get-WslOutput([string]$Arguments) {
    if (-not (Test-Wsl)) { return @{ Code = 1; Lines = @() } }
    $lines = @(cmd /c "`"$WslExe`" $Arguments 2>&1" | ForEach-Object { "$_".TrimEnd() } | Where-Object { $_ })
    return @{ Code = $LASTEXITCODE; Lines = $lines }
}

function Test-WslDistro {
    if (-not (Test-Wsl)) { return $false }
    return ((Get-WslOutput '--list --quiet').Lines -contains $WslDistro)
}

# A Windows path as WSL sees it, e.g. C:\Users\x -> /mnt/c/Users/x.
function ConvertTo-WslPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    return '/mnt/' + $full.Substring(0, 1).ToLowerInvariant() + ($full.Substring(2) -replace '\\', '/')
}

# The packages of $UbuntuPackages not installed in $WslDistro (all of them without it). --exec
# runs dpkg-query without a shell, which would otherwise replace ${Package} and ${Status}.
function Get-MissingUbuntuPackages {
    if (-not (Test-WslDistro)) { return $UbuntuPackages }
    $result = Get-WslOutput ("-d $WslDistro -u root --exec dpkg-query -W " + '-f=${Package}:${Status}\n ' +
                             ($UbuntuPackages -join ' '))
    $installed = @($result.Lines | Where-Object { $_ -match ':install ok installed$' } | ForEach-Object { ($_ -split ':')[0] })
    return @($UbuntuPackages | Where-Object { $installed -notcontains $_ })
}

function Test-UbuntuPackages { return (-not @(Get-MissingUbuntuPackages).Count) }

function Get-VcpkgRoot([string]$VcpkgRoot) {
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot -or -not (Test-Path $VcpkgRoot)) {
        throw 'vcpkg not found: pass -VcpkgRoot or set VCPKG_ROOT.'
    }
    return (Resolve-Path $VcpkgRoot).Path
}

# Set by the scripts: $true when they run without questions (-Steps in the setup, -Yes in the
# analysis runs); $AssumeYes then answers every question with yes.
$Unattended = $false
$AssumeYes = $false

# A yes/no question; Enter takes $Default. Unattended: $Default, or yes with $AssumeYes.
function Read-YesNo([string]$Question, [bool]$Default) {
    if ($Unattended) { return ($AssumeYes -or $Default) }
    $hint = if ($Default) { '[Y/n]' } else { '[y/N]' }
    while ($true) {
        $typed = (Read-Host "$Question $hint").Trim().ToLowerInvariant()
        if (-not $typed) { return $Default }
        if ($typed -in 'y', 'yes', 'j', 'ja') { return $true }
        if ($typed -in 'n', 'no', 'nein') { return $false }
    }
}

# Runs a command line through cmd and appends all its output to $Log. The window shows the
# progress instead, in one line updated in place: the step of ninja ("[12/345] File.cpp") or of
# cppcheck ("12/345 files checked"), else $Activity, with the warnings and errors so far and the
# time; errors are shown in full as well. Returns the exit code.
# Code page 65001 makes the compilers write their (localized) messages as UTF-8, like the log header.
function Invoke-Streamed([string]$CommandLine, [string]$Log, [string]$Activity = 'Running') {
    $writer = New-Object IO.StreamWriter($Log, $true, $Utf8)
    $writer.AutoFlush = $true
    $oldEncoding = [Console]::OutputEncoding
    # Without a console (e.g. in the ISE) the encoding can't be set; the log then may show
    # umlauts wrongly, nothing else.
    try { [Console]::OutputEncoding = $Utf8 } catch {}
    $width = 120
    try { $width = [math]::Max(40, $Host.UI.RawUI.WindowSize.Width) } catch {}
    $blank = "`r" + (' ' * ($width - 1)) + "`r"
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $shownAt = -1000
    $step = $Activity
    $warnings = 0
    $errors = 0
    try {
        # ForEach-Object runs in this scope, so the counters below keep their values.
        cmd /c "chcp 65001 >nul && $CommandLine 2>&1" | ForEach-Object {
            $line = "$_"
            $writer.WriteLine($line)
            if ($line -match '^\[(\d+)/(\d+)\]\s+(.*)$' -or $line -match '^(\d+)/(\d+) files checked') {
                # The file name alone, from e.g. "Building CXX object src\core\CMakeFiles\...\File.cpp.obj".
                $item = if ($Matches[3]) { ($Matches[3] -split '\s+')[-1] -replace '^.*[\\/]', '' } else { '' }
                $step = ('[{0}/{1}] {2}% {3}' -f $Matches[1], $Matches[2], [int](100 * $Matches[1] / $Matches[2]), ($item -replace '\.obj$', '')).TrimEnd()
            }
            # ninja's "FAILED:" line names the step of an error that is already counted.
            if ($line -match ': (fatal )?error\b|^FAILED: |^CMake Error') {
                if ($line -notmatch '^FAILED: ') { $errors++ }
                Write-Host $blank -NoNewline
                Write-Host $line -ForegroundColor Red
                $shownAt = -1000
            } elseif ($line -match ': warning\b') {
                $warnings++
            }
            # At most a few updates a second: writing to the console is slow.
            if ($clock.ElapsedMilliseconds - $shownAt -ge 300) {
                $status = "{0} | {1} warnings, {2} errors | {3:hh\:mm\:ss}" -f $step, $warnings, $errors, $clock.Elapsed
                Write-Host ("`r" + $status.PadRight($width - 1).Substring(0, $width - 1)) -NoNewline
                $shownAt = $clock.ElapsedMilliseconds
            }
        }
        $rc = $LASTEXITCODE
    } finally {
        $writer.Close()
        try { [Console]::OutputEncoding = $oldEncoding } catch {}
    }
    $status = "{0} | {1} warnings, {2} errors | {3:hh\:mm\:ss}" -f $step, $warnings, $errors, $clock.Elapsed
    Write-Host ("`r" + $status.PadRight($width - 1).Substring(0, $width - 1))
    if ($rc -ne 0) { Write-Host "Exit code $rc; the details are in $Log" -ForegroundColor Red }
    return $rc
}

# Runs a command line in the Visual Studio x64 developer environment, appending all output to $Log
# and showing the progress (see Invoke-Streamed).
function Invoke-VsLogged([string]$CommandLine, [string]$Log, [string]$Activity = 'Running') {
    return (Invoke-Streamed "`"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $CommandLine" $Log $Activity)
}

# The value of an environment variable inside the developer environment (e.g. INCLUDE).
function Get-VsEnv([string]$Name) {
    $line = cmd /c "`"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set $Name" |
        Where-Object { $_ -like "$Name=*" } | Select-Object -First 1
    return "$line".Substring($Name.Length + 1)
}

function Get-Stamp { Get-Date -Format 'yyyy-MM-dd HH:mm:ss' }

function Add-LogLine([string]$Log, [string]$Text) {
    [IO.File]::AppendAllText($Log, $Text + "`r`n", $Utf8)
}

# Creates the log with the standard header lines plus run-specific ones. -KeepPrevious keeps the
# log of the run before as <name>.previous.log, in case that run failed.
function New-Log([string]$Log, [string]$Title, [string[]]$Extra = @(), [switch]$KeepPrevious) {
    # Through cmd, so a missing Git (possible before the setup) or a ZIP download of the
    # repository only leaves the lines empty. Not inside the strings below: Windows PowerShell
    # garbles the escaped quotes of a string nested in $() of another string.
    $commit = cmd /c "git -C `"$Root`" rev-parse HEAD 2>nul"
    $changes = (cmd /c "git -C `"$Root`" status --porcelain 2>nul") -join '; '
    $lines = @(
        "# Run $Title"
        "# Start: $(Get-Stamp)"
        "# Commit: $commit"
        "# Working tree: $changes"
    ) + $Extra + @('')
    New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
    if ($KeepPrevious -and (Test-Path -LiteralPath $Log)) {
        Move-Item -LiteralPath $Log ([IO.Path]::ChangeExtension($Log, '.previous.log')) -Force
    }
    [IO.File]::WriteAllLines($Log, $lines, $Utf8)
}

function Get-ClVersion {
    $line = cmd /c "`"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl 2>&1" | Select-String 'Version' |
        Select-Object -First 1
    return "$line".Trim()
}

function Get-ToolVersions {
    return @(
        "# $((cmake --version | Select-Object -First 1))"
        "# ninja $(ninja --version)"
    )
}
