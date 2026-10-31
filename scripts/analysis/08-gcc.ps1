<#
.SYNOPSIS
  Run 08: GCC in WSL (Ubuntu 24.04) - strict warnings, then UBSan, ASan and the checks of the
  GCC standard library while all tests run.

.DESCRIPTION
  Builds the project twice with GCC in WSL, each a full rebuild in the Linux file system of WSL
  (~/scantailor-ocr-build), from the sources in this repository (/mnt/c/...):
    a) warnings: -DENABLE_CODE_ANALYSIS=ON (-Wall -Wextra -Wshadow -Wconversion) plus warnings
       only GCC has (-Wnull-dereference, -Wduplicated-cond, -Wduplicated-branches,
       -Wlogical-op)                                                    -> 08-gcc-warnings.log
    b) sanitizers: -fsanitize=address,undefined and -D_GLIBCXX_ASSERTIONS, which makes the
       standard library check indexes and iterators              -> 08-gcc-sanitizers-build.log
  and then runs all tests of build b) with ctest -V                    -> 08-gcc-tests.log
  Both builds are RelWithDebInfo: some of the warnings need the optimizer, and the reports
  get function names. The warnings come from a build of their own, as the sanitizers make GCC
  warn about code they add themselves; build b) has only the warnings of a normal build.

  The reports are in 08-gcc-tests.log: UBSan as "runtime error: ..." with file:line, ASan as
  "ERROR: AddressSanitizer: ...", both with stack traces; the tests keep running after them. A
  failed check of the standard library ("Assertion '...' failed.") stops its test program; ASan
  then reports the abort ("ABRT") with the stack trace.
  11-merge-findings.ps1 reads all three logs.

  Linux code paths are checked here, not the Windows ones (#ifdef _WIN32); runs 01 to 07 and 09 check
  those. Needs WSL with Ubuntu 24.04 and the packages of scripts\setup\04-setup-wsl-gcc.ps1.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\08-gcc.ps1

.PARAMETER SkipBuild
  Reuse the build of part b) and only run the tests.

.PARAMETER OutDir
  Where the logs go. Default: results in this folder.

.PARAMETER Yes
  Start without asking.
#>
param(
    [switch]$SkipBuild,
    [string]$OutDir = '',
    [switch]$Yes
)
. "$PSScriptRoot\support\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not (Test-UbuntuPackages)) {
    throw "GCC in WSL is not set up: scripts\setup\04-setup-wsl-gcc.ps1 installs WSL, $WslDistro and the packages."
}

$wslHome = (Get-WslOutput "-d $WslDistro --exec printenv HOME").Lines | Select-Object -First 1
# The build directories are deleted with rm -rf before each build: only below a plain home folder
# (a name without spaces or wildcards), never below an unexpected one.
if ($wslHome -notmatch '^(/home/[A-Za-z0-9._-]+|/root)$') { throw "No home folder found in $WslDistro (got '$wslHome')." }
$buildBase = "$wslHome/scantailor-ocr-build"
$warningsDir = "$buildBase/gcc-warnings"
$sanitizersDir = "$buildBase/gcc-sanitizers"
$source = ConvertTo-WslPath $Root
# Parallel compile jobs: one per core, as ninja would, but at most one per 1.5 GB of the memory WSL
# has (by default half of what Windows has): GCC needs up to that much for one file of this
# project, more with the sanitizers, and too many at once make Linux stop the build.
$cores = [int]((Get-WslOutput "-d $WslDistro --exec nproc").Lines | Select-Object -First 1)
$memKb = [long](((Get-WslOutput "-d $WslDistro --exec grep MemTotal /proc/meminfo").Lines | Select-Object -First 1) -replace '\D', '')
$jobs = [math]::Max(1, [math]::Min($cores, [int][math]::Floor($memKb / 1.5MB)))
$wsl = "`"$WslExe`" -d $WslDistro --exec"

$warningFlags = '-Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wlogical-op'
$sanitizerFlags = '-fsanitize=address,undefined -fsanitize-recover=address -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS'
$ubsanOptions = 'print_stacktrace=1:report_error_type=1:halt_on_error=0'
# handle_abort: a failed check of the standard library aborts the program; ASan then reports it
# with a stack trace, which shows where in the project's code it happened.
$asanOptions = 'halt_on_error=0:detect_leaks=0:handle_abort=1'
$header = @(
    "# Compiler: $((Get-WslOutput "-d $WslDistro --exec g++ --version").Lines | Select-Object -First 1)"
    "# $((Get-WslOutput "-d $WslDistro --exec cmake --version").Lines | Select-Object -First 1)"
    "# ninja $((Get-WslOutput "-d $WslDistro --exec ninja --version").Lines | Select-Object -First 1)"
    "# Linux: $WslDistro in WSL; sources: $source"
    "# Compile jobs: $jobs ($cores cores, $([math]::Round($memKb / 1MB, 1)) GB memory in WSL)"
)

$intro = @(
    "Builds everything twice with GCC in WSL ($WslDistro): once with strict warnings, once with the"
    'sanitizers ASan and UBSan and the checks of the standard library, then runs all tests with them.'
)
if ($SkipBuild) {
    if ((Get-WslOutput "-d $WslDistro --exec test -f `"$sanitizersDir/CMakeCache.txt`"").Code -ne 0) {
        throw "$sanitizersDir holds no build: run without -SkipBuild first."
    }
    $intro += 'With -SkipBuild the existing build is used and only the tests run.'
} else {
    $intro += "Each build takes longer than a normal build; $jobs compile jobs at once ($cores cores, " +
              "$([math]::Round($memKb / 1MB, 1)) GB memory in WSL)."
    $intro += "Build directories (in WSL, deleted first if left from an earlier run): $buildBase"
}
if (-not (Confirm-Run 'Analysis run 08: GCC in WSL' $intro "$OutDir\08-gcc-*.log")) {
    Write-Host 'Not started.'
    exit 1
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

# Configures and builds a fresh build directory in WSL; returns the exit code of the failing step or 0.
function Invoke-WslBuild([string]$Log, [string]$BuildDir, [string]$CxxFlags, [string]$Extra, [string]$LdFlags = '') {
    [void](Get-WslOutput "-d $WslDistro --exec rm -rf `"$BuildDir`"")
    $envVars = "env `"CFLAGS=$CxxFlags`" `"CXXFLAGS=$CxxFlags`""
    if ($LdFlags) { $envVars += " `"LDFLAGS=$LdFlags`"" }
    $cfg = "$wsl $envVars cmake -S `"$source`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo " +
           "-DENABLE_OCR=ON -DBUILD_TESTS=ON -DBUILD_DEV_TOOLS=ON $Extra"
    $build = "$wsl cmake --build `"$BuildDir`" -- -k 0 -j $jobs"
    Add-LogLine $Log "# Configure: $cfg`r`n"
    $rc = Invoke-Streamed $cfg $Log 'Configuring with CMake'
    Add-LogLine $Log "`r`n# Configure exit code: $rc"
    if ($rc -eq 0) {
        Add-LogLine $Log "# Build: $build`r`n"
        $rc = Invoke-Streamed $build $Log 'Building'
        Add-LogLine $Log "`r`n# Build exit code: $rc (non-zero if any build step failed)"
    }
    Add-LogLine $Log "# End: $(Get-Stamp)"
    return $rc
}

# --- a) and b) Builds ----------------------------------------------------------------------------
$logs = @()
if (-not $SkipBuild) {
    $log = "$OutDir\08-gcc-warnings.log"
    $logs += $log
    New-Log $log '08: GCC, strict warnings (RelWithDebInfo, ENABLE_CODE_ANALYSIS)' ($header + @(
        "# Build directory: $warningsDir"
        "# CFLAGS/CXXFLAGS: $warningFlags"
    ))
    Write-Host ''
    Write-Host 'a) Build with strict warnings' -ForegroundColor Cyan
    $rc = Invoke-WslBuild $log $warningsDir $warningFlags '-DENABLE_CODE_ANALYSIS=ON'
    if ($rc -ne 0) { Write-RunEnd $rc @("Log: $log"); exit $rc }

    $log = "$OutDir\08-gcc-sanitizers-build.log"
    $logs += $log
    New-Log $log '08: GCC with UBSan, ASan and _GLIBCXX_ASSERTIONS (RelWithDebInfo)' ($header + @(
        "# Build directory: $sanitizersDir"
        "# CFLAGS/CXXFLAGS: $sanitizerFlags"
    ))
    Write-Host ''
    Write-Host 'b) Build with the sanitizers' -ForegroundColor Cyan
    $rc = Invoke-WslBuild $log $sanitizersDir $sanitizerFlags '-DENABLE_CODE_ANALYSIS=OFF' '-fsanitize=address,undefined'
    if ($rc -ne 0) { Write-RunEnd $rc ($logs | ForEach-Object { "Log: $_" }); exit $rc }
}

# --- Tests ---------------------------------------------------------------------------------------
# ctest -V shows the output of every test, also of the passing ones: the sanitizers report
# without failing the test. ctest puts "<test number>: " in front of each line.
$log = "$OutDir\08-gcc-tests.log"
$logs += $log
New-Log $log '08: GCC with UBSan, ASan and _GLIBCXX_ASSERTIONS, all tests (ctest -V)' ($header + @(
    "# Build directory: $sanitizersDir"
    "# UBSAN_OPTIONS: $ubsanOptions"
    "# ASAN_OPTIONS: $asanOptions"
))
Write-Host ''
Write-Host 'Tests with the sanitizers' -ForegroundColor Cyan
# BOOST_TEST_COLOR_OUTPUT: without it, Boost.Test writes color codes into the log.
$test = "$wsl env UBSAN_OPTIONS=$ubsanOptions ASAN_OPTIONS=$asanOptions BOOST_TEST_COLOR_OUTPUT=no ctest --test-dir `"$sanitizersDir`" -V --timeout 3600"
Add-LogLine $log "# Tests: $test`r`n"
$rc = Invoke-Streamed $test $log 'Running the tests'
Add-LogLine $log "`r`n# Tests exit code: $rc (non-zero if any test failed)"
Add-LogLine $log "# End: $(Get-Stamp)"
$text = [IO.File]::ReadAllText($log, $Utf8)
$reports = ([regex]::Matches($text, 'runtime error:|ERROR: AddressSanitizer|Assertion .+ failed')).Count
Write-Host "Sanitizer reports and failed checks of the standard library: $reports"
Write-RunEnd $rc ($logs | ForEach-Object { "Log: $_" })
exit $rc
