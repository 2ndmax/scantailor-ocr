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
  This one report is no finding in the project's code. qt_tests runs on after it; core_tests may
  stop right there (ASan "CHECK failed" in its stack depot) without running a single test. A test
  program whose output holds no test summary is marked "# NOT RUN:" in the log, and
  11-merge-findings.ps1 names it in findings.md.

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
  Start without asking; a build directory left from an earlier run is deleted.
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
$env:QT_QPA_PLATFORM = 'offscreen'
$env:PATH = "$rtDir;$env:PATH"
Copy-Item "$VcpkgRoot\installed\x64-windows\Qt6\plugins\platforms\qoffscreen.dll" "$BuildDir\platforms" -Force

# The redirect files may still be held open briefly after the process ends.
function Read-Shared([string]$Path) {
    for ($i = 0; ; $i++) {
        try {
            $fs = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
            try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
        } catch {
            if ($i -ge 20) { return "<could not read $Path : $_>" }
            Start-Sleep -Milliseconds 500
        }
    }
}

function Invoke-Logged([string]$Log, [string]$Exe, [string]$ArgLine, [int]$TimeoutMin, [switch]$IsTest) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "Running $Exe ... " -NoNewline
    Add-LogLine $Log "`r`n# ===== $Exe $ArgLine"
    Add-LogLine $Log "# Start: $(Get-Stamp)"
    $tmpOut = "$BuildDir\run-stdout.txt"
    $tmpErr = "$BuildDir\run-stderr.txt"
    $sp = @{ FilePath = "$BuildDir\$Exe"; WorkingDirectory = $BuildDir; NoNewWindow = $true
             PassThru = $true; RedirectStandardOutput = $tmpOut; RedirectStandardError = $tmpErr }
    if ($ArgLine) { $sp.ArgumentList = $ArgLine }
    $p = Start-Process @sp
    $null = $p.Handle  # Without this, Windows PowerShell 5.1 doesn't report the exit code.
    if (-not $p.WaitForExit($TimeoutMin * 60000)) {
        $p.Kill()
        $p.WaitForExit()
        Add-LogLine $Log "# TIMEOUT after $TimeoutMin minutes, process killed"
    }
    $stdout = Read-Shared $tmpOut
    Add-LogLine $Log '# --- stdout'
    Add-LogLine $Log $stdout
    $stderr = Read-Shared $tmpErr
    Add-LogLine $Log '# --- stderr (UBSan and ASan reports go here)'
    Add-LogLine $Log $stderr
    Add-LogLine $Log "# Exit code: $($p.ExitCode)"
    # Boost.Test ends with "*** No errors detected" or "*** N failure(s) ... detected", Qt Test with
    # "Totals:". Without either, the program stopped before its tests, and its exit code alone
    # doesn't show that.
    $summary = '\*\*\* No errors detected|\*\*\* \d+ failures? (is|are) detected|^Totals:'
    $ran = (-not $IsTest) -or ([regex]::IsMatch("$stdout`n$stderr", $summary, 'Multiline'))
    if (-not $ran) { Add-LogLine $Log "# NOT RUN: $Exe printed no test summary; it stopped before or during its tests." }
    Add-LogLine $Log "# End: $(Get-Stamp)"
    $reports = ([regex]::Matches($stderr, 'runtime error:|ERROR: AddressSanitizer')).Count
    Write-Host ("exit code {0}, {1} sanitizer reports, {2:hh\:mm\:ss}" -f $p.ExitCode, $reports, $clock.Elapsed)
    if (-not $ran) { Write-Host "  $Exe ran no tests (no test summary): see the log." -ForegroundColor Red }
}

# --- a) Tests ------------------------------------------------------------------------------------
$log = "$OutDir\09-ubsan-tests.log"
New-Log $log '09: UBSan + ASan, all test programs (QT_QPA_PLATFORM=offscreen)' $header
foreach ($t in 'foundation_tests.exe', 'math_tests.exe', 'imageproc_tests.exe', 'core_tests.exe', 'qt_tests.exe') {
    Invoke-Logged $log $t '' 60 -IsTest
}

# --- b) Dev tools on real scans ------------------------------------------------------------------
if (-not $Images) { Write-RunEnd 0 @("Log: $log"); exit 0 }
$testLog = $log
$log = "$OutDir\09-ubsan-tools.log"
$in = "$BuildDir\tool-input"
$res = "$BuildDir\tool-output"
New-Item -ItemType Directory -Force $in, "$res\binarization", "$res\binarization-colors", "$res\color_detect" | Out-Null
$imgs = Get-ChildItem $Images -File | Where-Object { $_.Extension -in '.tif', '.tiff', '.jp2' }
foreach ($i in $imgs) { Copy-Item $i.FullName $in -Force }
New-Log $log '09: UBSan + ASan, dev tools binarization_compare and color_detect on real scans' ($header + @(
    "# Input: copies of $($imgs.Count) images from $Images (top level, tif/jp2) in $in`:"
) + @($imgs | ForEach-Object { "#   $($_.Name) ($([int]($_.Length / 1KB)) KB)" }) + @(
    "# Output images: $res (not part of this log)"
))
Invoke-Logged $log 'binarization_compare.exe' "`"$res\binarization`" `"$in`"" 120
Invoke-Logged $log 'binarization_compare.exe' "--colors `"$res\binarization-colors`" `"$in`"" 120
Invoke-Logged $log 'color_detect.exe' "`"$in`" `"$res\color_detect`" --zones" 120
Write-RunEnd 0 @("Logs: $testLog", "      $log")
exit 0
