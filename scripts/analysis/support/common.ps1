<#
.SYNOPSIS
  Helpers for the analysis runs in this folder. Dot-sourced by the run scripts.

.DESCRIPTION
  Loads the shared helpers of ..\..\support\common.ps1 (Visual Studio, vcpkg, logging) and adds the
  configure, build and compile database helpers of the runs.
#>

. "$PSScriptRoot\..\..\support\common.ps1"
if (-not $Vs) {
    throw 'No Visual Studio installation with the C++ tools found: scripts\setup\01-setup-windows.ps1 installs it.'
}
# All logs and results go here unless a script is given -OutDir (ignored by git).
$ResultsDir = "$(Split-Path $PSScriptRoot)\results"
# The -Yes of the run scripts: start without asking.
$Unattended = [bool]$Yes
$AssumeYes = [bool]$Yes

# Shows what a run does and asks before it starts. $BuildDir: the fresh build directory a run
# creates; when it is left from an earlier run, the run offers to delete it first. Returns $false
# when the run is not to start.
function Confirm-Run([string]$Title, [string[]]$Lines, [string]$Log, [string]$BuildDir = '') {
    Write-Host ''
    Write-Host $Title -ForegroundColor Cyan
    Write-Host ''
    foreach ($line in $Lines) { Write-Host $line }
    Write-Host "Log: $Log"
    Write-Host ''
    if ($BuildDir -and (Test-Path -LiteralPath $BuildDir)) {
        Write-Host "$BuildDir is left from an earlier run; this run needs a fresh one." -ForegroundColor Yellow
        # Only a CMake build directory is deleted here, never a folder given by mistake.
        if (-not (Test-Path -LiteralPath "$BuildDir\CMakeCache.txt")) {
            Write-Host 'It holds no CMake build, so it is not deleted here: remove it yourself, or pass -BuildDir.' -ForegroundColor Red
            return $false
        }
        if (-not (Read-YesNo 'Delete it and start the run?' $false)) { return $false }
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
        Write-Host "Deleted $BuildDir."
        return $true
    }
    return (Read-YesNo 'Start the run?' $true)
}

# The runs 02 to 04 read the compile database of run 01.
function Assert-Run01([string]$BuildDir) {
    if (-not (Test-Path "$BuildDir\compile_commands.json")) {
        throw "$BuildDir\compile_commands.json not found: run 01-msvc-analyze.ps1 first."
    }
}

# The last lines of a run: whether it finished, and where its results are.
function Write-RunEnd([int]$Rc, [string[]]$Results) {
    Write-Host ''
    if ($Rc -eq 0) { Write-Host 'The run is finished.' -ForegroundColor Green }
    else { Write-Host "The run is finished with exit code $Rc." -ForegroundColor Red }
    foreach ($result in $Results) { Write-Host "  $result" }
}

# clang-cl.exe of Visual Studio, for the runs 07 and 08.
function Get-ClangCl {
    $clang = "$LlvmBin\clang-cl.exe"
    if (-not (Test-Path $clang)) {
        throw "clang-cl not found in $LlvmBin; scripts\setup\02-setup-analysis.ps1 adds the Visual Studio " +
              "component 'C++ Clang Compiler for Windows'."
    }
    return $clang
}

# The configure arguments shared by all builds (generator, toolchain, project options).
function Get-ConfigureArgs([string]$BuildDir, [string]$VcpkgRoot, [string]$BuildType = 'Release') {
    $toolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" -replace '\\', '/'
    return "-S `"$Root`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$BuildType " +
           "-DCMAKE_TOOLCHAIN_FILE=$toolchain -DVCPKG_TARGET_TRIPLET=x64-windows " +
           '-DENABLE_OCR=ON -DBUILD_TESTS=ON -DBUILD_DEV_TOOLS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
}

# Configures and builds a fresh build directory; returns the exit code of the failing step or 0.
function Invoke-Build([string]$Log, [string]$BuildDir, [string]$Configure) {
    if (Test-Path $BuildDir) { throw "$BuildDir already exists; remove it for a full rebuild." }
    $cfg = "cmake $Configure"
    $build = "cmake --build `"$BuildDir`" -- -k 0"
    Add-LogLine $Log "# Configure: $cfg`r`n"
    $rc = Invoke-VsLogged $cfg $Log 'Configuring with CMake'
    Add-LogLine $Log "`r`n# Configure exit code: $rc"
    if ($rc -eq 0) {
        Add-LogLine $Log "# Build: $build`r`n"
        $rc = Invoke-VsLogged $build $Log 'Building'
        Add-LogLine $Log "`r`n# Build exit code: $rc (non-zero if any build step failed)"
    }
    return $rc
}

# The compile database of a build, reduced to the sources under src/ without the tests and
# without generated files. Switches matching any of $DropPatterns are removed from the commands.
function Get-FilteredCompileDb([string]$BuildDir, [string[]]$DropPatterns = @()) {
    $db = Get-Content "$BuildDir\compile_commands.json" -Raw | ConvertFrom-Json
    $src = ($Root -replace '\\', '/') + '/src/'
    $sel = @($db | Where-Object {
        $f = $_.file -replace '\\', '/'
        $f.StartsWith($src) -and $f -notmatch '/(tests?|qt_tests)/'
    })
    foreach ($e in $sel) {
        foreach ($p in $DropPatterns) { $e.command = $e.command -replace $p, '' }
    }
    return , $sel
}

function Save-CompileDb($Entries, [string]$Dir) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    [IO.File]::WriteAllText("$Dir\compile_commands.json", (ConvertTo-Json -InputObject $Entries -Depth 5), $Utf8)
}

# Counts the language standard switches in a compile database, as a check of the configuration.
function Get-StdSwitches([string]$BuildDir) {
    $db = Get-Content "$BuildDir\compile_commands.json" -Raw | ConvertFrom-Json
    return ($db | ForEach-Object { [regex]::Matches($_.command, '[-/]std:\S+') | ForEach-Object Value } |
        Group-Object | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ', '
}

# The MSVC conformance runs 05 and 06: a full rebuild with /W4 (dependency headers external,
# their warnings suppressed) like run 01, but without /analyze, so their warnings can be compared
# with the C4xxx warnings of run 01. $Flags are added to /W4, $Extra to the configure arguments.
function Invoke-ConformanceRun([string]$Id, [string]$Name, [string]$Title, [string]$Flags,
                               [string]$Extra, [string]$OutDir, [string]$BuildDir, [string]$VcpkgRoot) {
    $flagsAll = "/W4 /external:W0 /external:anglebrackets $Flags".Trim()
    $log = "$OutDir\$Id-$Name.log"
    $env:CFLAGS = $flagsAll
    $env:CXXFLAGS = $flagsAll
    New-Log $log "${Id}: $Title, full rebuild in $(Split-Path $BuildDir -Leaf)" (@(
        "# Compiler: $(Get-ClVersion)"
    ) + (Get-ToolVersions) + @("# CFLAGS/CXXFLAGS env: $flagsAll"))
    $rc = Invoke-Build $log $BuildDir "$(Get-ConfigureArgs $BuildDir $VcpkgRoot) -DENABLE_CODE_ANALYSIS=OFF $Extra"
    if (Test-Path "$BuildDir\compile_commands.json") {
        # Records the language standard actually used, as a check of the override.
        Add-LogLine $log "# Standard switches in compile_commands.json: $(Get-StdSwitches $BuildDir)"
    }
    Add-LogLine $log "# End: $(Get-Stamp)"
    return $rc
}
