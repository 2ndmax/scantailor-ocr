<#
.SYNOPSIS
  Overview run 01: graphify maps the code of the repository as a graph.

.DESCRIPTION
  graphify parses the code (C++, CMake, PowerShell, ...) with tree-sitter and builds a graph of
  files, classes, functions and how they use each other, then groups it into communities. Only
  the code is read, with no language model (--code-only, --no-label): nothing leaves the machine.
  Folders ignored by git (the build directories among them) are left out.

  Each community is named after the folder most of its code is in and its most connected node,
  e.g. "core/filters/deskew/ui . OptionsWidget" (support\graphify-labels.py). graphify alone would
  name it after that node only, which is often a standard header ("vector") or a class name
  several steps share ("OptionsWidget").

  The graph goes to graphify-out in the repository (ignored by git), where the /graphify skill of
  Claude Code finds it (setup\05-setup-overview.ps1, step 4):
    graph.json                the graph; graphify query/explain/path read it
    graph.html                the graph in the browser
    GRAPH_REPORT.md           the most connected nodes and the communities
    COMMUNITIES_BY_FOLDER.md  for each folder the communities its code is in
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
# graphify's own Python: it runs support\graphify-labels.py, and the /graphify skill reads its path
# from graphify-out\.graphify_python.
$python = "$env:UV_TOOL_DIR\graphifyy\Scripts\python.exe"
if (-not ($env:UV_TOOL_DIR -and (Test-Path $python))) {
    throw "graphify's Python not found in UV_TOOL_DIR: scripts\setup\05-setup-overview.ps1 installs it (steps 2 and 3)."
}
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
# graphify copies graph.json (some 20 MB) into a dated folder in graphify-out before each run that
# overwrites named communities. These names come from the code alone and the next run writes them
# again, so there is nothing to keep.
$env:GRAPHIFY_NO_BACKUP = '1'
if ($Update) {
    # --force: the graph may shrink, as it does when code is deleted.
    $extract = "`"$graphify`" update `"$Root`" --force"
} else {
    # graphify merges a new extraction into an existing graph (and refuses when that shrinks it), so
    # the graph, its report and the names of its communities go first. Kept: the cache of the parsed
    # files (cache\) and the answers the /graphify skill saved (memory\).
    $old = @('graph.json', 'graph.html', 'GRAPH_REPORT.md', 'COMMUNITIES_BY_FOLDER.md', 'manifest.json', '.graphify_analysis.json', '.graphify_labels*')
    $old | ForEach-Object { Remove-Item "$graphDir\$_" -Force -ErrorAction SilentlyContinue }
    $extract = "`"$graphify`" extract `"$Root`" --code-only"
}
Add-LogLine $log "# Command: $extract`r`n"
$rc = Invoke-Streamed $extract $log 'Reading the code'
# --no-label: no names from a language model. --no-viz: graph.html follows with the names below.
$cluster = "`"$graphify`" cluster-only `"$Root`" --no-label --no-viz"
$name = "`"$python`" `"$PSScriptRoot\support\graphify-labels.py`" `"$graphDir`""
# The same clustering again (it is the same on every run), now with the names of the step before:
# graphify keeps saved names for communities whose members are unchanged and writes them into
# graph.json, GRAPH_REPORT.md and graph.html.
$report = "`"$graphify`" cluster-only `"$Root`" --no-label"
foreach ($step in @(@($cluster, 'Grouping the graph'), @($name, 'Naming the communities'), @($report, 'Writing the report'))) {
    if ($rc -ne 0) { break }
    Add-LogLine $log "`r`n# Command: $($step[0])`r`n"
    $rc = Invoke-Streamed $step[0] $log $step[1]
}
if ($rc -eq 0) {
    [IO.File]::WriteAllText("$graphDir\.graphify_python", $python, $Utf8)
}
Add-LogLine $log "`r`n# Exit code: $rc"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log", "Graph: $graphDir\graph.html", "Report: $graphDir\GRAPH_REPORT.md",
                   "Communities by folder: $graphDir\COMMUNITIES_BY_FOLDER.md")
exit $rc
