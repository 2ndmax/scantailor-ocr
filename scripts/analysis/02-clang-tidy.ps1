<#
.SYNOPSIS
  Run 02: clang-tidy with the repository's .clang-tidy over all sources in src/ except the tests.

.DESCRIPTION
  Needs the build directory of run 01 (01-msvc-analyze.ps1) for its compile_commands.json and the
  generated headers. The compile database is filtered (tests and generated files removed,
  MSVC /analyze switches stripped, as clang-tidy doesn't know them), and the files are run in
  parallel through a generated ninja file, so the output stays grouped per file.

  When started without -Extended, -ExtraChecks or -Yes, asks whether to run only the standard
  checks of .clang-tidy or the extended set (which adds 14 curated bug-pattern and clarity checks
  on top of .clang-tidy).

  Besides the log, clang-tidy's diagnostics are exported as YAML per translation unit into
  <OutDir>\02-clang-tidy-fixes. clang-tidy only writes such a file for units with at least
  one diagnostic. Diagnostics in headers appear once per including source file.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\02-clang-tidy.ps1

.PARAMETER OutDir
  Where the log and the YAML files go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory of run 01. Default: build-analysis in the repository.

.PARAMETER Extended
  Add the curated set of 14 extra bug-pattern and clarity checks on top of .clang-tidy. Skips
  the interactive check-mode prompt.

.PARAMETER ExtraChecks
  Additional clang-tidy check patterns (comma-separated or array) appended on top of .clang-tidy.
  Skips the interactive check-mode prompt; can be combined with -Extended.

.PARAMETER Yes
  Start without asking (uses standard .clang-tidy unless -Extended or -ExtraChecks is given).
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [switch]$Extended,
    [string[]]$ExtraChecks = @(),
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
$tidy = "$LlvmBin\clang-tidy.exe"
$dbDir = "$BuildDir\tidy-db"
$run = "$BuildDir\tidy-run"
$fixes = "$OutDir\02-clang-tidy-fixes"
$log = "$OutDir\02-clang-tidy.log"
Assert-Run01 $BuildDir

$extendedChecks = @(
    'bugprone-branch-clone'
    'bugprone-empty-catch'
    'bugprone-exception-escape'
    'bugprone-derived-method-shadowing-base-method'
    'bugprone-return-const-ref-from-parameter'
    'bugprone-copy-constructor-init'
    'bugprone-signed-char-misuse'
    'bugprone-unhandled-self-assignment'
    'misc-redundant-expression'
    'misc-unused-using-decls'
    'readability-redundant-control-flow'
    'readability-simplify-boolean-expr'
    'readability-misleading-indentation'
    'readability-function-cognitive-complexity'
)

function Read-CheckMode {
    Write-Host ''
    Write-Host 'Which checks?'
    Write-Host '  1  Standard (.clang-tidy)'
    Write-Host "  2  Extended (.clang-tidy + $($extendedChecks.Count) extra bug/clarity checks)"
    Write-Host '  Q  Quit'
    while ($true) {
        $typed = (Read-Host 'Choice [1]').Trim()
        if ($typed -match '^[qQ]$') {
            Write-Host 'Not started.'
            exit 1
        }
        if (-not $typed -or $typed -eq '1') { return $false }
        if ($typed -eq '2') { return $true }
        Write-Host 'Choose 1, 2 or Q.' -ForegroundColor Yellow
    }
}

$customExtra = @($ExtraChecks | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Extended -and -not $customExtra.Count -and -not $Yes) {
    $Extended = Read-CheckMode
}

$addedChecks = @()
if ($Extended) { $addedChecks += $extendedChecks }
if ($customExtra.Count) { $addedChecks += $customExtra }
$addedChecks = @($addedChecks | Select-Object -Unique)
$checksFlag = if ($addedChecks.Count) { " --checks=`"$($addedChecks -join ',')`"" } else { '' }
$modeDesc = if ($addedChecks.Count) { "Extended (.clang-tidy + $($addedChecks -join ', '))" } else { 'Standard (.clang-tidy)' }

$intro = @(
    'clang-tidy checks every source file in src\ (without the tests) with the checks in'
    '.clang-tidy, several files at a time. It uses the build directory of run 01.'
    "Mode: $modeDesc"
    "Build directory of run 01: $BuildDir"
)
if (-not (Confirm-Run 'Analysis run 02: clang-tidy' $intro $log)) { Write-Host 'Not started.'; exit 1 }

$sel = Get-FilteredCompileDb $BuildDir @('\s/analyze\S*')
Save-CompileDb $sel $dbDir
New-Item -ItemType Directory -Force $run, $fixes | Out-Null

# Ninja file: one job per source file.
$n = New-Object Text.StringBuilder
[void]$n.AppendLine('rule tidy')
[void]$n.AppendLine("  command = `"$tidy`" -p `"$dbDir`"$checksFlag --export-fixes=`"`$yaml`" `"`$src`"")
[void]$n.AppendLine('  description = clang-tidy $src')
$i = 0
foreach ($e in $sel) {
    $i++
    $f = $e.file -replace '/', '\'
    $name = $f.Substring($Root.Length + 1) -replace '[\\/.]', '_'
    [void]$n.AppendLine(('build t{0:D4}: tidy' -f $i))
    [void]$n.AppendLine("  src = $f")
    [void]$n.AppendLine("  yaml = $fixes\$name.yaml")
}
[IO.File]::WriteAllText("$run\build.ninja", $n.ToString(), $Utf8)

$logMeta = @(
    "# Files: $($sel.Count) translation units under src\ (without tests and generated files)"
    "# Mode: $modeDesc"
)
if ($addedChecks.Count) {
    $logMeta += "# Extra checks: $($addedChecks -join ', ')"
}
$logMeta += @(
    "# Machine-readable diagnostics per file: $fixes\*.yaml (only for files with diagnostics)"
    "# $((& $tidy --version | Select-String 'version') -replace '^\s+', '')"
    "# ninja $(ninja --version)"
    "# Per-file command: `"$tidy`" -p `"$dbDir`"$checksFlag --export-fixes=<yaml> <file>"
    "# Build: ninja -C `"$run`" -k 0"
)
New-Log $log '02: clang-tidy, checks from .clang-tidy in the repository' $logMeta
$rc = Invoke-VsLogged "ninja -C `"$run`" -k 0" $log 'clang-tidy'
Add-LogLine $log "`r`n# Ninja exit code: $rc (non-zero if any clang-tidy call failed)"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log", "Diagnostics as YAML: $fixes")
exit $rc
