<#
.SYNOPSIS
  Helpers for the setup scripts in this folder: questions, the step menu and running commands
  visibly. Dot-sourced after ..\..\support\common.ps1.

.DESCRIPTION
  Every setup script shows its steps in a menu; 0 runs all of them in order. Given -Steps, a
  script runs those steps without the menu and takes the default answer of every question
  (-Yes answers yes instead), so it can be run unattended.
#>

$LogDir = "$(Split-Path $PSScriptRoot)\logs"

# A suggestion for a folder on drive D: if that drive is a ready hard disk, else on C:. A DVD,
# a USB stick or a network drive D: is not suggested: unattended, the suggestion is taken as it is.
function Get-DefaultBase {
    $d = New-Object System.IO.DriveInfo('D')
    if ($d.IsReady -and ($d.DriveType -eq [System.IO.DriveType]::Fixed)) { return 'D:\' }
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
        try {
            $answer = [IO.Path]::GetFullPath($answer)
        } catch {
            if ($Unattended) { throw "Invalid folder path '$answer': $($_.Exception.Message)" }
            Write-Host "Invalid folder path: $($_.Exception.Message)" -ForegroundColor Yellow
            continue
        }
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

# The installation folder of the newest Visual Studio, with or without the C++ tools, or $null.
function Get-AnyVsInstance {
    if (-not (Test-Path "$vswhereDir\vswhere.exe")) { return $null }
    return (& "$vswhereDir\vswhere.exe" -latest -products * -property installationPath)
}

# Adds components to an installed Visual Studio with its installer. Returns $true on success.
function Add-VsComponents([string]$Instance, [string[]]$Components, [string]$Log) {
    Write-Host "Adds to the Visual Studio in $Instance`:"
    $Components | ForEach-Object { Write-Host "  $_" }
    Write-Host 'Close Visual Studio first. Windows asks for administrator rights.'
    if (-not (Read-YesNo 'Add them now?' $false)) { return $false }
    $installer = "$vswhereDir\setup.exe"
    $arguments = @('modify', '--installPath', "`"$Instance`"", '--passive', '--norestart') +
                 ($Components | ForEach-Object { '--add', $_ })
    Add-LogLine $Log "# $(Get-Stamp) `"$installer`" $($arguments -join ' ')"
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    Add-LogLine $Log "# Exit code: $($process.ExitCode)"
    # 3010: done, a restart of Windows is needed to finish.
    if ($process.ExitCode -eq 3010) { Write-Host 'Done; restart Windows when convenient.' -ForegroundColor Yellow }
    return ($process.ExitCode -in 0, 3010)
}

# Downloads a file; curl.exe (Windows 10 1803 and later) is much faster than Invoke-WebRequest in Windows PowerShell 5.1.
# Downloads a file and optionally logs to $Log.
function Save-Download([string]$Url, [string]$OutFile, [string]$Log = '') {
    Write-Host "Downloading $Url"
    if ($Log) { Add-LogLine $Log "# $(Get-Stamp) Download $Url" }
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        # --progress-bar: one bar with the percentage instead of curl's table of numbers.
        & curl.exe --location --fail --retry 3 --progress-bar --output $OutFile $Url
        $ok = ($LASTEXITCODE -eq 0)
    } else {
        # The progress bar slows Invoke-WebRequest down by orders of magnitude.
        $oldProgress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $OutFile
            $ok = $true
        } catch {
            Write-Host $_.Exception.Message -ForegroundColor Red
            $ok = $false
        } finally {
            $ProgressPreference = $oldProgress
        }
    }
    if (-not $ok) {
        # A partial file would look like a finished download on the next run.
        if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile }
        Write-Host "Download failed: $Url" -ForegroundColor Red
    }
    return $ok
}

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

# Runs a command line through cmd (in the Visual Studio developer environment with -InVsEnv) and
# shows its output while it runs; the output also goes to $Log. Returns the exit code.
# Code page 65001 and a UTF-8 console encoding make the localized messages come out right: wsl.exe
# writes UTF-8 (WSL_UTF8), which PowerShell would otherwise read as the OEM code page ("ÔÇ×", "├╝").
function Invoke-Shown([string]$CommandLine, [string]$Log, [switch]$InVsEnv) {
    if ($InVsEnv -and -not $Vs) {
        throw 'No Visual Studio installation with the C++ tools found.'
    }
    Add-LogLine $Log "`r`n# $(Get-Stamp) Command: $CommandLine"
    Write-Host "> $CommandLine" -ForegroundColor DarkGray
    $full = if ($InVsEnv) { "call `"$Vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $CommandLine" } else { $CommandLine }
    $tmpCmd = Join-Path ([IO.Path]::GetTempPath()) ('st-cmd-' + [guid]::NewGuid().ToString('N') + '.cmd')
    [IO.File]::WriteAllLines($tmpCmd, @('@echo off', 'chcp 65001 >nul', $full), $Utf8)
    $writer = New-Object IO.StreamWriter($Log, $true, $Utf8)
    $writer.AutoFlush = $true
    $oldEncoding = [Console]::OutputEncoding
    # Without a console (e.g. in the ISE) the encoding can't be set; the output then may show
    # umlauts wrongly, nothing else.
    try { [Console]::OutputEncoding = $Utf8 } catch {}
    try {
        # cmd merges stderr into stdout itself, so PowerShell only sees plain output lines.
        # While wsl.exe runs, a line feed in the console no longer returns the cursor to the first
        # column, so each line would start where the one before ended (a staircase). The carriage
        # return in front of every line, and one after the last, sets the column right.
        cmd /c "`"$tmpCmd`" 2>&1" | ForEach-Object {
            $line = "$_"
            Write-Host "`r$line"
            $writer.WriteLine($line)
        }
        $rc = $LASTEXITCODE
        Write-Host "`r" -NoNewline
    } finally {
        $writer.Close()
        Remove-Item -LiteralPath $tmpCmd -ErrorAction SilentlyContinue
        try { [Console]::OutputEncoding = $oldEncoding } catch {}
    }
    Add-LogLine $Log "# Exit code: $rc"
    return $rc
}

# The step menu. $Info: lines shown under the title, or a scriptblock that returns them (evaluated
# each time the menu is shown). $Steps: hashtables with Title, Done (scriptblock: $true when the
# step's work is already there; shown as [done] or [missing], left out for a step that only checks)
# and Run (scriptblock: $true on success). Runs $Selected (numbers,
# 0 = all) without the menu when it is given. $Extra: hashtables with Key, Title and Run (a
# scriptblock that returns lines to show), listed after the steps; not part of 0 nor of -Steps.
function Invoke-StepMenu([string]$Title, [object]$Info, [object[]]$Steps, [int[]]$Selected, [object[]]$Extra = @()) {
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
    if ($Selected.Count -gt 0) {
        Write-Host $Title -ForegroundColor Cyan
        $lines = if ($Info -is [scriptblock]) { & $Info } else { $Info }
        foreach ($line in $lines) { Write-Host $line }
        return (& $runSteps $Selected)
    }

    while ($true) {
        Write-Host ''
        Write-Host $Title -ForegroundColor Cyan
        $lines = if ($Info -is [scriptblock]) { & $Info } else { $Info }
        foreach ($line in $lines) { Write-Host $line }
        Write-Host ''
        Write-Host '  0  All steps in order'
        for ($i = 0; $i -lt $Steps.Count; $i++) {
            $mark = if (-not $Steps[$i].Done) { '' } elseif (& $Steps[$i].Done) { '  [done]' } else { '  [missing]' }
            Write-Host ("  {0}  {1}{2}" -f ($i + 1), $Steps[$i].Title, $mark)
        }
        foreach ($item in $Extra) { Write-Host ("  {0}  {1}" -f $item.Key, $item.Title) }
        Write-Host '  Q  Quit'
        $typed = (Read-Host 'Step(s), e.g. 0 or 2,4').Trim()
        if ($typed -match '^[qQ]$') { return $true }
        $chosen = @($Extra | Where-Object { $_.Key -eq $typed })
        if ($chosen.Count) {
            Write-Host ''
            foreach ($line in (& $chosen[0].Run)) { Write-Host $line }
            continue
        }
        $numbers = @($typed -split '[,\s]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        if (-not $numbers.Count) { continue }
        [void](& $runSteps $numbers)
    }
}

# The version number a tool prints ("graphify 0.9.112", "uv 0.13.9"), or ''. Graphviz's dot
# prints it for -V, and to stderr ("dot - graphviz version 16.1.1 (...)").
function Get-ToolVersion([string]$Exe, [string]$Arguments = '--version 2>nul') {
    if (-not $Exe) { return '' }
    $line = cmd /c "`"$Exe`" $Arguments" | Select-Object -First 1
    if ("$line" -match '(\d+\.\d+\.\d+)') { return $Matches[1] }
    return ''
}

# --- winget & uv ------------------------------------------------------------------------------

# Runs winget directly in the console (so agreements and prompts can be answered) and logs the call.
function Invoke-Winget {
    param(
        [string]$Subcommand = 'install',
        [Parameter(Mandatory)][string]$Id,
        [string[]]$ExtraArgs = @(),
        [Parameter(Mandatory)][string]$Log
    )
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw 'winget (App Installer) is not available.'
    }
    $wingetArgs = @($Subcommand, '--id', $Id, '--exact', '--source', 'winget') + $ExtraArgs
    Add-LogLine $Log "# $(Get-Stamp) winget $($wingetArgs -join ' ')"
    # Run directly in the console, so the questions of winget can be answered.
    & winget @wingetArgs
    $rc = $LASTEXITCODE
    Add-LogLine $Log "# Exit code: $rc"
    return $rc
}

# Installs uv with winget unless it is there. $Purpose says what uv is needed for, shown before
# installing. Returns $true when uv is there afterwards.
function Install-Uv([string]$Purpose, [string]$Log) {
    $uv = Find-Uv
    if (-not $uv) {
        Write-Host "Installs uv with winget. $Purpose"
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
            Write-Host 'winget (App Installer) is not available. Install uv from https://docs.astral.sh/uv/.' -ForegroundColor Yellow
            return $false
        }
        if (-not (Read-YesNo 'Install now?' $false)) { return $false }
        [void](Invoke-Winget -Subcommand 'install' -Id 'astral-sh.uv' -Log $Log)
        Update-Path
        $uv = Find-Uv
        if (-not $uv) { Write-Host 'uv is installed, but not found. Open a new window.' -ForegroundColor Red; return $false }
    }
    Write-Host "uv: $uv ($(Get-ToolVersion $uv))"
    return $true
}

function Test-UvDirs {
    return ([Environment]::GetEnvironmentVariable('UV_PYTHON_INSTALL_DIR', 'User') -and
            [Environment]::GetEnvironmentVariable('UV_TOOL_DIR', 'User'))
}

# The folder uv keeps its Pythons ($Kind 'python') or tools ('tool') in now, or ''.
function Get-UvDirInUse([string]$Uv, [string]$Kind) {
    return "$(cmd /c "`"$Uv`" $Kind dir 2>nul" | Select-Object -First 1)".Trim()
}

# Sets UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR for the user account (see $UvHome in common.ps1),
# unless they are set already. $IfDeclined is shown when the user answers no: what then does not
# work, and how to set them later. Returns $true when both are set afterwards.
function Set-UvDirs([string]$IfDeclined, [string]$Log) {
    $uv = Find-Uv
    if (-not $uv) { Write-Host 'uv is missing; install it first.' -ForegroundColor Red; return $false }
    $dirs = @(@{ Name = 'UV_PYTHON_INSTALL_DIR'; Kind = 'python'; Value = "$UvHome\python" },
              @{ Name = 'UV_TOOL_DIR'; Kind = 'tool'; Value = "$UvHome\tools" })
    # Folders set before, by this setup or by the user, are kept.
    $missing = @($dirs | Where-Object { -not [Environment]::GetEnvironmentVariable($_.Name, 'User') })
    foreach ($item in $dirs) {
        if ($missing -notcontains $item) {
            Write-Host "$($item.Name) = $([Environment]::GetEnvironmentVariable($item.Name, 'User')) (set before, kept)"
        }
    }
    if (-not $missing.Count) { return $true }

    Write-Host 'uv is to keep its Pythons and tools outside AppData: a program installed as an app package,'
    Write-Host 'such as the Claude desktop app, has its writes there redirected, and uv then cannot find the'
    Write-Host 'Python it has just installed. Sets for your user account:'
    foreach ($item in $missing) { Write-Host "  $($item.Name) = $($item.Value)" }
    # What uv has in the folders it uses now, it no longer sees once they are moved.
    $inUse = @(foreach ($item in $missing) {
            $dir = Get-UvDirInUse $uv $item.Kind
            if ($dir -and (Test-Path -LiteralPath $dir) -and
                (Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) { $dir }
        })
    foreach ($dir in $inUse) {
        Write-Host "uv has Pythons or tools in $dir; after this, uv no longer sees them." -ForegroundColor Yellow
    }
    if (-not (Read-YesNo 'Set them now?' ($inUse.Count -eq 0))) {
        Write-Host "Not set. $IfDeclined" -ForegroundColor Yellow
        return $false
    }
    foreach ($item in $missing) {
        [Environment]::SetEnvironmentVariable($item.Name, $item.Value, 'User')
        [Environment]::SetEnvironmentVariable($item.Name, $item.Value, 'Process')
        Add-LogLine $Log "# $(Get-Stamp) $($item.Name)=$($item.Value) (user environment)"
    }
    Write-Host 'Set for your user account; windows opened from now on see them.'
    return $true
}

# Adds the PATH entries from the registry that this window does not have yet. A window keeps the
# PATH it was opened with, so a tool installed since then (Git by 01-setup-windows.ps1, say)
# would count as missing. The window's own entries stay, e.g. those of a Developer PowerShell.
function Update-Path {
    $have = @($env:Path -split ';' | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') })
    $new = @(foreach ($scope in 'Machine', 'User') {
            [Environment]::GetEnvironmentVariable('Path', $scope) -split ';' | Where-Object { $_ }
        }) | Where-Object { $have -notcontains $_.TrimEnd('\') } | Select-Object -Unique
    if ($new) { $env:Path = (@($env:Path.TrimEnd(';')) + $new) -join ';' }
}
Update-Path
