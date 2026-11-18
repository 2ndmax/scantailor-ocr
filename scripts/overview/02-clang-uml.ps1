<#
.SYNOPSIS
  Overview run 02: clang-uml draws the class and sequence diagrams of clang-uml.yml.

.DESCRIPTION
  clang-uml reads the code with the Clang compiler, so it needs a compile database and the
  headers the build generates (Qt's moc and uic). Without them in the build directory, the run
  first configures and builds it, like the analysis runs (a full build, the first time only).

  The diagrams are defined in clang-uml.yml in this folder. They are written as Mermaid to
  <OutDir>\clang-uml\<diagram>.mmd, and all of them together to <OutDir>\clang-uml\diagrams.html,
  which shows them in a browser (it loads Mermaid from the internet), and to diagrams.md, which
  GitHub and editors that draw Mermaid show. The log goes to <OutDir>\02-clang-uml.log.

    powershell -ExecutionPolicy Bypass -File scripts\overview\02-clang-uml.ps1

.PARAMETER Diagram
  Draw only these diagrams (names from clang-uml.yml). Default: all.

.PARAMETER OutDir
  Where the log and the diagrams go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory with compile_commands.json. Default: build-overview in the repository; the
  one of analysis run 01 (build-analysis) can be used as well.

.PARAMETER VcpkgRoot
  vcpkg, for the build. Default: the variable VCPKG_ROOT.

.PARAMETER Yes
  Start without asking.
#>
param(
    [string[]]$Diagram = @(),
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-overview" }
$clangUml = Find-ClangUml
if (-not $clangUml) { throw 'clang-uml not found: scripts\setup\05-setup-overview.ps1 installs it (step 3).' }
# "-Diagram a,b" arrives as one string when the script is started with -File.
$Diagram = @($Diagram | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$config = "$PSScriptRoot\clang-uml.yml"
# The version of Mermaid that diagrams.html loads; tested with the output of clang-uml 0.6.3.
$MermaidVersion = '12.3.0'
$log = "$OutDir\02-clang-uml.log"
$diagramDir = "$OutDir\clang-uml"

# A build directory without the compile database can't be used, and is not deleted here.
$needsBuild = -not (Test-Path "$BuildDir\compile_commands.json")
if ($needsBuild -and (Test-Path $BuildDir)) {
    throw "$BuildDir has no compile_commands.json: remove it, or pass another -BuildDir."
}
if ($needsBuild) {
    if (-not $Vs) { throw 'No Visual Studio installation with the C++ tools found: scripts\setup\01-setup-windows.ps1 installs it.' }
    $VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
}
$buildLine = if ($needsBuild) { "First configures and builds $BuildDir (a full build)." } else { "Uses the build in $BuildDir." }
$which = if ($Diagram.Count) { $Diagram -join ', ' } else { 'all diagrams' }
if (-not (Confirm-Run 'Overview run 02: clang-uml' @(
    "clang-uml draws $which of $config."
    $buildLine
) $log)) { Write-Host 'Not started.'; exit 1 }

# Not inside the string below: Windows PowerShell garbles the escaped quotes of a string nested in
# $() of another string.
$version = cmd /c "`"$clangUml`" --version 2>nul" | Select-Object -First 1
New-Log $log '02: clang-uml, Mermaid' @(
    "# clang-uml: $clangUml ($version)"
    "# Configuration: $config"
    "# Build directory: $BuildDir"
)
$rc = 0
if ($needsBuild) { $rc = Invoke-Build $log $BuildDir (Get-ConfigureArgs $BuildDir $VcpkgRoot) }
if ($rc -eq 0) {
    # Diagrams of an earlier run that are no longer drawn would otherwise stay.
    if (Test-Path $diagramDir) {
        Remove-Item "$diagramDir\*.mmd", "$diagramDir\diagrams.md", "$diagramDir\diagrams.html" -ErrorAction SilentlyContinue
    }
    $names = ($Diagram | ForEach-Object { " -n $_" }) -join ''
    # From the repository: clang-uml asks git for the commit, and complains outside of it.
    $draw = "cd /d `"$Root`" && `"$clangUml`" -c `"$config`" -d `"$BuildDir`" -o `"$diagramDir`" -g mermaid$names"
    Add-LogLine $log "`r`n# Command: $draw`r`n"
    $rc = Invoke-Streamed $draw $log 'Drawing the diagrams'
}
if ($rc -eq 0) {
    # clang-uml ends with exit code 0 also when it could not draw a diagram (or knows no diagram of
    # that name), so each one is checked here. "-l" lists them as "  - <name> [<type>]".
    $wanted = if ($Diagram.Count) { $Diagram } else {
        @(cmd /c "`"$clangUml`" -c `"$config`" -l 2>nul" | Where-Object { $_ -match '^\s+- (\S+) \[' } | ForEach-Object { $Matches[1] })
    }
    $missing = @($wanted | Where-Object { -not (Test-Path "$diagramDir\$_.mmd") })
    if (-not $wanted.Count) {
        $rc = 1
        Write-Host "clang-uml found no diagrams in $config." -ForegroundColor Red
    } elseif ($missing.Count) {
        $rc = 1
        Write-Host "Not drawn: $($missing -join ', '); the reason is in $log" -ForegroundColor Red
        Add-LogLine $log "`r`n# Not drawn: $($missing -join ', ')"
    }
}
if ($rc -eq 0) {
    # All diagrams in one Markdown file, each in a mermaid block.
    $lines = @('# Diagrams of clang-uml', '', "Drawn from ``scripts/overview/clang-uml.yml``, $(Get-Stamp).", '')
    foreach ($file in Get-ChildItem "$diagramDir\*.mmd" | Sort-Object Name) {
        $lines += @("## $($file.BaseName)", '', '```mermaid') + @(Get-Content $file.FullName -Encoding UTF8) + @('```', '')
    }
    [IO.File]::WriteAllLines("$diagramDir\diagrams.md", $lines, $Utf8)

    # The same as a web page that draws them in any browser. Mermaid is loaded from a CDN, so the
    # page needs the internet when it is opened. The diagram text goes in HTML-encoded: Mermaid reads
    # the decoded text, so the entities clang-uml writes itself (&lt; in templates) stay intact.
    $sections = foreach ($file in Get-ChildItem "$diagramDir\*.mmd" | Sort-Object Name) {
        $text = [Net.WebUtility]::HtmlEncode([IO.File]::ReadAllText($file.FullName, $Utf8))
        "<h2>$($file.BaseName)</h2>`n<div class=`"diagram`"><pre class=`"mermaid`">`n$text</pre></div>"
    }
    $html = @(
        '<!doctype html>'
        '<html lang="en"><head><meta charset="utf-8"><title>clang-uml diagrams</title>'
        # At most as high as the window, so its scroll bars are on screen while the diagram is.
        '<style>body { font-family: sans-serif; margin: 1em 2em; }'
        '.diagram { overflow: auto; max-height: calc(100vh - 4em); border: 1px solid #ccc; cursor: grab; }'
        '.diagram.moving { cursor: grabbing; user-select: none; }</style>'
        '</head><body>'
        '<h1>Diagrams of clang-uml</h1>'
        "<p>Drawn from scripts/overview/clang-uml.yml, $(Get-Stamp).</p>"
    ) + $sections + @(
        '<script type="module">'
        "import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@$MermaidVersion/dist/mermaid.esm.min.mjs';"
        'mermaid.initialize({ startOnLoad: false, maxTextSize: 1000000 });'
        'await mermaid.run();'
        # Mermaid shrinks a diagram to the window, which makes the text of a large one unreadable. Its
        # full width is the max-width Mermaid sets; the diagram is then scrolled sideways. A class
        # diagram starts in the middle (its base class is at the top centre), a sequence diagram left.
        'for (const svg of document.querySelectorAll(".diagram svg")) {'
        '  if (svg.style.maxWidth) { svg.style.width = svg.style.maxWidth; svg.style.maxWidth = "none"; }'
        '  const box = svg.closest(".diagram");'
        '  if (svg.getAttribute("aria-roledescription") === "classDiagram") { box.scrollLeft = (box.scrollWidth - box.clientWidth) / 2; }'
        '}'
        # Dragging with the mouse moves a diagram in every direction, like a map.
        'for (const box of document.querySelectorAll(".diagram")) {'
        '  let start = null;'
        '  box.addEventListener("pointerdown", e => {'
        '    if (e.button !== 0) return;'
        '    start = { x: e.clientX, y: e.clientY, left: box.scrollLeft, top: box.scrollTop };'
        '    box.setPointerCapture(e.pointerId); box.classList.add("moving");'
        '  });'
        '  box.addEventListener("pointermove", e => {'
        '    if (!start) return;'
        '    box.scrollLeft = start.left - (e.clientX - start.x); box.scrollTop = start.top - (e.clientY - start.y);'
        '  });'
        '  const stop = () => { start = null; box.classList.remove("moving"); };'
        '  box.addEventListener("pointerup", stop); box.addEventListener("pointercancel", stop);'
        '}'
        '</script>'
        '</body></html>'
    )
    [IO.File]::WriteAllLines("$diagramDir\diagrams.html", $html, $Utf8)
}
Add-LogLine $log "`r`n# Exit code: $rc"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd $rc @("Log: $log", "Diagrams: $diagramDir\diagrams.html (browser), diagrams.md")
exit $rc
