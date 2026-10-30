<#
.SYNOPSIS
  Run 04: the EDG C++ front end over all sources in src/ except the tests.

.DESCRIPTION
  EDG (edgcpp.org) parses and checks every file like a compiler, without generating code; it
  is a third C++ front end besides MSVC and Clang. Needs the build directory of run 01
  (01-msvc-analyze.ps1) for its compile_commands.json and the generated headers, and EDG built
  by scripts\setup\03-setup-edg.ps1.

  Each MSVC command of the compile database is translated into EDG options:
    -D, -I              -> passed on
    -external:I<dir>    -> --sys_include <dir>   (Qt, vcpkg: fewer warnings from there)
    INCLUDE of MSVC     -> --sys_include <dir>   (C++ standard library, Windows SDK)
    -std:c++17          -> --ms_c++17
    -permissive-        -> --no_ms_permissive
    -Zc:__cplusplus     -> --ms_cplusplus_std_value
    cl.exe version      -> --microsoft_version   (e.g. 19.51 -> 1951)
  EDG has to emulate MSVC: the headers of the MSVC standard library and of Windows use
  Microsoft extensions (e.g. __int64), and EDG's --strict mode can't be combined with that.
  With /permissive- emulated it checks as close to the standard as these headers allow.

  The files run in parallel through a generated ninja file, so the output stays grouped per
  file. --no_wrap_diagnostics keeps every diagnostic on one line, for 10-merge-findings.ps1.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\04-edg.ps1

.PARAMETER EdgDir
  The EDG checkout built by scripts\setup\03-setup-edg.ps1. Default: the EDG_ROOT environment variable.

.PARAMETER Files
  Only files whose path (relative to the repository) matches this regular expression,
  e.g. 'src\\core\\'. Default: all.

.PARAMETER Suppress
  Diagnostic numbers not to report (passed as --diag_suppress). Default: 161, "unrecognized
  #pragma", which EDG reports for MSVC's "#pragma warning" in the Qt and Boost headers (Q_OBJECT)
  hundreds of times. Passing -Suppress replaces the default; -Suppress @() reports everything.

.PARAMETER Remarks
  Also report EDG's remarks (hints below warning level; many).

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER BuildDir
  The build directory of run 01. Default: build-analysis in the repository.

.PARAMETER Yes
  Start without asking.
#>
param(
    [string]$EdgDir = '',
    [string]$Files = '',
    [string[]]$Suppress = @('161'),
    [switch]$Remarks,
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [switch]$Yes
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
if (-not $EdgDir) { $EdgDir = $env:EDG_ROOT }
if (-not $EdgDir) { throw 'EDG not found: pass -EdgDir or set EDG_ROOT.' }
$cpfe = "$EdgDir\build\msvc-release\bin\cpfe.exe"
if (-not (Test-Path $cpfe)) { throw "$cpfe not found: run scripts\setup\03-setup-edg.ps1 first." }
$run = "$BuildDir\edg-run"
$log = "$OutDir\04-edg.log"
Assert-Run01 $BuildDir

if (-not (Confirm-Run 'Analysis run 04: EDG C++ front end' @(
    'EDG, a third C++ front end besides MSVC and Clang, checks every source file in src\ (without'
    'the tests) like a compiler, without generating code. It uses the build directory of run 01.'
    "Build directory of run 01: $BuildDir"
    "EDG: $EdgDir"
) $log)) { Write-Host 'Not started.'; exit 1 }

$sel = Get-FilteredCompileDb $BuildDir
if ($Files) {
    $sel = @($sel | Where-Object { ($_.file -replace '/', '\').Substring($Root.Length + 1) -match $Files })
}
if (-not $sel.Count) { throw 'No source files selected.' }

# Options shared by all files: MSVC version, standard library and Windows SDK headers.
$clVersion = Get-ClVersion
if ($clVersion -notmatch 'Version (\d+)\.(\d+)') { throw "Unknown cl.exe version: $clVersion" }
$msVersion = [int]$Matches[1] * 100 + [int]$Matches[2]
$common = @('--no_code_gen', '--no_wrap_diagnostics', '--display_error_number',
            "--microsoft_version=$msVersion")
if ($Remarks) { $common += '--remarks' }
if ($Suppress) { $common += "--diag_suppress=$($Suppress -join ',')" }
foreach ($dir in (Get-VsEnv 'INCLUDE') -split ';' | Where-Object { $_ }) {
    $common += "--sys_include `"$($dir.TrimEnd('\'))`""
}

# Translates one MSVC command line into EDG options (see the table at the top). A trailing
# backslash is removed from paths, as it would escape the closing quote.
function Convert-Command([string]$Command) {
    $opts = @()
    foreach ($m in [regex]::Matches($Command, '"[^"]*"|\S+')) {
        switch -regex ($m.Value) {
            '^[-/]D(.+)$'          { $opts += "-D$($Matches[1])"; break }
            '^[-/]I(.+)$'          { $opts += "-I`"$($Matches[1].Trim('"').TrimEnd('\'))`""; break }
            '^[-/]external:I(.+)$' { $opts += "--sys_include `"$($Matches[1].Trim('"').TrimEnd('\'))`""; break }
            '^[-/]std:c\+\+(\w+)$' { $opts += "--ms_c++$($Matches[1])"; break }
            '^[-/]permissive-$'    { $opts += '--no_ms_permissive'; break }
            '^[-/]Zc:__cplusplus$' { $opts += '--ms_cplusplus_std_value'; break }
        }
    }
    return $opts -join ' '
}

# Ninja file: one job per source file; "$" is ninja's escape character.
New-Item -ItemType Directory -Force $run | Out-Null
$n = New-Object Text.StringBuilder
[void]$n.AppendLine('rule edg')
[void]$n.AppendLine("  command = `"$cpfe`" $($common -join ' ') `$flags `"`$src`"")
[void]$n.AppendLine('  description = EDG $src')
$i = 0
foreach ($e in $sel) {
    $i++
    $f = $e.file -replace '/', '\'
    [void]$n.AppendLine(('build e{0:D4}: edg' -f $i))
    [void]$n.AppendLine("  src = $($f -replace '\$', '$$$$')")
    [void]$n.AppendLine("  flags = $((Convert-Command $e.command) -replace '\$', '$$$$')")
}
[IO.File]::WriteAllText("$run\build.ninja", $n.ToString(), $Utf8)

New-Log $log '04: EDG C++ front end, MSVC emulation with /permissive-' @(
    "# Files: $($sel.Count) translation units under src\ (without tests and generated files)$(if ($Files) { ", filter '$Files'" })"
    "# EDG: $(git -C $EdgDir log -1 --format='%H (%cs)')"
    "# Compiler emulated: $clVersion"
    "# ninja $(ninja --version)"
    "# Options for all files: $($common -join ' ')"
    "# Per-file options and commands: $run\build.ninja"
    "# Build: ninja -C `"$run`" -k 0"
)
$rc = Invoke-VsLogged "ninja -C `"$run`" -k 0" $log 'EDG'
Add-LogLine $log "`r`n# Ninja exit code: $rc (non-zero if EDG reported an error in any file)"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log")
if ($rc -ne 0) {
    # The known error #2422 in Qt's qrunnable.h fails every file that includes it (see README),
    # but EDG checks those files to the end.
    $errors = @(Select-String -LiteralPath $log -Pattern ': error #' | ForEach-Object Line)
    $known = @($errors | Where-Object { $_ -match 'qrunnable\.h.*error #2422' })
    if ($errors.Count -and $known.Count -eq $errors.Count) {
        Write-Host "All files were checked. The $($known.Count) errors are the known one in Qt's qrunnable.h" -ForegroundColor Yellow
        Write-Host '(#2422, see "What to expect" in scripts\analysis\README.md); there are no others.' -ForegroundColor Yellow
    }
}
exit $rc
