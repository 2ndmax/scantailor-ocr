<#
.SYNOPSIS
  Overview run 03: cmake --graphviz draws which targets of the build use which others.

.DESCRIPTION
  CMake writes the targets of the build (programs, libraries, tests) and the libraries they link
  as graphs in the DOT language of Graphviz. Three graphs can be drawn (-Graph):
    Program  the program and the command line tool with the libraries of the project they use
    Targets  all targets of the project, with the tests and the developer tools
    All      all targets and the libraries they link from outside (Qt, Boost, Tesseract, Windows)

  They go to <OutDir>\cmake-graphviz\<graph>.dot and, all of them, to graphs.html there, which
  draws them in any browser (it loads Graphviz as viz.js from the internet). With Graphviz
  installed (setup\05-setup-overview.ps1, step 6) each is also drawn to <graph>.svg. The log goes
  to <OutDir>\03-cmake-graphviz.log.

  CMake only needs a configured build directory, not a built one: the run uses build-overview of
  run 02 if it is there, else it only configures build-graphviz.

    powershell -ExecutionPolicy Bypass -File scripts\overview\03-cmake-graphviz.ps1

.PARAMETER Graph
  Draw only these graphs: Program, Targets, All. Default: all three.

.PARAMETER OutDir
  Where the log and the graphs go. Default: results in this folder.

.PARAMETER BuildDir
  A configured build directory. Default: build-overview in the repository if it is configured,
  else build-graphviz (configured, not built, by this run).

.PARAMETER VcpkgRoot
  vcpkg, for configuring. Default: the variable VCPKG_ROOT.

.PARAMETER Yes
  Start without asking.
#>
param(
    [string[]]$Graph = @(),
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $Vs) { throw 'No Visual Studio installation with the C++ tools found: scripts\setup\01-setup-windows.ps1 installs it.' }
# The version of viz.js (Graphviz for the browser) that graphs.html loads.
$VizVersion = '3.31.0'
$log = "$OutDir\03-cmake-graphviz.log"
$graphDir = "$OutDir\cmake-graphviz"

# The graphs and the settings of CMake for each (see the CMake documentation of --graphviz).
$graphs = [ordered]@{
    Program = @{ Title = 'the program and the command line tool with the libraries of the project they use'
                 ExternalLibs = 'FALSE'
                 # Tests end in _tests, _test or _link_check; the developer tools (BUILD_DEV_TOOLS) are *_compare and color_detect.
                 Ignore = '_tests$;_compare$;^color_detect$;_link_check$;_test$' }
    Targets = @{ Title = 'all targets of the project, with the tests and the developer tools'
                 ExternalLibs = 'FALSE'; Ignore = '' }
    All = @{ Title = 'all targets and the libraries they link from outside'
             ExternalLibs = 'TRUE'; Ignore = '' }
}
# "-Graph a,b" arrives as one string when the script is started with -File.
$Graph = @($Graph | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Graph.Count) { $Graph = @($graphs.Keys) }
foreach ($name in $Graph) {
    if (-not $graphs.Contains($name)) { throw "No graph '$name'; there are: $($graphs.Keys -join ', ')." }
}

if (-not $BuildDir) {
    $BuildDir = if (Test-Path "$Root\build-overview\CMakeCache.txt") { "$Root\build-overview" } else { "$Root\build-graphviz" }
}
$needsConfigure = -not (Test-Path "$BuildDir\CMakeCache.txt")
if ($needsConfigure -and (Test-Path $BuildDir)) {
    throw "$BuildDir is not a configured build directory: remove it, or pass another -BuildDir."
}
if ($needsConfigure) { $VcpkgRoot = Get-VcpkgRoot $VcpkgRoot }
# CMake reads its settings for --graphviz from this file in the build directory; the run writes it
# for each graph and removes it at the end, so one already there is not the run's.
$options = "$BuildDir\CMakeGraphVizOptions.cmake"
if (Test-Path $options) { throw "$options is there already; move it away, as this run writes its own." }
$graphviz = Find-Graphviz

$buildLine = if ($needsConfigure) { "First configures $BuildDir (no build)." } else { "Uses the build directory $BuildDir." }
$svgLine = if ($graphviz) { "Graphviz draws them as SVG; graphs.html shows them in the browser." }
           else { 'Graphviz is not installed, so no SVG files; graphs.html shows them in the browser.' }
if (-not (Confirm-Run 'Overview run 03: cmake --graphviz' @(
    "CMake draws the graphs $($Graph -join ', ') of the targets of the build."
    $buildLine
    $svgLine
) $log)) { Write-Host 'Not started.'; exit 1 }

# Not inside the string below: Windows PowerShell garbles the escaped quotes of a string nested in
# $() of another string.
$dotVersion = if ($graphviz) { cmd /c "`"$graphviz`" -V 2>&1" | Select-Object -First 1 } else { 'not installed' }
New-Log $log '03: cmake --graphviz' (@(
    "# Build directory: $BuildDir"
    "# Graphs: $($Graph -join ', ')"
    "# Graphviz: $graphviz ($dotVersion)"
) + (Get-ToolVersions))

$rc = 0
if ($needsConfigure) {
    $configure = "cmake $(Get-ConfigureArgs $BuildDir $VcpkgRoot)"
    Add-LogLine $log "# Configure: $configure`r`n"
    $rc = Invoke-VsLogged $configure $log 'Configuring with CMake'
}
if ($rc -eq 0) {
    New-Item -ItemType Directory -Force $graphDir | Out-Null
    Remove-Item "$graphDir\*.dot", "$graphDir\*.svg", "$graphDir\graphs.html" -ErrorAction SilentlyContinue
    try {
        foreach ($name in $Graph) {
            $g = $graphs[$name]
            [IO.File]::WriteAllLines($options, @(
                "set(GRAPHVIZ_GRAPH_NAME `"ScanTailor OCR: $($g.Title)`")"
                'set(GRAPHVIZ_GENERATE_PER_TARGET FALSE)'
                'set(GRAPHVIZ_GENERATE_DEPENDERS FALSE)'
                "set(GRAPHVIZ_EXTERNAL_LIBS $($g.ExternalLibs))"
                "set(GRAPHVIZ_IGNORE_TARGETS `"$($g.Ignore)`")"
            ), $Utf8)
            # CMake configures the build directory again and writes the graph while it does.
            $cmd = "cmake `"--graphviz=$graphDir\$name.dot`" `"$BuildDir`""
            Add-LogLine $log "`r`n# Graph $name`: $cmd`r`n"
            $rc = Invoke-VsLogged $cmd $log "Graph $name"
            if ($rc -ne 0) { break }
            if ($graphviz) {
                $svg = "`"$graphviz`" -Tsvg `"$graphDir\$name.dot`" -o `"$graphDir\$name.svg`""
                Add-LogLine $log "`r`n# SVG: $svg`r`n"
                $rc = Invoke-Streamed $svg $log "Drawing $name"
                if ($rc -ne 0) { break }
            }
        }
    } finally {
        Remove-Item $options -ErrorAction SilentlyContinue
    }
}
if ($rc -eq 0) {
    $diagrams = foreach ($name in $Graph) {
        @{ Name = $name; Text = [IO.File]::ReadAllText("$graphDir\$name.dot", $Utf8); Kind = 'graphviz' }
    }
    Write-DiagramPage "$graphDir\graphs.html" 'Build targets of CMake' `
        "Drawn by cmake --graphviz from $BuildDir, $(Get-Stamp). Shapes: see the legend in each graph." $diagrams @(
        "<script src=`"https://cdn.jsdelivr.net/npm/@viz-js/viz@$VizVersion/dist/viz-global.js`"></script>"
        '<script>'
        'Viz.instance().then(viz => {'
        '  for (const pre of document.querySelectorAll("pre.graphviz")) {'
        '    try { pre.replaceWith(viz.renderSVGElement(pre.textContent)); }'
        '    catch (e) { pre.insertAdjacentText("beforebegin", "Not drawn: " + e); }'
        '  }'
        '  fitDiagrams();'
        '});'
        '</script>'
    )
}
Add-LogLine $log "`r`n# Exit code: $rc"
Add-LogLine $log "# End: $(Get-Stamp)"
$results = @("Log: $log", "Graphs: $graphDir\graphs.html (browser)")
if ($graphviz) { $results += "SVG files: $graphDir\<graph>.svg" }
Write-RunEnd $rc $results
exit $rc
