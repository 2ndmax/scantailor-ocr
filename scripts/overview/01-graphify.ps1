<#
.SYNOPSIS
  Overview run 01: graphify maps the code of the repository as a graph.

.DESCRIPTION
  graphify parses the code (C++, CMake, PowerShell, ...) with tree-sitter and builds a graph of
  files, classes, functions and how they use each other, then groups it into communities. Only
  the code is read, with no language model (--code-only, --no-label): nothing leaves the machine.
  Folders ignored by git (the build directories among them) are left out.

  The graph goes to graphify-out in the repository (ignored by git), where the /graphify skill of
  Claude Code finds it (setup\05-setup-overview.ps1, step 5):
    graph.json       the graph; graphify query/explain/path read it
    graph.html       the graph in the browser
    GRAPH_REPORT.md  the most connected nodes and the communities
  Without -Update the graph is built anew: the graph of an earlier run, its report and the names
  of its communities are deleted first; the cache of parsed files and the answers saved by the
  /graphify skill (graphify-out\memory) are kept. The log goes to <OutDir>\01-graphify.log.

    powershell -ExecutionPolicy Bypass -File scripts\overview\01-graphify.ps1

.PARAMETER Update
  Read only the files changed since the last run and update the graph (much quicker).

.PARAMETER OutDir
  Where the log goes. Default: results in this folder.

.PARAMETER Yes
  Start without asking.
#>
param(
    [switch]$Update,
    [string]$OutDir = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
$graphify = Find-Graphify
if (-not $graphify) { throw 'graphify not found: scripts\setup\05-setup-overview.ps1 installs it (step 3).' }
$log = "$OutDir\01-graphify.log"
$graphDir = "$Root\graphify-out"

$what = if ($Update) { "the files changed since the last run and updates the graph in $graphDir" }
        else { "all code files and builds the graph in $graphDir anew" }
if (-not (Confirm-Run 'Overview run 01: graphify' @(
    "graphify reads $what."
    'Only the code is read; no language model is used.'
) $log)) { Write-Host 'Not started.'; exit 1 }

# Not inside the string below: Windows PowerShell garbles the escaped quotes of a string nested in
# $() of another string.
$version = cmd /c "`"$graphify`" --version 2>nul" | Select-Object -First 1
New-Log $log '01: graphify, code only' @(
    "# graphify: $graphify ($version)"
    "# Graph: $graphDir"
)
# graphify is a Python program: without this it writes its output in the code page of Windows.
$env:PYTHONUTF8 = '1'
if ($Update) {
    # --force: the graph may shrink, as it does when code is deleted.
    $extract = "`"$graphify`" update `"$Root`" --force"
} else {
    # graphify merges a new extraction into an existing graph (and refuses when that shrinks it), so
    # the graph, its report and the names of its communities go first. Kept: the cache of the parsed
    # files (cache\) and the answers the /graphify skill saved (memory\).
    $old = @('graph.json', 'graph.html', 'GRAPH_REPORT.md', 'manifest.json', '.graphify_analysis.json', '.graphify_labels*')
    $old | ForEach-Object { Remove-Item "$graphDir\$_" -Force -ErrorAction SilentlyContinue }
    $extract = "`"$graphify`" extract `"$Root`" --code-only"
}
Add-LogLine $log "# Command: $extract`r`n"
$rc = Invoke-Streamed $extract $log 'Reading the code'
if ($rc -eq 0) {
    # Communities and report; --no-label: the communities keep numbers instead of names from a model.
    $cluster = "`"$graphify`" cluster-only `"$Root`" --no-label"
    Add-LogLine $log "`r`n# Command: $cluster`r`n"
    $rc = Invoke-Streamed $cluster $log 'Grouping the graph'
}
# The /graphify skill runs graphify's Python from the path in this file.
$python = "$env:UV_TOOL_DIR\graphifyy\Scripts\python.exe"
if ($rc -eq 0 -and $env:UV_TOOL_DIR -and (Test-Path $python)) {
    [IO.File]::WriteAllText("$graphDir\.graphify_python", $python, $Utf8)
}
Add-LogLine $log "`r`n# Exit code: $rc"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log", "Graph: $graphDir\graph.html", "Report: $graphDir\GRAPH_REPORT.md")
exit $rc
