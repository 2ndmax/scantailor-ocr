<#
.SYNOPSIS
  Helpers for the analysis runs in this folder. Dot-sourced by the run scripts.

.DESCRIPTION
  Loads the shared helpers of ..\..\support\common.ps1 (Visual Studio, vcpkg, logging, configure
  and build) and adds the compile database helpers of the runs.
#>

. "$PSScriptRoot\..\..\support\common.ps1"
if (-not $Vs) {
    throw 'No Visual Studio installation with the C++ tools found: scripts\setup\01-setup-windows.ps1 installs it.'
}
# All logs and results go here unless a script is given -OutDir (ignored by git).
$ResultsDir = "$(Split-Path $PSScriptRoot)\results"
# The -Yes of the run scripts: start without asking.
$Unattended = [bool]$Yes
$AssumeYes = [bool]$Yes

# The runs 02 to 04 read the compile database of run 01.
function Assert-Run01([string]$BuildDir) {
    if (-not (Test-Path "$BuildDir\compile_commands.json")) {
        throw "$BuildDir\compile_commands.json not found: run 01-msvc-analyze.ps1 first."
    }
}

# clang-cl.exe of Visual Studio, for the runs 07 and 09.
function Get-ClangCl {
    $clang = "$LlvmBin\clang-cl.exe"
    if (-not (Test-Path $clang)) {
        throw "clang-cl not found in $LlvmBin; scripts\setup\02-setup-analysis.ps1 adds the Visual Studio " +
              "component 'C++ Clang Compiler for Windows'."
    }
    return $clang
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

# --- Shared by the runs that start the test programs and dev tools (09, 12) -----------------------

# The Boost.Test programs; qt_tests is started on its own, as it writes its results into files.
$BoostTestPrograms = 'foundation_tests.exe', 'math_tests.exe', 'imageproc_tests.exe', 'core_tests.exe'

# Only the "windows" Qt platform plugin is deployed to the build directory; the programs run
function Save-EnvVars([string[]]$Names) {
    $saved = @{}
    foreach ($n in $Names) {
        $item = Get-Item -Path "Env:$n" -ErrorAction SilentlyContinue
        $saved[$n] = if ($item) { $item.Value } else { $null }
    }
    return $saved
}

function Restore-EnvVars([hashtable]$Saved) {
    foreach ($kv in $Saved.GetEnumerator()) {
        if ($null -eq $kv.Value) {
            Remove-Item -Path "Env:$($kv.Key)" -ErrorAction SilentlyContinue
        } else {
            Set-Item -Path "Env:$($kv.Key)" -Value $kv.Value
        }
    }
}

# headless with "offscreen", so that plugin is copied from vcpkg.
function Enable-OffscreenQt([string]$BuildDir, [string]$VcpkgRoot) {
    $env:QT_QPA_PLATFORM = 'offscreen'
    Copy-Item "$VcpkgRoot\installed\x64-windows\Qt6\plugins\platforms\qoffscreen.dll" "$BuildDir\platforms" -Force
}

# The redirect files may still be held open briefly after the process ends.
function Read-Shared([string]$Path) {
    for ($i = 0; ; $i++) {
        try {
            $fs = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
            try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
        } catch {
            if ($i -ge 20) { return "<could not read $Path : $_>" }
            Start-Sleep -Milliseconds 500
        }
    }
}

# Boost.Test ends with "*** No errors detected" or "*** N failure(s) ... detected", Qt Test with
# "Totals:". Without either, the program stopped before its tests, and its exit code alone
# doesn't show that.
function Test-TestSummary([string]$Output) {
    $summary = '\*\*\* No errors detected|\*\*\* \d+ failures? (is|are) detected|^Totals:'
    return [regex]::IsMatch($Output, $summary, 'Multiline')
}

# Copies the scans of $Images (.tif, .tiff, .jp2, top level only) into the build directory for the
# dev tools. Returns the log header lines that name them, and the dev tool calls (Exe, Args).
function New-DevToolInput([string]$Images, [string]$BuildDir) {
    $in = "$BuildDir\tool-input"
    $res = "$BuildDir\tool-output"
    New-Item -ItemType Directory -Force $in, "$res\binarization", "$res\binarization-colors", "$res\color_detect" | Out-Null
    $imgs = Get-ChildItem $Images -File | Where-Object { $_.Extension -in '.tif', '.tiff', '.jp2' }
    foreach ($i in $imgs) { Copy-Item $i.FullName $in -Force }
    $header = @(
        "# Input: copies of $($imgs.Count) images from $Images (top level, tif/jp2) in $in`:"
    ) + @($imgs | ForEach-Object { "#   $($_.Name) ($([int]($_.Length / 1KB)) KB)" }) + @(
        "# Output images: $res (not part of this log)"
    )
    $calls = @(
        @{ Exe = 'binarization_compare.exe'; Args = "`"$res\binarization`" `"$in`"" }
        @{ Exe = 'binarization_compare.exe'; Args = "--colors `"$res\binarization-colors`" `"$in`"" }
        @{ Exe = 'color_detect.exe'; Args = "`"$in`" `"$res\color_detect`" --zones" }
    )
    return @{ Header = $header; Calls = $calls }
}

# The MSVC conformance runs 05 and 06: a full rebuild with /W4 (dependency headers external,
# their warnings suppressed) like run 01, but without /analyze, so their warnings can be compared
# with the C4xxx warnings of run 01. $Flags are added to /W4, $Extra to the configure arguments.
function Invoke-ConformanceRun([string]$Id, [string]$Name, [string]$Title, [string]$Flags,
                               [string]$Extra, [string]$OutDir, [string]$BuildDir, [string]$VcpkgRoot) {
    $flagsAll = "/W4 /external:W0 /external:anglebrackets $Flags".Trim()
    $log = "$OutDir\$Id-$Name.log"
    $savedEnv = Save-EnvVars @('CFLAGS', 'CXXFLAGS')
    try {
        $env:CFLAGS = $flagsAll
        $env:CXXFLAGS = $flagsAll
        New-Log $log "${Id}: $Title, full rebuild in $(Split-Path $BuildDir -Leaf)" (@(
            "# Compiler: $(Get-ClVersion)"
        ) + (Get-ToolVersions) + @("# CFLAGS/CXXFLAGS env: $flagsAll"))
        $rc = Invoke-Build $log $BuildDir "$(Get-ConfigureArgs $BuildDir $VcpkgRoot) -DENABLE_CODE_ANALYSIS=OFF $Extra"
        if (Test-Path "$BuildDir\compile_commands.json") {
            # Records the language standard actually used, as a check of the override.
            Add-LogLine $log "# Standard switches in compile_commands.json: $(Get-StdSwitches $BuildDir)"
        }
        Add-LogLine $log "# End: $(Get-Stamp)"
        return $rc
    } finally {
        Restore-EnvVars $savedEnv
    }
}
