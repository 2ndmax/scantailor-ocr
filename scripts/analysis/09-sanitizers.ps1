<#
.SYNOPSIS
  Run 09: UndefinedBehaviorSanitizer (UBSan) together with AddressSanitizer (ASan).

.DESCRIPTION
  Builds with clang-cl and -fsanitize=address,undefined (RelWithDebInfo, so the stack traces have
  function names), then
    a) runs all test programs                                    -> 09-ubsan-tests.log
    b) lets the two command-line dev tools process real scans    -> 09-ubsan-tools.log
  The sanitizer reports are in the stderr sections of the logs: UBSan as "runtime error: ..."
  with file:line, ASan as "ERROR: AddressSanitizer: ...", both with stack traces. The programs
  keep running after a report, so all findings of a program are collected.

  Needs the Visual Studio components "C++ Clang Compiler for Windows" and "C++ AddressSanitizer"
  (stl_asan.lib, which the MSVC standard library links under ASan).

  Why it is set up this way (each point cost a failed attempt):
  - The static UBSan runtime of clang on Windows is built for /MT and can't be linked with the
    /MD project and vcpkg libraries. The dynamic ASan runtime is /MD-compatible and contains
    UBSan, so ASan runs along; -fsanitize-recover=address keeps going after an ASan report.
  - CMake's compiler check builds with Debug (/MDd /RTC1), which ASan rejects, hence
    CMAKE_TRY_COMPILE_CONFIGURATION=RelWithDebInfo.
  - CMake links with lld-link directly, not through clang-cl, so the sanitizer runtime isn't
    added automatically. LDFLAGS holds what clang-cl itself would pass to the linker, with
    forward slashes, as CMake drops the backslashes.
  - Only the "windows" Qt platform plugin is deployed to the build directory; the programs run
    headless with "offscreen", so that plugin is copied from vcpkg.

  Known limitation: Qt6Widgets.dll allocates memory while it is loaded, before ASan has hooked
  the allocator, and ASan then reports freeing or growing that memory as an error inside Qt
  (core_tests, qt_tests) right at startup, with a stack trace only in Qt's DLL initialization.
  This one report is no finding in the project's code. qt_tests runs on after it; core_tests
  often fails to load (exit code 0xC0000142, or ASan "CHECK failed" in its stack depot), by
  chance about two starts in three. The report itself is no such sign, as every start prints it.
  A test program that printed nothing and failed with one of these signs is therefore started
  again, at most three times; the reports of the discarded starts stay in the log. A crash or a timeout is never retried, as it may be a
  finding. A test program whose output still holds no test summary is marked
  "# NOT RUN:" in the log, and 90-merge-findings.ps1 names it in findings.md.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\09-sanitizers.ps1 -Images D:\scans

.PARAMETER Images
  A folder with scans (.tif, .tiff, .jp2, top level only) for the dev tools. Without it,
  part b) is skipped.

.PARAMETER SkipBuild
  Reuse an existing build directory and only run a) and b).

.PARAMETER OutDir
  Where the logs go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory. Default: build-ubsan in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.

.PARAMETER Yes
  Start without asking. A build directory left from an earlier run is deleted only if it is a
  build-* folder in the project folder, as the runs name theirs; any other stops the run.
#>
param(
    [string]$Images = '',
    [switch]$SkipBuild,
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-ubsan" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
$clang = Get-ClangCl
# Linked by the MSVC standard library under ASan; without it the build fails only when linking.
if (-not (Test-Path "$Vs\VC\Tools\MSVC\*\lib\x64\stl_asan.lib")) {
    throw "stl_asan.lib not found: scripts\setup\02-setup-analysis.ps1 (step 1) adds the Visual Studio " +
          "component 'C++ AddressSanitizer'."
}

# The clang runtime libraries, e.g. ...\Llvm\x64\lib\clang\22\lib\windows.
$rtDir = Get-ChildItem "$LlvmBin\..\lib\clang" -Directory | Sort-Object { [int]$_.Name } |
    Select-Object -Last 1 | ForEach-Object { "$($_.FullName)\lib\windows" }
$rtDir = (Resolve-Path $rtDir).Path
$rt = $rtDir -replace '\\', '/'
$flags = '-fsanitize=address,undefined -fsanitize-recover=address'
$ldflags = "`"$rt/clang_rt.asan_dynamic-x86_64.lib`" /include:__asan_seh_interceptor " +
           "`"/wholearchive:$rt/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`""
$ubsanOptions = 'print_stacktrace=1:report_error_type=1:halt_on_error=0'
$asanOptions = 'halt_on_error=0:detect_leaks=0'
$header = @(
    "# Compiler: $((& $clang --version | Select-Object -First 1))"
    "# Sanitizer flags (CFLAGS/CXXFLAGS env): $flags"
    "# UBSAN_OPTIONS: $ubsanOptions"
    "# ASAN_OPTIONS: $asanOptions"
)

$intro = @(
    'Builds everything with clang-cl and the sanitizers ASan and UBSan, which report memory errors'
    'and undefined behaviour while the programs run, then runs all test programs with them.'
)
if ($SkipBuild) {
    if (-not (Test-Path "$BuildDir\CMakeCache.txt")) { throw "$BuildDir holds no build: run without -SkipBuild first." }
    $intro += 'With -SkipBuild the existing build is used.'
} else {
    $intro += 'The build takes longer than a normal build.'
}
if ($Images) { $intro += "Afterwards the dev tools process the scans in $Images." }
else { $intro += 'The dev tools on real scans are skipped: pass -Images <folder> for them.' }
$intro += "Build directory: $BuildDir"
$freshDir = if ($SkipBuild) { '' } else { $BuildDir }
if (-not (Confirm-Run 'Analysis run 09: sanitizers ASan and UBSan' $intro "$OutDir\09-ubsan-*.log" $freshDir)) {
    Write-Host 'Not started.'
    exit 1
}

# --- Build ---------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    $log = "$OutDir\09-ubsan-build.log"
    $env:CFLAGS = $flags
    $env:CXXFLAGS = $flags
    $env:LDFLAGS = $ldflags
    New-Log $log '09: UBSan + ASan build with clang-cl (RelWithDebInfo)' ($header + @(
        "# Build directory: $BuildDir"
        "# LDFLAGS env: $ldflags"
    ) + (Get-ToolVersions))
    $cc = $clang -replace '\\', '/'
    $rc = Invoke-Build $log $BuildDir ((Get-ConfigureArgs $BuildDir $VcpkgRoot 'RelWithDebInfo') +
        " -DCMAKE_TRY_COMPILE_CONFIGURATION=RelWithDebInfo -DENABLE_CODE_ANALYSIS=OFF " +
        "-DCMAKE_C_COMPILER=`"$cc`" -DCMAKE_CXX_COMPILER=`"$cc`"")
    Add-LogLine $log "# End: $(Get-Stamp)"
    Remove-Item Env:CFLAGS, Env:CXXFLAGS, Env:LDFLAGS
    if ($rc -ne 0) { Write-RunEnd $rc @("Log: $log"); exit $rc }
}

$env:UBSAN_OPTIONS = $ubsanOptions
$env:ASAN_OPTIONS = $asanOptions
$env:PATH = "$rtDir;$env:PATH"
Enable-OffscreenQt $BuildDir $VcpkgRoot

# $ResultDir: where the program writes its results instead of to stdout (Qt Test with -o); they
# are taken as its output.
function Invoke-Logged([string]$Log, [string]$Exe, [string]$ArgLine, [int]$TimeoutMin, [switch]$IsTest,
                       [string]$ResultDir = '') {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "Running $Exe ... " -NoNewline
    Add-LogLine $Log "`r`n# ===== $Exe $ArgLine"
    Add-LogLine $Log "# Start: $(Get-Stamp)"
    $tmpOut = "$BuildDir\run-stdout.txt"
    $tmpErr = "$BuildDir\run-stderr.txt"
    $sp = @{ FilePath = "$BuildDir\$Exe"; WorkingDirectory = $BuildDir; NoNewWindow = $true
             PassThru = $true; RedirectStandardOutput = $tmpOut; RedirectStandardError = $tmpErr }
    if ($ArgLine) { $sp.ArgumentList = $ArgLine }
    # A test program that fails while loading its DLLs (the known report in Qt6Widgets'
    # initialization, see above) has printed nothing yet. That depends on chance, so it is
    # started again, at most three times in all. Only these signs count: core_tests buffers its
    # output, so a crash during the tests leaves no output either, and it must stay a finding.
    # 0xC0000142 (-1073741502) is STATUS_DLL_INIT_FAILED. The bad-free in qarraydata.cpp is no
    # such sign: with halt_on_error=0 it is also reported by runs that go on and pass.
    $knownLoadError = 'AddressSanitizer: CHECK failed'
    $readOutput = {
        $text = Read-Shared $tmpOut
        if ($ResultDir -and (Test-Path $ResultDir)) {
            foreach ($f in Get-ChildItem $ResultDir -File | Sort-Object Name) { $text += Read-Shared $f.FullName }
        }
        $text
    }
    $maxStarts = if ($IsTest) { 3 } else { 1 }
    for ($start = 1; $true; $start++) {
        if ($ResultDir) {
            Remove-Item -Recurse -Force $ResultDir -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $ResultDir | Out-Null
        }
        $p = Start-Process @sp
        $null = $p.Handle  # Without this, Windows PowerShell 5.1 doesn't report the exit code.
        $timedOut = $false
        if (-not $p.WaitForExit($TimeoutMin * 60000)) {
            $p.Kill()
            $p.WaitForExit()
            $timedOut = $true
            Add-LogLine $Log "# TIMEOUT after $TimeoutMin minutes, process killed"
        }
        $startFailed = (& $readOutput).Trim().Length -eq 0
        if (-not $startFailed -or $timedOut -or ($start -ge $maxStarts)) { break }
        $failedStderr = Read-Shared $tmpErr
        $isLoadError = ($p.ExitCode -eq -1073741502) -or [regex]::IsMatch($failedStderr, $knownLoadError)
        if (-not $isLoadError) { break }
        Add-LogLine $Log "# Start $start of $maxStarts failed while loading (exit code $($p.ExitCode)); started again. Its stderr:"
        Add-LogLine $Log $failedStderr
    }
    $stdout = & $readOutput
    Add-LogLine $Log $(if ($ResultDir) { "# --- output (stdout and the files in $ResultDir)" } else { '# --- stdout' })
    Add-LogLine $Log $stdout
    $stderr = Read-Shared $tmpErr
    Add-LogLine $Log '# --- stderr (UBSan and ASan reports go here)'
    Add-LogLine $Log $stderr
    Add-LogLine $Log "# Exit code: $($p.ExitCode)"
    $ran = (-not $IsTest) -or (Test-TestSummary "$stdout`n$stderr")
    if (-not $ran) { Add-LogLine $Log "# NOT RUN: $Exe printed no test summary; it stopped before or during its tests." }
    # Not "# End:": that line is written once, at the end of the run (run 90 checks it).
    Add-LogLine $Log "# Program end: $(Get-Stamp)"
    $reports = ([regex]::Matches($stderr, 'runtime error:|ERROR: AddressSanitizer')).Count
    Write-Host ("exit code {0}, {1} sanitizer reports, {2:hh\:mm\:ss}" -f $p.ExitCode, $reports, $clock.Elapsed)
    if (-not $ran) { Write-Host "  $Exe ran no tests (no test summary): see the log." -ForegroundColor Red }
}

# --- a) Tests ------------------------------------------------------------------------------------
$log = "$OutDir\09-ubsan-tests.log"
New-Log $log '09: UBSan + ASan, all test programs (QT_QPA_PLATFORM=offscreen)' $header
foreach ($t in $BoostTestPrograms) {
    Invoke-Logged $log $t '' 60 -IsTest
}
# Qt Test writes nothing to stdout when the script runs without a console window; into files it
# always does, one per test class.
$qtResults = "$BuildDir\qt-results"
Invoke-Logged $log 'qt_tests.exe' "-o `"$qtResults\qt_tests.txt,txt`"" 60 -IsTest -ResultDir $qtResults

# --- b) Dev tools on real scans ------------------------------------------------------------------
Add-LogLine $log "# End: $(Get-Stamp)"
if (-not $Images) { Write-RunEnd 0 @("Log: $log"); exit 0 }
$testLog = $log
$log = "$OutDir\09-ubsan-tools.log"
$tools = New-DevToolInput $Images $BuildDir
New-Log $log '09: UBSan + ASan, dev tools binarization_compare and color_detect on real scans' ($header + $tools.Header)
foreach ($c in $tools.Calls) { Invoke-Logged $log $c.Exe $c.Args 120 }
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd 0 @("Logs: $testLog", "      $log")
exit 0
