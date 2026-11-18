<#
.SYNOPSIS
  Helpers for the overview runs in this folder. Dot-sourced by the run scripts.

.DESCRIPTION
  Loads the shared helpers of ..\..\support\common.ps1 (Visual Studio, vcpkg, logging, configure
  and build, finding graphify and clang-uml) and sets where the runs put their results.
#>

. "$PSScriptRoot\..\..\support\common.ps1"
# Logs and diagrams go here unless a script is given -OutDir (ignored by git).
$ResultsDir = "$(Split-Path $PSScriptRoot)\results"
# The -Yes of the run scripts: start without asking.
$Unattended = [bool]$Yes
$AssumeYes = [bool]$Yes

# Writes a web page that draws diagrams in the browser. $Diagrams: hashtables with Name and Text
# (the source of the diagram) and Kind (the class of its <pre>, which $Scripts look for). $Scripts:
# the page's last lines: they load the drawing library, replace the text of each <pre> by its
# drawing and then call fitDiagrams(). The text goes in HTML-encoded; the libraries read the
# decoded text, so entities in the source (&lt; that clang-uml writes in templates) stay intact.
function Write-DiagramPage([string]$Path, [string]$Title, [string]$Intro, [object[]]$Diagrams, [string[]]$Scripts) {
    $sections = foreach ($diagram in $Diagrams) {
        $text = [Net.WebUtility]::HtmlEncode($diagram.Text)
        "<h2>$($diagram.Name)</h2>`n<div class=`"diagram`"><pre class=`"$($diagram.Kind)`">`n$text</pre></div>"
    }
    $html = @(
        '<!doctype html>'
        "<html lang=`"en`"><head><meta charset=`"utf-8`"><title>$Title</title>"
        # At most as high as the window, so its scroll bars are on screen while the diagram is.
        '<style>body { font-family: sans-serif; margin: 1em 2em; }'
        '.diagram { overflow: auto; max-height: calc(100vh - 4em); border: 1px solid #ccc; cursor: grab; }'
        '.diagram.moving { cursor: grabbing; user-select: none; }</style>'
        '</head><body>'
        "<h1>$Title</h1>"
        "<p>$Intro</p>"
    ) + $sections + @(
        '<script>'
        # A library may shrink a diagram to the window (Mermaid does), which makes the text of a large
        # one unreadable. Its full width is the max-width it sets; the diagram is then scrolled
        # sideways, from the middle (where a class diagram or a graph starts), a sequence diagram from
        # the left.
        'function fitDiagrams() {'
        '  for (const svg of document.querySelectorAll(".diagram svg")) {'
        '    if (svg.style.maxWidth) { svg.style.width = svg.style.maxWidth; svg.style.maxWidth = "none"; }'
        '    const box = svg.closest(".diagram");'
        '    if (svg.getAttribute("aria-roledescription") !== "sequence") { box.scrollLeft = (box.scrollWidth - box.clientWidth) / 2; }'
        '  }'
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
    ) + $Scripts + @('</body></html>')
    [IO.File]::WriteAllLines($Path, $html, $Utf8)
}
