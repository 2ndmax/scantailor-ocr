<#
.SYNOPSIS
  Shared helpers for the analysis runs in this folder. Dot-sourced by the run scripts.

.DESCRIPTION
  Finds Visual Studio (through vswhere), its bundled CMake, Ninja and LLVM tools, and vcpkg
  (VCPKG_ROOT), and provides the logging helpers: every log starts with "# ..." header lines
  (commit, tool versions, command lines) and ends with the exit codes and the end time.
#>

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Utf8 = New-Object Text.UTF8Encoding $false

$vswhereDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$Vs = & "$vswhereDir\vswhere.exe" -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $Vs) { throw 'No Visual Studio installation with the C++ tools found.' }
$LlvmBin = "$Vs\VC\Tools\Llvm\x64\bin"
$CMakeBin = "$Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$NinjaBin = "$Vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
# vcvars64.bat calls vswhere itself and complains when it isn't on PATH.
$env:PATH = "$CMakeBin;$NinjaBin;$vswhereDir;$env:PATH"

function Get-VcpkgRoot([string]$VcpkgRoot) {
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot -or -not (Test-Path $VcpkgRoot)) {
        throw 'vcpkg not found: pass -VcpkgRoot or set VCPKG_ROOT.'
    }
    return (Resolve-Path $VcpkgRoot).Path
}

# The configure arguments shared by all builds (generator, toolchain, project options).
function Get-ConfigureArgs([string]$BuildDir, [string]$VcpkgRoot, [string]$BuildType = 'Release') {
    $toolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" -replace '\\', '/'
    return "-S `"$Root`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$BuildType " +
           "-DCMAKE_TOOLCHAIN_FILE=$toolchain -DVCPKG_TARGET_TRIPLET=x64-windows " +
           '-DENABLE_OCR=ON -DBUILD_TESTS=ON -DBUILD_DEV_TOOLS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
}

# Runs a command line in the Visual Studio x64 developer environment, appending all output to $Log.
# Code page 65001 makes the compilers write their (localized) messages as UTF-8, like the log header.
function Invoke-VsLogged([string]$CommandLine, [string]$Log) {
    cmd /c "chcp 65001 >nul && `"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $CommandLine >> `"$Log`" 2>&1"
    return $LASTEXITCODE
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

# Configures and builds a fresh build directory; returns the exit code of the failing step or 0.
function Invoke-Build([string]$Log, [string]$BuildDir, [string]$Configure) {
    if (Test-Path $BuildDir) { throw "$BuildDir already exists; remove it for a full rebuild." }
    $cfg = "cmake $Configure"
    $build = "cmake --build `"$BuildDir`" -- -k 0"
    Add-LogLine $Log "# Configure: $cfg`r`n"
    $rc = Invoke-VsLogged $cfg $Log
    Add-LogLine $Log "`r`n# Configure exit code: $rc"
    if ($rc -eq 0) {
        Add-LogLine $Log "# Build: $build`r`n"
        $rc = Invoke-VsLogged $build $Log
        Add-LogLine $Log "`r`n# Build exit code: $rc (non-zero if any build step failed)"
    }
    return $rc
}

# The compile database of a build, reduced to the sources under src/ without the tests and
# without generated files. Switches matching any of $DropPatterns are removed from the commands.
function Get-FilteredCompileDb([string]$BuildDir, [string[]]$DropPatterns = @()) {
    $db = Get-Content "$BuildDir\compile_commands.json" -Raw | ConvertFrom-Json
    $src = ($Root -replace '\\', '/') + '/src/'
    $sel = @($db | Where-Object {
        $f = $_.file -replace '\\', '/'
        $f.StartsWith($src) -and $f -notmatch '/(tests?|qt_tests)/'
    })
    foreach ($e in $sel) {
        foreach ($p in $DropPatterns) { $e.command = $e.command -replace $p, '' }
    }
    return , $sel
}

function Save-CompileDb($Entries, [string]$Dir) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    [IO.File]::WriteAllText("$Dir\compile_commands.json", (ConvertTo-Json -InputObject $Entries -Depth 5), $Utf8)
}

# Counts the language standard switches in a compile database, as a check of the configuration.
function Get-StdSwitches([string]$BuildDir) {
    $db = Get-Content "$BuildDir\compile_commands.json" -Raw | ConvertFrom-Json
    return ($db | ForEach-Object { [regex]::Matches($_.command, '[-/]std:\S+') | ForEach-Object Value } |
        Group-Object | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ', '
}
