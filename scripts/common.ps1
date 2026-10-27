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
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Utf8 = New-Object Text.UTF8Encoding $false

$vswhereDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$Vs = $null
if (Test-Path "$vswhereDir\vswhere.exe") {
    $Vs = & "$vswhereDir\vswhere.exe" -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ($Vs) {
    $LlvmBin = "$Vs\VC\Tools\Llvm\x64\bin"
    $CMakeBin = "$Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $NinjaBin = "$Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    # vcvars64.bat calls vswhere itself and complains when it isn't on PATH.
    $env:PATH = "$CMakeBin;$NinjaBin;$vswhereDir;$env:PATH"
}

function Get-VcpkgRoot([string]$VcpkgRoot) {
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot -or -not (Test-Path $VcpkgRoot)) {
        throw 'vcpkg not found: pass -VcpkgRoot or set VCPKG_ROOT.'
    }
    return (Resolve-Path $VcpkgRoot).Path
}

# Runs a command line in the Visual Studio x64 developer environment, appending all output to $Log.
# Code page 65001 makes the compilers write their (localized) messages as UTF-8, like the log header.
function Invoke-VsLogged([string]$CommandLine, [string]$Log) {
    cmd /c "chcp 65001 >nul && `"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $CommandLine >> `"$Log`" 2>&1"
    return $LASTEXITCODE
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

# Creates the log with the standard header lines plus run-specific ones.
function New-Log([string]$Log, [string]$Title, [string[]]$Extra = @()) {
    $lines = @(
        "# Run $Title"
        "# Start: $(Get-Stamp)"
        "# Commit: $(git -C $Root rev-parse HEAD)"
        "# Working tree: $((git -C $Root status --porcelain) -join '; ')"
    ) + $Extra + @('')
    New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
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
