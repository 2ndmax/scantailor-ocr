<#
.SYNOPSIS
  Helpers for the setup scripts in this folder: questions, the step menu and running commands
  visibly. Dot-sourced after ..\common.ps1.

.DESCRIPTION
  Every setup script shows its steps in a menu; 0 runs all of them in order. Given -Steps, a
  script runs those steps without the menu and takes the default answer of every question
  (-Yes answers yes instead), so it can be run unattended.
#>

$LogDir = "$PSScriptRoot\logs"
# Set by each script: $true when it runs with -Steps instead of the menu.
$Unattended = $false
$AssumeYes = $false

# A suggestion for a folder on drive D: if that drive exists, else on C:.
function Get-DefaultBase {
    if (Test-Path 'D:\') { return 'D:\' }
    return 'C:\'
}

# Shows the title and what a script does, before its first question. Not shown unattended.
function Write-Intro([string]$Title, [string[]]$Lines) {
    if ($Unattended) { return }
    Write-Host ''
    Write-Host $Title -ForegroundColor Cyan
    Write-Host ''
    foreach ($line in $Lines) { Write-Host $line }
    Write-Host ''
}

# Asks for a folder; Enter takes $Default. The folder must not contain spaces or non-ASCII
# characters, as some of the build tools (vcpkg's ports among them) fail on such paths.
function Read-Folder([string]$Question, [string]$Default) {
    if (-not $Unattended) {
        Write-Host 'Press Enter to use the suggestion in brackets, or type another folder'
        Write-Host '(without spaces or non-English characters).'
    }
    while ($true) {
        $answer = $Default
        if (-not $Unattended) {
            $typed = Read-Host "$Question [$Default]"
            if ($typed.Trim()) { $answer = $typed.Trim().Trim('"') }
        }
        $answer = [IO.Path]::GetFullPath($answer)
        if ($answer -match '\s' -or $answer -match '[^\x21-\x7E]') {
            if ($Unattended) { throw "The folder must not contain spaces or non-English characters: $answer" }
            Write-Host 'The folder must not contain spaces or non-English characters.' -ForegroundColor Yellow
            continue
        }
        # A drive root keeps its backslash: "D:" alone would mean the current folder on D:.
        if ($answer.Length -gt 3) { $answer = $answer.TrimEnd('\') }
        return $answer
    }
}

# A yes/no question; Enter takes $Default. Unattended: $Default, or yes with -Yes.
function Read-YesNo([string]$Question, [bool]$Default) {
    if ($Unattended) { return ($AssumeYes -or $Default) }
    $hint = if ($Default) { '[Y/n]' } else { '[y/N]' }
    while ($true) {
        $typed = (Read-Host "$Question $hint").Trim().ToLowerInvariant()
        if (-not $typed) { return $Default }
        if ($typed -in 'y', 'yes', 'j', 'ja') { return $true }
        if ($typed -in 'n', 'no', 'nein') { return $false }
    }
}

# A choice between numbered options; Enter takes the first. Unattended: the first.
function Read-Choice([string]$Question, [string[]]$Options) {
    if ($Unattended) { return 0 }
    Write-Host $Question
    for ($i = 0; $i -lt $Options.Count; $i++) { Write-Host ("  {0}  {1}" -f ($i + 1), $Options[$i]) }
    while ($true) {
        $typed = (Read-Host "Choice [1]").Trim()
        if (-not $typed) { return 0 }
        $n = 0
        if ([int]::TryParse($typed, [ref]$n) -and $n -ge 1 -and $n -le $Options.Count) { return $n - 1 }
    }
}

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

# Runs a command line through cmd (in the Visual Studio developer environment with -InVsEnv) and
# shows its output while it runs; the output also goes to $Log. Returns the exit code.
function Invoke-Shown([string]$CommandLine, [string]$Log, [switch]$InVsEnv) {
    Add-LogLine $Log "`r`n# $(Get-Stamp) Command: $CommandLine"
    Write-Host "> $CommandLine" -ForegroundColor DarkGray
    $full = if ($InVsEnv) { "`"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $CommandLine" } else { $CommandLine }
    # cmd merges stderr into stdout itself, so PowerShell only sees plain output lines.
    cmd /c "$full 2>&1" | ForEach-Object {
        Write-Host $_
        Add-LogLine $Log $_
    }
    $rc = $LASTEXITCODE
    Add-LogLine $Log "# Exit code: $rc"
    return $rc
}

# The step menu. $Steps: ordered hashtables with Title, Done (scriptblock: $true when the step's
# work is already there) and Run (scriptblock: $true on success). Runs $Selected (numbers, 0 = all)
# without the menu when it is given.
function Invoke-StepMenu([string]$Title, [string[]]$Info, [object[]]$Steps, [int[]]$Selected) {
    $runSteps = {
        param([int[]]$Numbers)
        if ($Numbers -contains 0) { $Numbers = 1..$Steps.Count }
        foreach ($n in $Numbers) {
            if ($n -lt 1 -or $n -gt $Steps.Count) { Write-Host "No step $n." -ForegroundColor Yellow; return $false }
            $step = $Steps[$n - 1]
            Write-Step "Step ${n}: $($step.Title)"
            $ok = & $step.Run
            if (-not $ok) {
                Write-Host "Step $n did not finish; see the messages above." -ForegroundColor Red
                return $false
            }
            Write-Host "Step $n finished." -ForegroundColor Green
        }
        return $true
    }
    # Count, not truthiness: @(0) ("all steps") counts as false in PowerShell.
    if ($Selected.Count -gt 0) { return (& $runSteps $Selected) }

    while ($true) {
        Write-Host ''
        Write-Host $Title -ForegroundColor Cyan
        foreach ($line in $Info) { Write-Host $line }
        Write-Host ''
        Write-Host '  0  All steps in order'
        for ($i = 0; $i -lt $Steps.Count; $i++) {
            $mark = if (& $Steps[$i].Done) { '  [done]' } else { '' }
            Write-Host ("  {0}  {1}{2}" -f ($i + 1), $Steps[$i].Title, $mark)
        }
        Write-Host '  Q  Quit'
        $typed = (Read-Host 'Step(s), e.g. 0 or 2,4').Trim()
        if ($typed -match '^[qQ]$') { return $true }
        $numbers = @($typed -split '[,\s]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        if (-not $numbers.Count) { continue }
        [void](& $runSteps $numbers)
    }
}
