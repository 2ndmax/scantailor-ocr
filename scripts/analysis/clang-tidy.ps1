<#
.SYNOPSIS
  Run 02: clang-tidy with the repository's .clang-tidy over all sources in src/ except the tests.

.DESCRIPTION
  Needs the build directory of run 01 (msvc-analyze.ps1) for its compile_commands.json and the
  generated headers. The compile database is filtered (tests and generated files removed,
  MSVC /analyze switches stripped, as clang-tidy doesn't know them), and the files are run in
  parallel through a generated ninja file, so the output stays grouped per file.

  Besides the log, clang-tidy's diagnostics are exported as YAML per translation unit into
  <OutDir>\02-clang-tidy-fixes. clang-tidy only writes such a file for units with at least
  one diagnostic. Diagnostics in headers appear once per including source file.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\clang-tidy.ps1

.PARAMETER OutDir
  Where the log and the YAML files go. Default: ANALYSE in the repository.

.PARAMETER BuildDir
  The build directory of run 01. Default: build-analysis in the repository.
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = ''
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = "$Root\ANALYSE" }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
$tidy = "$LlvmBin\clang-tidy.exe"
$dbDir = "$BuildDir\tidy-db"
$run = "$BuildDir\tidy-run"
$fixes = "$OutDir\02-clang-tidy-fixes"
$log = "$OutDir\02-clang-tidy.log"

$sel = Get-FilteredCompileDb $BuildDir @('\s/analyze\S*')
Save-CompileDb $sel $dbDir
New-Item -ItemType Directory -Force $run, $fixes | Out-Null

# Ninja file: one job per source file.
$n = New-Object Text.StringBuilder
[void]$n.AppendLine('rule tidy')
[void]$n.AppendLine("  command = `"$tidy`" -p `"$dbDir`" --export-fixes=`"`$yaml`" `"`$src`"")
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

New-Log $log '02: clang-tidy, checks from .clang-tidy in the repository' (@(
    "# Files: $($sel.Count) translation units under src\ (without tests and generated files)"
    "# Machine-readable diagnostics per file: $fixes\*.yaml (only for files with diagnostics)"
    "# $((& $tidy --version | Select-String 'version') -replace '^\s+', '')"
    "# ninja $(ninja --version)"
    "# Per-file command: `"$tidy`" -p `"$dbDir`" --export-fixes=<yaml> <file>"
    "# Build: ninja -C `"$run`" -k 0"
))
$rc = Invoke-VsLogged "ninja -C `"$run`" -k 0" $log
Add-LogLine $log "`r`n# Ninja exit code: $rc (non-zero if any clang-tidy call failed)"
Add-LogLine $log "# End: $(Get-Stamp)"
exit $rc
