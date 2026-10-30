<#
.SYNOPSIS
  Run 09: fuzzing with libFuzzer and AddressSanitizer (the fuzz targets in src/fuzz).

.DESCRIPTION
  Builds the fuzz targets (-DBUILD_FUZZERS=ON, MSVC, RelWithDebInfo) and runs each of them for
  -Minutes. A fuzz target feeds generated input to one part of the program (image headers,
  image decoding, project files, filter settings, the processing of a page) and reports crashes,
  memory errors, inputs that take too long and inputs that take too much memory.

  Per target, in <OutDir>\09-fuzz\<target>:
    corpus\     the inputs found so far; the next run continues from them. The seed files in
                src\fuzz\corpus are read as well, but not changed.
    findings\   the inputs that made the target fail (crash-*, oom-*, timeout-*, leak-*).
    fuzz.log    the output of the last run.
  09-fuzz.log holds a summary per target and the report of every input in findings\: each is
  run once more, so a finding that has been fixed shows up without a report there. Delete
  findings\ once its inputs are dealt with.

  An existing build directory is built again (only what changed) instead of being deleted,
  so after a fix the next run starts within minutes.

  Why it is set up this way:
  - MSVC brings libFuzzer and ASan for the /MD runtime of the vcpkg libraries. The code of the
    project is all built with ASan: MSVC can't link instrumented with uninstrumented code.
  - Qt and the vcpkg libraries are not instrumented (they are DLLs): the fuzzer sees no coverage
    inside them and ASan checks no accesses there.
  - An input that needs more memory than -RssLimitMb in one piece counts as a finding; the
    report of a finding found with other limits may not come back with these.

  Without -Only and -Minutes, the script asks which targets to run and for how long; given on
  the command line, the question is skipped:

    powershell -ExecutionPolicy Bypass -File scripts\analysis\09-fuzz.ps1
    powershell -ExecutionPolicy Bypass -File scripts\analysis\09-fuzz.ps1 -Only fuzz_image_loader -Minutes 5

.PARAMETER Minutes
  How long each target runs. Asked for when not given; with -Yes 10.

.PARAMETER Only
  Run only these targets, e.g. -Only fuzz_image_loader. Asked for when not given; with -Yes all.

.PARAMETER RssLimitMb
  Memory limit of a run in MB; an input that needs more counts as a finding (oom-*). Default:
  2048. The targets skip images over 16 megapixels, so a correct input needs far less.

.PARAMETER SkipBuild
  Use the existing build directory as it is.

.PARAMETER OutDir
  Where the logs, corpora and findings go. Default: results in this folder.

.PARAMETER BuildDir
  The build directory. Default: build-fuzz in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder. Default: the VCPKG_ROOT environment variable.

.PARAMETER Yes
  Start without asking: all targets for 10 minutes each, unless -Only and -Minutes say otherwise.
#>
param(
    [int]$Minutes = 10,
    [string[]]$Only = @(),
    [int]$RssLimitMb = 2048,
    [switch]$SkipBuild,
    [string]$OutDir = '',
    [string]$BuildDir = '',
    [string]$VcpkgRoot = '',
    [switch]$Yes
)
. "$PSScriptRoot\common.ps1"
if (-not $OutDir) { $OutDir = $ResultsDir }
if (-not $BuildDir) { $BuildDir = "$Root\build-fuzz" }
$VcpkgRoot = Get-VcpkgRoot $VcpkgRoot
# Without it the build fails only when linking the first fuzz target.
if (-not (Test-Path "$Vs\VC\Tools\MSVC\*\lib\x64\clang_rt.fuzzer_MD-x86_64.lib")) {
    throw "clang_rt.fuzzer_MD-x86_64.lib not found: scripts\setup\2-setup-analysis.ps1 (step 1) adds the " +
          "Visual Studio component 'C++ AddressSanitizer'."
}
$fuzzDir = "$OutDir\09-fuzz"
$log = "$OutDir\09-fuzz.log"
$seeds = "$Root\src\fuzz\corpus"

# The targets of src/fuzz/CMakeLists.txt with their seeds and dictionary.
$targets = @(
    @{ Name = 'fuzz_image_metadata'; Title = 'image headers (metadata of TIFF, PNG, JPEG, JPEG 2000 ...)'
       Seeds = 'images'; Dict = '' }
    @{ Name = 'fuzz_image_loader'; Title = 'decoding images'; Seeds = 'images'; Dict = '' }
    @{ Name = 'fuzz_project_reader'; Title = 'reading project files'; Seeds = 'project'; Dict = 'project.dict' }
    @{ Name = 'fuzz_filter_params'; Title = 'filter settings in project files'; Seeds = 'filter_params'
       Dict = 'filter_params.dict' }
    @{ Name = 'fuzz_image_processing'; Title = 'processing a page (binarization, despeckling, deskew, split, content)'
       Seeds = 'processing'; Dict = '' }
)
# The choices offered for the time per target, and the one taken with Enter.
$minuteChoices = @(5, 10, 30, 60)
$defaultMinutes = 10

# Asks which targets to run. Returns their numbers (1-based); Enter takes all.
function Read-Targets {
    Write-Host ''
    Write-Host 'Which targets?'
    Write-Host '  0  All targets'
    for ($i = 0; $i -lt $targets.Count; $i++) {
        $t = $targets[$i]
        # What earlier runs left: the inputs found so far, and findings not yet dealt with.
        $dir = "$fuzzDir\$($t.Name)"
        $corpus = if (Test-Path "$dir\corpus") { @(Get-ChildItem "$dir\corpus" -File).Count } else { 0 }
        $findings = if (Test-Path "$dir\findings") { @(Get-ChildItem "$dir\findings" -File).Count } else { 0 }
        $state = if ($corpus) { "corpus $corpus inputs" } else { 'not run yet' }
        if ($findings) { $state += ", $findings findings" }
        Write-Host ("  {0}  {1}: {2}  [{3}]" -f ($i + 1), $t.Name, $t.Title, $state)
    }
    while ($true) {
        $typed = (Read-Host 'Target(s), e.g. 0 or 2,4 [0]').Trim()
        if (-not $typed) { $typed = '0' }
        $numbers = @($typed -split '[,\s]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        if ($numbers -contains 0) { return @(1..$targets.Count) }
        if ($numbers.Count -and -not @($numbers | Where-Object { $_ -lt 1 -or $_ -gt $targets.Count }).Count) {
            return @($numbers | Sort-Object -Unique)
        }
        Write-Host "Numbers from 0 to $($targets.Count), separated by commas." -ForegroundColor Yellow
    }
}

# Asks how long each of $Count targets runs. Returns the minutes; Enter takes $defaultMinutes.
function Read-Minutes([int]$Count) {
    Write-Host ''
    Write-Host 'How long per target?'
    for ($i = 0; $i -lt $minuteChoices.Count; $i++) {
        $m = $minuteChoices[$i]
        Write-Host ("  {0}  {1} minutes (about {2} minutes in all)" -f ($i + 1), $m, ($m * $Count))
    }
    Write-Host ("  {0}  Other" -f ($minuteChoices.Count + 1))
    while ($true) {
        $typed = (Read-Host "Choice [$($minuteChoices.IndexOf($defaultMinutes) + 1)]").Trim()
        if (-not $typed) { return $defaultMinutes }
        $n = 0
        if ([int]::TryParse($typed, [ref]$n) -and $n -ge 1 -and $n -le $minuteChoices.Count) { return $minuteChoices[$n - 1] }
        if ($n -eq $minuteChoices.Count + 1) {
            while ($true) {
                $typed = (Read-Host 'Minutes per target').Trim()
                if ([int]::TryParse($typed, [ref]$n) -and $n -ge 1) { return $n }
            }
        }
    }
}

if ($Only) {
    $unknown = @($Only | Where-Object { $_ -notin $targets.Name })
    if ($unknown) { throw "Unknown fuzz target: $($unknown -join ', '). Known: $($targets.Name -join ', ')" }
    $targets = @($targets | Where-Object { $_.Name -in $Only })
} elseif (-not $Unattended) {
    Write-Host ''
    Write-Host 'Analysis run 09: fuzzing with libFuzzer and ASan' -ForegroundColor Cyan
    $targets = @(Read-Targets | ForEach-Object { $targets[$_ - 1] })
}
if (-not $PSBoundParameters.ContainsKey('Minutes') -and -not $Unattended) {
    $Minutes = Read-Minutes $targets.Count
}
# A single input that runs longer than this is a finding (timeout-*).
$timeoutSec = 20
$fuzzArgs = "-rss_limit_mb=$RssLimitMb -timeout=$timeoutSec"

$intro = @(
    'Builds the fuzz targets with MSVC and AddressSanitizer and runs each of them; they feed generated'
    'input to the image readers, the project file reader, the filter settings and the processing of a'
    'page and report crashes, memory errors and inputs that need too much time or memory.'
    "Targets: $($targets.Name -join ', ')"
    "Time: $Minutes minutes per target, about $($Minutes * $targets.Count) minutes in all."
)
if ($SkipBuild) {
    if (-not (Test-Path "$BuildDir\CMakeCache.txt")) { throw "$BuildDir holds no build: run without -SkipBuild first." }
    $intro += 'With -SkipBuild the existing build is used.'
} elseif (Test-Path "$BuildDir\CMakeCache.txt") {
    $intro += 'The existing build directory is brought up to date.'
} else {
    $intro += 'The first build takes as long as a normal build.'
}
$intro += "Build directory: $BuildDir"
$intro += "Corpora and findings: $fuzzDir"
if (-not (Confirm-Run 'Analysis run 09: fuzzing with libFuzzer and ASan' $intro $log)) {
    Write-Host 'Not started.'
    exit 1
}

# --- Build ---------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    $buildLog = "$OutDir\09-fuzz-build.log"
    New-Log $buildLog '09: fuzz targets, build with MSVC and ASan (RelWithDebInfo)' (@(
        "# Compiler: $(Get-ClVersion)"
        "# Build directory: $BuildDir"
    ) + (Get-ToolVersions))
    $rc = 0
    if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
        $toolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" -replace '\\', '/'
        $cfg = "cmake -S `"$Root`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo " +
               "-DCMAKE_TOOLCHAIN_FILE=$toolchain -DVCPKG_TARGET_TRIPLET=x64-windows -DENABLE_OCR=ON " +
               '-DBUILD_FUZZERS=ON'
        Add-LogLine $buildLog "# Configure: $cfg`r`n"
        $rc = Invoke-VsLogged $cfg $buildLog 'Configuring with CMake'
        Add-LogLine $buildLog "`r`n# Configure exit code: $rc"
    }
    if ($rc -eq 0) {
        $build = "cmake --build `"$BuildDir`""
        Add-LogLine $buildLog "# Build: $build`r`n"
        $rc = Invoke-VsLogged $build $buildLog 'Building'
        Add-LogLine $buildLog "`r`n# Build exit code: $rc"
    }
    Add-LogLine $buildLog "# End: $(Get-Stamp)"
    if ($rc -ne 0) { Write-RunEnd $rc @("Log: $buildLog"); exit $rc }
}

# Runs a fuzz target; output to $OutFile. Returns the exit code.
function Invoke-Fuzzer([string]$Exe, [string]$ArgLine, [string]$WorkDir, [string]$OutFile, [int]$TimeoutMin) {
    $sp = @{ FilePath = $Exe; ArgumentList = $ArgLine; WorkingDirectory = $WorkDir; NoNewWindow = $true
             PassThru = $true; RedirectStandardOutput = "$OutFile.stdout"; RedirectStandardError = $OutFile }
    $p = Start-Process @sp
    $null = $p.Handle  # Without this, Windows PowerShell 5.1 doesn't report the exit code.
    if (-not $p.WaitForExit($TimeoutMin * 60000)) {
        $p.Kill()
        $p.WaitForExit()
        Add-Content -LiteralPath $OutFile "`r`n# KILLED after $TimeoutMin minutes"
    }
    Remove-Item -LiteralPath "$OutFile.stdout" -ErrorAction SilentlyContinue
    return $p.ExitCode
}

New-Log $log "09: fuzzing, $Minutes minutes per target" @(
    "# Compiler: $(Get-ClVersion)"
    "# Build directory: $BuildDir"
    "# Options: $fuzzArgs"
)

# --- Fuzzing -------------------------------------------------------------------------------------
foreach ($t in $targets) {
    $exe = "$BuildDir\$($t.Name).exe"
    if (-not (Test-Path $exe)) { throw "$exe not found: the build has no such target." }
    $dir = "$fuzzDir\$($t.Name)"
    New-Item -ItemType Directory -Force "$dir\corpus", "$dir\findings" | Out-Null
    # The prefix ends in a backslash; doubled before the closing quote, so it doesn't escape it.
    $argLine = "$fuzzArgs -max_total_time=$($Minutes * 60) -print_final_stats=1 -artifact_prefix=`"$dir\findings\\`""
    if ($t.Dict) { $argLine += " -dict=`"$Root\src\fuzz\$($t.Dict)`"" }
    # New inputs go into the first folder; the seeds are only read.
    $argLine += " `"$dir\corpus`" `"$seeds\$($t.Seeds)`""

    Write-Host "Fuzzing $($t.Name) for $Minutes minutes ... " -NoNewline
    $before = @(Get-ChildItem "$dir\findings" -File).Count
    $rc = Invoke-Fuzzer $exe $argLine $dir "$dir\fuzz.log" ($Minutes + 10)
    $out = [IO.File]::ReadAllText("$dir\fuzz.log")
    $stat = { param($name) if ($out -match "stat::${name}:\s+(\d+)") { $Matches[1] } else { '?' } }
    $ft = if ($out -match '(?s).*\bft: (\d+)') { $Matches[1] } else { '?' }
    $corpus = @(Get-ChildItem "$dir\corpus" -File).Count
    $new = @(Get-ChildItem "$dir\findings" -File).Count - $before
    $summary = "runs $(& $stat 'number_of_executed_units'), $(& $stat 'average_exec_per_sec')/s, " +
               "coverage (ft) $ft, corpus $corpus inputs, peak $(& $stat 'peak_rss_mb') MB, new findings $new"
    Write-Host $summary -ForegroundColor $(if ($new -gt 0) { 'Yellow' } else { 'Gray' })
    Add-LogLine $log "# ===== $($t.Name): exit code $rc, $summary"
}

# --- Reports of all findings ---------------------------------------------------------------------
# Each input in findings\ runs once more, so its report is in this log (for 10-merge-findings.ps1),
# and one that no longer fails shows that it was fixed.
$total = 0
foreach ($t in $targets) {
    $dir = "$fuzzDir\$($t.Name)"
    foreach ($f in Get-ChildItem "$dir\findings" -File | Sort-Object Name) {
        $total++
        Add-LogLine $log "`r`n# Finding: $($t.Name) $($f.FullName)"
        $replay = "$dir\replay.log"
        $rc = Invoke-Fuzzer "$BuildDir\$($t.Name).exe" "$fuzzArgs `"$($f.FullName)`"" $dir $replay 5
        $text = [IO.File]::ReadAllText($replay)
        Remove-Item -LiteralPath $replay
        # Only the report, without libFuzzer's lines about the run itself.
        $report = @($text -split "`r?`n" | Where-Object { $_ -notmatch '^(INFO:|Running:|Executed |\*\*\*)' -and $_ })
        if (-not ($text -match 'ERROR: (AddressSanitizer|libFuzzer)')) { $report = @('# No report with these limits: fixed, or found with other limits.') }
        Add-LogLine $log ($report -join "`r`n")
        Add-LogLine $log "# Replay exit code: $rc"
    }
}
Add-LogLine $log "`r`n# Findings in all: $total"
Add-LogLine $log "# End: $(Get-Stamp)"
Write-RunEnd 0 @("Log: $log", "Corpora and findings: $fuzzDir")
exit 0
