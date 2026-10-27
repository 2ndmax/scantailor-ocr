<#
.SYNOPSIS
  Run 03: cppcheck over all sources in src/ except the tests.

.DESCRIPTION
  Uses the compile database of run 01 (01-msvc-analyze.ps1), filtered like run 02. The include
  paths of the vcpkg dependencies are removed: cppcheck would otherwise analyse all of Boost and
  Qt. Their APIs are described to cppcheck by --library=qt,boost instead.
  Severities: error (always on), warning, performance, portability. No style, no
  unusedFunction (many false alarms with Qt slots), no --inconclusive.

  The results are in <OutDir>\03-cppcheck.xml; the log only holds the progress output.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\03-cppcheck.ps1

.PARAMETER OutDir
  Where the log and the XML go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory of run 01. Default: build-analysis in the repository.

.PARAMETER Cppcheck
  The cppcheck executable. Default: cppcheck on PATH, else C:\Program Files\Cppcheck.

.PARAMETER Only
  Check just this one source file (a trial run, no log).
#>
param(
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$Cppcheck = '',
    [string]$Only = ''
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-analysis" }
if (-not $Cppcheck) { $Cppcheck = Find-Cppcheck }
if (-not $Cppcheck) {
    throw 'cppcheck not found: scripts\setup\2-setup-analysis.ps1 installs it, or pass -Cppcheck <exe>.'
}
$dbDir = "$BuildDir\cppcheck-db"
$cache = "$BuildDir\cppcheck-build"
$log = "$OutDir\03-cppcheck.log"
$xml = "$OutDir\03-cppcheck.xml"

$vcpkgInclude = '\s(-I|-external:I|/external:I)\s*' + [regex]::Escape((Get-VcpkgRoot '')) + '\S*'
$sel = Get-FilteredCompileDb $BuildDir @('\s/analyze\S*', $vcpkgInclude)
if ($Only) {
    $o = $Only -replace '\\', '/'
    $sel = @($sel | Where-Object { ($_.file -replace '\\', '/') -eq $o })
}
Save-CompileDb $sel $dbDir
New-Item -ItemType Directory -Force $cache, $OutDir | Out-Null

$cargs = @(
    "--project=$dbDir\compile_commands.json"
    '--enable=warning,performance,portability'
    '--check-level=exhaustive'
    '--library=qt'
    '--library=boost'
    '--platform=win64'
    '--inline-suppr'
    "--cppcheck-build-dir=$cache"
    '-j', "$([Environment]::ProcessorCount)"
    '--xml'
    "--output-file=$xml"
)
if ($Only) {
    & $Cppcheck @cargs
    exit $LASTEXITCODE
}

New-Log $log '03: cppcheck, severities error + warning, performance, portability' @(
    "# Files: $($sel.Count) translation units under src\ (without tests and generated files)"
    "# Results (machine-readable): $xml; this log only holds the progress output"
    "# $(& $Cppcheck --version)"
    "# Command: `"$Cppcheck`" $($cargs -join ' ')"
)
$quoted = ($cargs | ForEach-Object { if ($_ -match ' ') { "`"$_`"" } else { $_ } }) -join ' '
cmd /c "`"$Cppcheck`" $quoted >> `"$log`" 2>&1"
$rc = $LASTEXITCODE
Add-LogLine $log "`r`n# cppcheck exit code: $rc"
Add-LogLine $log "# End: $(Get-Stamp)"
exit $rc
