<#
.SYNOPSIS
  Merges the raw output of the analysis runs into one table of distinct findings.

.DESCRIPTION
  Reads the logs written by the run scripts in this folder (01-07) and collects every warning
  or runtime report with its location.  A finding is one combination of tool, check, file and
  line; repeats (a header reported once per including file, the same warning in the runs 01, 04
  and 05) are merged, and the runs that reported it are listed.

  Writes two files into the analysis folder:
    findings.csv  all findings, one per row (semicolon separated, UTF-8 with BOM for Excel)
    findings.md   a summary per tool and check, then the findings grouped by file and line.
                  Checks listed in -BulkChecks are only counted there, not listed.

  Not read: the failed setup attempts of run 07 (07-*-failed.log, 07-*-aborted.log).
  Does not need Visual Studio or any of the tools.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\analysis\merge-findings.ps1
#>
param(
  [string] $AnalysisDir,
  # Frequent, mostly harmless checks: counted in findings.md, listed only in findings.csv.
  [string[]] $BulkChecks = @(
    'C4100', '-Wunused-parameter',                  # unreferenced parameter
    'C4267',                                        # size_t to a smaller type
    'C4456', 'C4457', 'C4458', 'C4459', 'C6246'     # a name hides another one
  )
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $AnalysisDir) { $AnalysisDir = Join-Path $Root 'ANALYSE' }
$AnalysisDir = (Resolve-Path $AnalysisDir).Path
$Utf8 = New-Object Text.UTF8Encoding $false

$findings = @{}  # key -> finding
$order = New-Object System.Collections.Generic.List[string]

# Makes a path relative to the repository, with forward slashes.  Paths outside it
# (vcpkg, Windows SDK) are kept and marked as external.
function Get-RelPath([string] $path) {
  $p = $path.Trim() -replace '/', '\'
  if ($p -match '^[A-Za-z]:\\') { $p = [IO.Path]::GetFullPath($p) }  # resolves "..\" in generated files
  if ($p.StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase)) {
    return $p.Substring($Root.Length + 1) -replace '\\', '/'
  }
  return "(extern) " + ($p -replace '\\', '/')
}

function Add-Finding($tool, $run, $check, $file, $line, $msg) {
  $rel = Get-RelPath $file
  $key = "$tool|$check|$($rel.ToLowerInvariant())|$line"
  if (-not $findings.ContainsKey($key)) {
    $findings[$key] = [pscustomobject]@{
      Tool = $tool; Runs = New-Object System.Collections.Generic.SortedSet[string]
      Check = $check; File = $rel; Line = [int]$line; Message = $msg.Trim(); Count = 0
    }
    $order.Add($key)
  }
  $f = $findings[$key]
  [void]$f.Runs.Add($run)
  $f.Count++
}

function Read-Lines([string] $name) {
  $path = Join-Path $AnalysisDir $name
  if (-not (Test-Path $path)) { Write-Warning "missing: $name"; return @() }
  return [IO.File]::ReadAllLines($path, $Utf8)
}

# MSVC: file(line): warning C4xxx: text   and   file(line) : warning C6xxx: text
$reMsvc = '^(?<file>[A-Za-z]:\\[^(]+)\((?<line>\d+)(,\d+)?\)\s?:\s+warning (?<code>C\d{4}):\s*(?<msg>.*)$'
foreach ($run in @(@('01', '01-msvc-analyze.log'), @('04', '04-msvc-cxx20.log'),
                   @('05', '05-msvc-zc-preprocessor.log'))) {
  foreach ($l in Read-Lines $run[1]) {
    if ($l -match $reMsvc) {
      Add-Finding 'MSVC' $run[0] $Matches.code $Matches.file $Matches.line $Matches.msg
    }
  }
}

# Clang: file:line:col: warning: text [check]   or (clang-cl)   file(line,col): warning: text [-Wx]
$reClang = '^(?<file>[A-Za-z]:\\.+?)(?::(?<line>\d+):\d+|\((?<line>\d+),\d+\)):\s+(?:warning|error):\s+(?<msg>.*?)\s+\[(?<check>[^\]]+)\]\s*$'
foreach ($run in @(@('clang-tidy', '02', '02-clang-tidy.log'), @('clang-cl', '06', '06-clang-cl.log'),
                   @('clang-cl', '07', '07-ubsan-build.log'))) {
  foreach ($l in Read-Lines $run[2]) {
    if ($l -match $reClang) {
      $check = ($Matches.check -split ',')[0]
      Add-Finding $run[0] $run[1] $check $Matches.file $Matches.line $Matches.msg
    }
  }
}

# cppcheck: the first <location> of an error is its primary location.
$xmlPath = Join-Path $AnalysisDir '03-cppcheck.xml'
if (Test-Path $xmlPath) {
  [xml]$xml = [IO.File]::ReadAllText($xmlPath, $Utf8)
  foreach ($e in $xml.results.errors.error) {
    $loc = @($e.location)[0]
    if ($loc) { Add-Finding 'cppcheck' '03' $e.id $loc.file $loc.line $e.msg }
    else { Add-Finding 'cppcheck' '03' $e.id '(no file)' 0 $e.msg }
  }
} else { Write-Warning "missing: 03-cppcheck.xml" }

# Sanitizers: UBSan reports its location directly; for ASan the first stack frame
# in the project's own code is taken (or the first frame at all, if there is none).
$reUbsan = '^(?<file>[A-Za-z]:\\.+?):(?<line>\d+):\d+: runtime error: (?<msg>.*)$'
$reFrame = '^\s+#\d+ 0x[0-9a-f]+ in \S+ (?<file>[A-Za-z]:\\.+?):(?<line>\d+)'
foreach ($name in @('07-ubsan-tests.log', '07-ubsan-tools.log')) {
  $lines = Read-Lines $name
  # The type of a report (shift-base, allocation-size-too-big ...) is in its SUMMARY line.
  function Get-SummaryType([int] $from, [string] $fallback) {
    for ($j = $from; $j -lt $lines.Count -and $j -le $from + 80; $j++) {
      if ($lines[$j] -match '^SUMMARY: \w+Sanitizer: (?<type>[\w-]+)') { return $Matches.type }
    }
    return $fallback
  }
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i]
    if ($l -match $reUbsan) {
      $file = $Matches.file; $line = $Matches.line; $msg = $Matches.msg
      Add-Finding 'UBSan' '07' (Get-SummaryType $i 'runtime error') $file $line $msg
    } elseif ($l -match 'ERROR: AddressSanitizer: (?<msg>.*)$') {
      $msg = $Matches.msg -replace '0x[0-9a-f]+', '0x...'
      $check = Get-SummaryType $i 'AddressSanitizer'
      $file = '(no stack frame)'; $line = 0
      for ($j = $i + 1; $j -lt $lines.Count -and $j -le $i + 60; $j++) {
        if ($lines[$j] -match $reFrame) {
          if ($file -eq '(no stack frame)') { $file = $Matches.file; $line = $Matches.line }
          if ((Get-RelPath $Matches.file) -like 'src/*') { $file = $Matches.file; $line = $Matches.line; break }
        } elseif ($lines[$j] -match '^\s*$' -and $file -ne '(no stack frame)') { break }
      }
      Add-Finding 'ASan' '07' $check $file $line $msg
    }
  }
}

$all = foreach ($k in $order) {
  $f = $findings[$k]
  [pscustomobject]@{
    Tool = $f.Tool; Runs = ($f.Runs -join ','); Check = $f.Check; File = $f.File
    Line = $f.Line; Count = $f.Count; Message = $f.Message
    Bulk = ($BulkChecks -contains $f.Check)
  }
}
$all = @($all | Sort-Object File, Line, Tool, Check)

# --- findings.csv
$csvPath = Join-Path $AnalysisDir 'findings.csv'
$all | Select-Object Tool, Runs, Check, File, Line, Count, Bulk, Message |
  ConvertTo-Csv -Delimiter ';' -NoTypeInformation |
  ForEach-Object -Begin { $sb = New-Object Text.StringBuilder } -Process { [void]$sb.AppendLine($_) } -End {
    [IO.File]::WriteAllText($csvPath, $sb.ToString(), (New-Object Text.UTF8Encoding $true)) }

# --- findings.md
function Esc([string] $s) { return ($s -replace '\|', '\|' -replace '\r?\n', ' ') }
$md = New-Object Text.StringBuilder
$commit = (& git -C $Root rev-parse --short HEAD 2>$null)
[void]$md.AppendLine('# Findings of the analysis runs (merged)')
[void]$md.AppendLine()
[void]$md.AppendLine("Generated by ``scripts/analysis/merge-findings.ps1`` on $(Get-Date -Format 'yyyy-MM-dd HH:mm') (repository at ``$commit``) from ``$AnalysisDir``.")
[void]$md.AppendLine('One finding = tool + check + file + line. "Runs" lists the runs that reported it, "n" how often it')
[void]$md.AppendLine('appeared in total (headers count once per including file). All findings are in `findings.csv`.')
[void]$md.AppendLine()
$own = @($all | Where-Object { $_.File -like 'src/*' })
$ext = @($all | Where-Object { $_.File -notlike 'src/*' })
[void]$md.AppendLine("Findings in ``src/``: **$($own.Count)**, of which $(@($own | Where-Object Bulk).Count) in bulk checks. Outside ``src/``: $($ext.Count).")
[void]$md.AppendLine()
[void]$md.AppendLine('## Per tool and check')
[void]$md.AppendLine()
[void]$md.AppendLine('| Tool | Check | Findings | Bulk |')
[void]$md.AppendLine('|---|---|---:|---|')
foreach ($g in ($own | Group-Object Tool, Check | Sort-Object @{e = { $_.Group[0].Tool } }, @{e = 'Count'; Descending = $true })) {
  $b = if ($g.Group[0].Bulk) { 'yes' } else { '' }
  [void]$md.AppendLine("| $($g.Group[0].Tool) | $(Esc $g.Group[0].Check) | $($g.Count) | $b |")
}
[void]$md.AppendLine()
[void]$md.AppendLine('## Findings by file (without bulk checks)')
[void]$md.AppendLine()
[void]$md.AppendLine('Several tools at the same line: the rows follow each other.')
foreach ($g in ($own | Where-Object { -not $_.Bulk } | Group-Object File | Sort-Object Name)) {
  [void]$md.AppendLine()
  [void]$md.AppendLine("### $($g.Name)")
  [void]$md.AppendLine()
  [void]$md.AppendLine('| Line | Tool | Runs | Check | n | Message |')
  [void]$md.AppendLine('|---:|---|---|---|---:|---|')
  foreach ($f in $g.Group) {
    [void]$md.AppendLine("| $($f.Line) | $($f.Tool) | $($f.Runs) | $(Esc $f.Check) | $($f.Count) | $(Esc $f.Message) |")
  }
}
if ($ext.Count) {
  [void]$md.AppendLine()
  [void]$md.AppendLine('## Outside src/')
  [void]$md.AppendLine()
  [void]$md.AppendLine('| File | Line | Tool | Runs | Check | n | Message |')
  [void]$md.AppendLine('|---|---:|---|---|---|---:|---|')
  foreach ($f in $ext) {
    [void]$md.AppendLine("| $(Esc $f.File) | $($f.Line) | $($f.Tool) | $($f.Runs) | $(Esc $f.Check) | $($f.Count) | $(Esc $f.Message) |")
  }
}
$mdPath = Join-Path $AnalysisDir 'findings.md'
[IO.File]::WriteAllText($mdPath, $md.ToString(), $Utf8)

Write-Host "Findings: $($all.Count) ($($own.Count) in src/, $(@($own | Where-Object { -not $_.Bulk }).Count) without bulk checks)"
Write-Host "Written: $csvPath"
Write-Host "Written: $mdPath"
