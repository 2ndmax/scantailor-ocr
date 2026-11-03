<#
.SYNOPSIS
  Analysis run 12: the test programs (and optionally the dev tools) under Application Verifier.

.DESCRIPTION
  Builds everything with MSVC (RelWithDebInfo, so the stack traces have file names and lines) and
  runs every program with the "Basics" checks of Application Verifier switched on (heaps with full
  page heap, handles, locks, SRW locks, memory, TLS, exceptions, thread pool, leaks at DLL unload).
  Unlike the sanitizers of run 09 they also check the DLLs that are not built here (Qt, Tesseract,
  Leptonica, libtiff ...), and they find misused handles and locks, which ASan does not.

  Each program runs under the debugger cdb: a verifier stop is written to the log with its stack
  and the program goes on; any other stop of the debugger (a crash) is written with its stack and
  ends the program. The binary log of Application Verifier is exported as XML next to the logs.
    a) all test programs                                 -> 12-appverif-tests.log
    b) with -Images: the dev tools on real scans         -> 12-appverif-tools.log
    c) with -LowResource: core_tests (only the command line tool) with fault injection: memory
       allocations and file operations fail at random    -> 12-appverif-lowres.log
  12-appverif\<program>.xml: the exported logs of Application Verifier.

  Needs administrator rights: Application Verifier keeps its settings per program name in the
  registry (Image File Execution Options). The script deletes them again for every program it
  ran, also when it stops with an error; -Reset deletes them without running anything.
  The stacks of Windows' own DLLs come from Microsoft's symbol server (cached in the build
  directory), unless _NT_SYMBOL_PATH is set.

  A test program whose output holds no test summary is marked "# NOT RUN:" in the log, and
  90-merge-findings.ps1 names it in findings.md.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\12-appverifier.ps1 -Images D:\scans

.PARAMETER Images
  A folder with scans (.tif, .tiff, .jp2, top level only) for the dev tools. Without it,
  part b) is skipped.

.PARAMETER LowResource
  Also run part c), fault injection. Expect failed tests there; findings are the verifier stops
  and crashes, not the failures.

.PARAMETER Reset
  Only delete the Application Verifier settings of all programs of this run (after an
  interrupted run), then stop.

.PARAMETER SkipBuild
  Reuse an existing build directory and only run a) to c).

.PARAMETER OutDir
  Where the logs go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory. Default: build-appverif in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.

.PARAMETER Yes
  Start without asking. A build directory left from an earlier run is deleted only if it is a
  build-* folder in the project folder, as the runs name theirs; any other stops the run.
#>
param(
    [string]$Images = '',
    [switch]$LowResource,
    [switch]$Reset,
    [switch]$SkipBuild,
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-appverif" }

$appverif = Find-AppVerif
if (-not $appverif) {
    throw 'appverif.exe not found: it comes with Windows 11, otherwise with the Windows SDK ' +
          '(feature "Application Verifier For Windows").'
}
$cdb = Find-Cdb
if (-not $cdb) { throw "cdb.exe not found: scripts\setup\02-setup-analysis.ps1 (step 5) adds it." }
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw 'Application Verifier writes its settings into the registry: start this script in a ' +
          'PowerShell run as administrator.'
}

$devTools = 'binarization_compare.exe', 'color_detect.exe'
$allPrograms = @($BoostTestPrograms) + 'qt_tests.exe' + $devTools

# appverif.exe with the given arguments; its output and exit code go to $Log (if given).
function Invoke-AppVerif([string]$ArgLine, [string]$Log = '') {
    $tmp = [IO.Path]::GetTempFileName()
    $p = Start-Process -FilePath $appverif -ArgumentList $ArgLine -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $tmp
    $out = (Get-Content $tmp -Raw)
    Remove-Item $tmp -ErrorAction SilentlyContinue
    if ($Log) {
        Add-LogLine $Log "# appverif $ArgLine  (exit code $($p.ExitCode))"
        if ($out) { Add-LogLine $Log ($out.TrimEnd() -replace '(?m)^', '#   ') }
    }
    return $p.ExitCode
}

# Settings left in the registry would slow down every later start of these programs.
function Remove-VerifierSettings([string[]]$Programs) {
    foreach ($exe in $Programs) { [void](Invoke-AppVerif "-delete settings -for $exe") }
}

if ($Reset) {
    Remove-VerifierSettings $allPrograms
    Write-Host "Deleted the Application Verifier settings of: $($allPrograms -join ', ')"
    exit 0
}

$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$symbolPath = if ($env:_NT_SYMBOL_PATH) { "$env:_NT_SYMBOL_PATH;$BuildDir" } `
              else { "srv*$BuildDir\symbols*https://msdl.microsoft.com/download/symbols;$BuildDir" }
$header = @(
    "# Application Verifier: $appverif ($((Get-Item $appverif).VersionInfo.FileVersion))"
    "# Debugger: $cdb ($((Get-Item $cdb).VersionInfo.FileVersion))"
    "# Symbol path: $symbolPath"
)

$intro = @(
    'Builds everything with MSVC (RelWithDebInfo), then runs all test programs under the debugger'
    'cdb with the Basics checks of Application Verifier (full page heap, handles, locks, memory ...).'
    'The tests take much longer than without them.'
)
if ($SkipBuild) {
    if (-not (Test-Path "$BuildDir\CMakeCache.txt")) { throw "$BuildDir holds no build: run without -SkipBuild first." }
    $intro += 'With -SkipBuild the existing build is used.'
}
if ($Images) { $intro += "Afterwards the dev tools process the scans in $Images." }
else { $intro += 'The dev tools on real scans are skipped: pass -Images <folder> for them.' }
if ($LowResource) { $intro += 'Then core_tests runs the command line tool tests with fault injection.' }
else { $intro += 'Fault injection is skipped: pass -LowResource for it.' }
$intro += "Build directory: $BuildDir"
$freshDir = if ($SkipBuild) { '' } else { $BuildDir }
if (-not (Confirm-Run 'Analysis run 12: Application Verifier' $intro "$OutDir\12-appverif-*.log" $freshDir)) {
    Write-Host 'Not started.'
    exit 1
}

# --- Build ---------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    $log = "$OutDir\12-appverif-build.log"
    New-Log $log '12: MSVC build for Application Verifier (RelWithDebInfo)' (@(
        "# Compiler: $(Get-ClVersion)"
        "# Build directory: $BuildDir"
    ) + (Get-ToolVersions))
    $rc = Invoke-Build $log $BuildDir ((Get-ConfigureArgs $BuildDir $VcpkgRoot 'RelWithDebInfo') +
        ' -DENABLE_CODE_ANALYSIS=OFF')
    Add-LogLine $log "# End: $(Get-Stamp)"
    if ($rc -ne 0) { Write-RunEnd $rc @("Log: $log"); exit $rc }
}

Enable-OffscreenQt $BuildDir $VcpkgRoot
$xmlDir = "$OutDir\12-appverif"
New-Item -ItemType Directory -Force $xmlDir | Out-Null

# The debugger's commands. At the start: a verifier stop (a breakpoint) writes its stack and the
# program goes on; the end of the process writes its exit code and ends cdb. Module loads are not
# listed. Any other stop of the debugger (a crash, an unhandled exception) waits for input, which
# comes from $cdbOnStop: write what happened and the stack, then quit.
$cdbStart = "$BuildDir\appverif-cdb-start.txt"
$cdbOnStop = "$BuildDir\appverif-cdb-stop.txt"
[IO.File]::WriteAllLines($cdbStart, @(
    '.lines -e'
    'sxi ld'
    'sxe -c ".echo # VERIFIER BREAK;.lastevent;kn 60;.echo # VERIFIER BREAK END;g" bpe'
    'sxe -c ".echo # PROCESS EXIT;.lastevent;q" epr'
    'g'
))
[IO.File]::WriteAllLines($cdbOnStop, @(
    '.echo # DEBUGGER STOP;.lastevent;kn 60;.echo # DEBUGGER STOP END;q'
))

# Runs $Exe under cdb with the checks given to appverif ($Enable: "-verify <exe>" or "-enable ...").
# $ResultDir: where the program writes its results instead of to stdout (Qt Test with -o); they
# are taken as its output.
function Invoke-Verified([string]$Log, [string]$Exe, [string]$ArgLine, [string]$Enable, [int]$TimeoutMin,
                         [switch]$IsTest, [string]$ResultDir = '') {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "Running $Exe ... " -NoNewline
    Add-LogLine $Log "`r`n# ===== $Exe $ArgLine"
    Add-LogLine $Log "# Start: $(Get-Stamp)"
    if ($ResultDir) {
        Remove-Item -Recurse -Force $ResultDir -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force $ResultDir | Out-Null
    }
    $tmpOut = "$BuildDir\run-stdout.txt"
    $tmpErr = "$BuildDir\run-stderr.txt"
    try {
        [void](Invoke-AppVerif "-delete settings -for $Exe")
        [void](Invoke-AppVerif "-delete logs -for $Exe")
        $rc = Invoke-AppVerif $Enable $Log
        if ($rc -ne 0) { throw "appverif $Enable failed with exit code $rc; see $Log." }
        $cdbArgs = "-lines -cf `"$cdbStart`" -y `"$symbolPath`" `"$BuildDir\$Exe`" $ArgLine"
        Add-LogLine $Log "# cdb $cdbArgs"
        $sp = @{ FilePath = $cdb; ArgumentList = $cdbArgs; WorkingDirectory = $BuildDir; NoNewWindow = $true
                 PassThru = $true; RedirectStandardOutput = $tmpOut; RedirectStandardError = $tmpErr
                 RedirectStandardInput = $cdbOnStop }
        $p = Start-Process @sp
        $null = $p.Handle  # Without this, Windows PowerShell 5.1 doesn't report the exit code.
        if (-not $p.WaitForExit($TimeoutMin * 60000)) {
            # Ending the debugger ends the program it debugs.
            $p.Kill()
            $p.WaitForExit()
            Add-LogLine $Log "# TIMEOUT after $TimeoutMin minutes, process killed"
        }
        $stdout = Read-Shared $tmpOut
        if ($ResultDir -and (Test-Path $ResultDir)) {
            foreach ($f in Get-ChildItem $ResultDir -File | Sort-Object Name) { $stdout += Read-Shared $f.FullName }
        }
        Add-LogLine $Log $(if ($ResultDir) { "# --- cdb and program output (and the files in $ResultDir)" } else { '# --- cdb and program output' })
        Add-LogLine $Log $stdout
        $stderr = Read-Shared $tmpErr
        if ($stderr.Trim()) { Add-LogLine $Log '# --- stderr'; Add-LogLine $Log $stderr }
        $xml = "$xmlDir\$([IO.Path]::GetFileNameWithoutExtension($Exe)).xml"
        [void](Invoke-AppVerif "-export log -for $Exe -with To=`"$xml`" Symbols=`"$symbolPath`"" $Log)
    } finally {
        [void](Invoke-AppVerif "-delete settings -for $Exe")
    }
    # The exit code of the program is in cdb's ".lastevent" line at its end, not in cdb's own.
    $exit = [regex]::Match($stdout, 'Exit process [^,]*, code (?<code>[0-9a-fA-F]+)')
    $exitText = if ($exit.Success) { $exit.Groups['code'].Value } else { '(none: did not exit by itself)' }
    Add-LogLine $Log "# Exit code: $exitText"
    # Boost.Test writes its summary to stderr.
    $ran = (-not $IsTest) -or (Test-TestSummary "$stdout`n$stderr")
    if (-not $ran) { Add-LogLine $Log "# NOT RUN: $Exe printed no test summary; it stopped before or during its tests." }
    Add-LogLine $Log "# End: $(Get-Stamp)"
    $stops = ([regex]::Matches($stdout, '(?m)^# VERIFIER BREAK$')).Count
    $crashes = ([regex]::Matches($stdout, '(?m)^# DEBUGGER STOP$')).Count
    Write-Host ("exit code {0}, {1} verifier stops, {2} other stops, {3:hh\:mm\:ss}" -f $exitText, $stops, $crashes, $clock.Elapsed)
    if (-not $ran) { Write-Host "  $Exe ran no tests (no test summary): see the log." -ForegroundColor Red }
}

# --- a) Tests ------------------------------------------------------------------------------------
$log = "$OutDir\12-appverif-tests.log"
$logs = @($log)
New-Log $log '12: Application Verifier (Basics), all test programs under cdb (QT_QPA_PLATFORM=offscreen)' $header
foreach ($t in $BoostTestPrograms) {
    Invoke-Verified $log $t '' "-verify $t" 120 -IsTest
}
# Qt Test writes nothing to stdout when the script runs without a console window; into files it
# always does, one per test class.
$qtResults = "$BuildDir\qt-results"
Invoke-Verified $log 'qt_tests.exe' "-o `"$qtResults\qt_tests.txt,txt`"" '-verify qt_tests.exe' 120 -IsTest -ResultDir $qtResults

# --- b) Dev tools on real scans ------------------------------------------------------------------
if ($Images) {
    $log = "$OutDir\12-appverif-tools.log"
    $logs += $log
    $tools = New-DevToolInput $Images $BuildDir
    New-Log $log '12: Application Verifier (Basics), dev tools binarization_compare and color_detect on real scans' ($header + $tools.Header)
    foreach ($c in $tools.Calls) { Invoke-Verified $log $c.Exe $c.Args "-verify $($c.Exe)" 240 }
}

# --- c) Fault injection --------------------------------------------------------------------------
if ($LowResource) {
    $log = "$OutDir\12-appverif-lowres.log"
    $logs += $log
    # 2 % of the heap allocations and file operations fail, from 2 seconds after the start on.
    # Application Verifier draws them at random: two runs fail at different places.
    $lowres = '-enable lowres -for core_tests.exe -with HEAP_ALLOC=20000 FILE=20000 TimeOut=2000'
    New-Log $log '12: Application Verifier, fault injection (Low Resource Simulation), core_tests CommandLineRun' ($header + @(
        "# Checks: appverif $lowres"
    ))
    Invoke-Verified $log 'core_tests.exe' '--run_test=CommandLineRunTestSuite' $lowres 120 -IsTest
}

$results = @("Logs: $($logs[0])") + @($logs | Select-Object -Skip 1 | ForEach-Object { "      $_" }) + "XML:  $xmlDir"
Write-RunEnd 0 $results
exit 0
