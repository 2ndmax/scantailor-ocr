<#
.SYNOPSIS
  Setup 6: the MCP server in tools\mcp, and its entry in Claude Code, Claude Desktop and
  Antigravity.

.DESCRIPTION
  The MCP server lets an agent run scantailor-ocr-cli (see tools\mcp\README.md). This setup
  installs what the server needs and enters it in the clients found on this PC. It shows the
  steps in a menu; each can be run on its own, 0 runs all of them in order, 9 shows what is
  there and where the server is entered:
    1. The command line tool: finds scantailor-ocr-cli.exe (-Cli, SCANTAILOR_OCR_CLI,
       build\scantailor-ocr-cli.exe of this repository, PATH) and checks that it is new enough.
       Setup 1 builds it.
    2. uv, with winget, at least version 0.13.9. uv runs the server with a Python of its own.
    3. Sets UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR for your user account (as setup 5 does): the
       Claude desktop app is an app package, and uv started by it would not find its Python
       below AppData.
    4. The server: "uv sync" in tools\mcp installs Python and the server's packages from
       uv.lock, then the server is started once with --help.
    5. Claude Code (the claude command and the Code tab of the Claude desktop app): with
       "claude mcp add" when the claude command is there, else in %USERPROFILE%\.claude.json.
       For all projects, or for one folder only.
    6. Claude Desktop (the chat of the Claude desktop app): in claude_desktop_config.json.
    7. Antigravity: in %USERPROFILE%\.gemini\config\mcp_config.json (or the older
       .gemini\antigravity\mcp_config.json, when only that one exists).
  Steps 5 to 7 ask for the folders the server may work in (--allow-root) once and suggest the
  ones of an entry made before. A client that is not installed is skipped. Before a file is
  changed, the client must be closed, and a copy of the file is kept as <file>.<date-time>.bak.
  The output also goes to logs\06-setup-mcp.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\06-setup-mcp.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER Yes
  With -Steps: answer every question with yes.

.PARAMETER AllowRoot
  The folders the server may work in. Without it, the folders of an entry made before are
  taken; unattended, steps 5 to 7 fail when there are none.

.PARAMETER Cli
  The scantailor-ocr-cli.exe to use, instead of searching for it.

.PARAMETER ClaudeScope
  With -Steps: 'user' (default) enters the server in Claude Code for all projects, 'local' for
  this repository's folder only.
#>
param(
    [int[]]$Steps = @(),
    [switch]$Yes,
    [string[]]$AllowRoot = @(),
    [string]$Cli = '',
    [ValidateSet('user', 'local')]
    [string]$ClaudeScope = 'user'
)
. "$PSScriptRoot\..\support\common.ps1"
. "$PSScriptRoot\support\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\06-setup-mcp.log"
New-Log $log 'setup 6: MCP server' -KeepPrevious

# The name of the entry in the clients, and the program uv starts.
$ServerName = 'scantailor-ocr'
$ServerProgram = 'scantailor-ocr-mcp'
$ServerDir = "$Root\tools\mcp"
# The oldest uv the server is tested with (tools\mcp\README.md, pyproject.toml).
$UvMinVersion = '0.13.9'
$ConfigHelper = "$PSScriptRoot\support\mcp-config.py"

Write-Intro 'ScanTailor OCR - set up the MCP server' @(
    'This script installs what the MCP server in tools\mcp needs and enters the server in Claude'
    'Code, Claude Desktop and Antigravity, as far as they are installed. An agent can then run'
    'scantailor-ocr-cli through it. It shows the steps in a menu; nothing is changed without asking.'
)

# --- Helpers ----------------------------------------------------------------------------------

function Format-FolderPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.Length -gt 3) { return $full.TrimEnd('\') }
    return $full
}

# Quotes an argument for a cmd command line when it needs it.
function ConvertTo-CmdArg([string]$Text) {
    if ($Text -eq '') { return '""' }
    if ($Text -notmatch '[\s"&|<>^()]') { return $Text }
    $escaped = [regex]::Replace($Text, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return "`"$escaped`""
}

# The CLI: -Cli, SCANTAILOR_OCR_CLI (window or user account), the build of this repository, PATH.
function Find-McpCli {
    if ($Cli) { return $Cli }
    $fromEnv = @($env:SCANTAILOR_OCR_CLI, [Environment]::GetEnvironmentVariable('SCANTAILOR_OCR_CLI', 'User'))
    return (Find-Tool 'scantailor-ocr-cli' ($fromEnv + @("$Root\build\scantailor-ocr-cli.exe")))
}

# '' when the CLI answers --profile-list with schema version 1 (what the server checks at its
# start), else why not.
function Test-McpCli([string]$Exe) {
    if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) { return 'not found' }
    $output = cmd /c "`"$Exe`" --profile-list 2>nul"
    if ($LASTEXITCODE -ne 0) { return "--profile-list ended with exit code $LASTEXITCODE (too old?)" }
    try { $json = ($output -join "`n") | ConvertFrom-Json } catch { return '--profile-list printed no JSON (too old?)' }
    if ($json.schemaVersion -ne 1) { return "--profile-list has schema version '$($json.schemaVersion)', not 1" }
    return ''
}

# Runs mcp-config.py through uv; returns its output lines. Throws when it fails.
function Invoke-McpConfig([string[]]$Arguments, [switch]$ReadOnly) {
    $uv = Find-Uv
    if (-not $uv) { throw 'uv is missing; run step 2 first.' }
    $uvArgs = @('run', '--no-project', '--quiet')
    if ($ReadOnly) { $uvArgs += '--no-python-downloads' }
    $uvArgs += @('python', $ConfigHelper) + $Arguments
    $hasPrevUtf8 = Test-Path Env:PYTHONUTF8
    $prevUtf8 = $env:PYTHONUTF8
    try {
        $env:PYTHONUTF8 = '1'
        $output = & $uv @uvArgs
    } finally {
        if ($hasPrevUtf8) { $env:PYTHONUTF8 = $prevUtf8 } else { Remove-Item Env:PYTHONUTF8 -ErrorAction SilentlyContinue }
    }
    if ($LASTEXITCODE -ne 0) { throw "mcp-config.py ended with exit code $LASTEXITCODE." }
    return $output
}

# The entry at $Keys in a client's config file, or $null. Never changes the file. Without uv or
# Python nothing can be read, which counts as no entry.
function Get-McpEntry([string]$File, [string[]]$Keys) {
    if (-not $File -or -not (Test-Path -LiteralPath $File) -or -not (Find-Uv)) { return $null }
    try {
        $json = (Invoke-McpConfig (@('get', $File) + $Keys) -ReadOnly) -join "`n"
        return ($json | ConvertFrom-Json)
    } catch {
        return $null
    }
}

# The folders after --allow-root in an entry's arguments.
function Get-EntryRoots([object]$Entry) {
    if (-not $Entry) { return @() }
    $arguments = @($Entry.args)
    return @(for ($i = 0; $i -lt $arguments.Count - 1; $i++) {
            if ($arguments[$i] -eq '--allow-root') { $arguments[$i + 1] }
        })
}

# Waits until no process matching $Filter runs. A client rewrites its config file while it runs
# and would undo the change. Unattended there is no one to close it, so it fails.
function Wait-ClientClosed([string]$Client, [scriptblock]$Filter) {
    while ($true) {
        $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object $Filter)
        if (-not $running.Count) { return $true }
        Write-Host "$Client is running ($(($running | Select-Object -ExpandProperty Name -Unique) -join ', '))." -ForegroundColor Yellow
        Write-Host "Close it first; it would overwrite the change when it saves its settings."
        if ($Unattended -or -not (Read-YesNo 'Closed now? (no: skip)' $true)) { return $false }
    }
}

# --- The folders the server may work in -------------------------------------------------------

$script:Roots = $null

# The folders of entries made before, in all clients, without repeats.
function Get-KnownRoots {
    $entries = @((Get-ClaudeCodeEntries) | ForEach-Object { $_.Entry }) +
               @(Get-McpEntry (Get-DesktopConfig) @('mcpServers', $ServerName)) +
               @(Get-McpEntry (Get-AntigravityConfig) @('mcpServers', $ServerName))
    return @($entries | Where-Object { $_ } | ForEach-Object { Get-EntryRoots $_ } | Select-Object -Unique)
}

# Asks once per run for the allowed folders; -AllowRoot or an entry made before is suggested.
function Get-AllowRoots {
    if ($null -ne $script:Roots) { return $script:Roots }
    $suggested = if ($AllowRoot.Count) { @($AllowRoot) } else { @(Get-KnownRoots) }
    if ($Unattended) {
        if (-not $suggested.Count) { throw 'No allowed folder: give -AllowRoot.' }
        $chosen = $suggested
    } else {
        Write-Host 'The server only works on files inside the folders you allow (--allow-root): the scans,'
        Write-Host 'the projects and the output. Several folders are separated by ";".'
        while ($true) {
            $typed = (Read-Host "Allowed folders [$($suggested -join ';')]").Trim()
            $chosen = if ($typed) { @($typed -split ';' | ForEach-Object { $_.Trim().Trim('"') } | Where-Object { $_ }) } else { $suggested }
            if (-not $chosen.Count) { Write-Host 'Give at least one folder.' -ForegroundColor Yellow; continue }
            $missing = @($chosen | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Container) })
            if (-not $missing.Count) { break }
            Write-Host "Not a folder: $($missing -join ', ')" -ForegroundColor Yellow
        }
    }
    $script:Roots = @($chosen | ForEach-Object { Format-FolderPath $_ })
    Add-LogLine $log "# Allowed folders: $($script:Roots -join '; ')"
    return $script:Roots
}

# The command that starts the server, as every client gets it: uv with its full path (a desktop
# app does not see the PATH of a window) and the CLI in SCANTAILOR_OCR_CLI.
function Get-ServerCommand {
    $uv = Find-Uv
    $arguments = @('run', '--directory', $ServerDir, $ServerProgram)
    foreach ($folder in (Get-AllowRoots)) { $arguments += @('--allow-root', $folder) }
    return @{ Command = $uv; Args = $arguments; Cli = (Find-McpCli) }
}

# Checks what every entry needs; $false with a message when something is missing.
function Test-EntryReady {
    if (-not (Find-Uv)) { Write-Host 'uv is missing; run step 2 first.' -ForegroundColor Red; return $false }
    if (-not (Test-Path "$ServerDir\.venv")) { Write-Host 'The server is not installed; run step 4 first.' -ForegroundColor Red; return $false }
    $problem = Test-McpCli (Find-McpCli)
    if ($problem) { Write-Host "The command line tool: $problem; run step 1 first." -ForegroundColor Red; return $false }
    return $true
}

# Writes the entry into a JSON config file (Claude Desktop, Antigravity, Claude Code without its
# claude command). $Type: Claude Code's "type", else ''.
function Set-JsonEntry([string]$File, [string[]]$Keys, [string]$Type) {
    $server = Get-ServerCommand
    $arguments = @('set', $File) + $Keys + @("--command=$($server.Command)")
    foreach ($a in $server.Args) { $arguments += "--arg=$a" }
    $arguments += "--env=SCANTAILOR_OCR_CLI=$($server.Cli)"
    if ($Type) { $arguments += "--type=$Type" }
    Add-LogLine $log "# $(Get-Stamp) mcp-config.py $($arguments -join ' ')"
    try {
        Invoke-McpConfig $arguments | ForEach-Object { Write-Host $_; Add-LogLine $log $_ }
    } catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
        Add-LogLine $log "# $($_.Exception.Message)"
        return $false
    }
    return $true
}

# Shows an entry made before and asks whether to replace it. $true: go on.
function Confirm-Replace([object]$Entry, [string]$Where) {
    if (-not $Entry) { return $true }
    Write-Host "There is an entry '$ServerName' in $Where already:"
    Write-Host "  $($Entry.command) $(@($Entry.args) -join ' ')"
    return (Read-YesNo 'Replace it?' $true)
}

# --- Step 1: the command line tool ------------------------------------------------------------

$stepCli = {
    $exe = Find-McpCli
    $problem = Test-McpCli $exe
    if ($problem) {
        Write-Host "scantailor-ocr-cli: $problem$(if ($exe) { " ($exe)" })." -ForegroundColor Red
        Write-Host 'Setup 1 (01-setup-windows.ps1) builds it into build\, or give its path with -Cli.'
        return $false
    }
    Write-Host "scantailor-ocr-cli: $exe"
    return $true
}

# --- Step 2: uv -------------------------------------------------------------------------------

$stepUv = {
    if (-not (Install-Uv 'uv runs the MCP server with a Python of its own (step 4).' $log)) { return $false }
    $uv = Find-Uv
    $version = Get-ToolVersion $uv
    if ($version -and ([version]$version -ge [version]$UvMinVersion)) { return $true }
    Write-Host "uv $version is older than $UvMinVersion, which the server needs." -ForegroundColor Yellow
    if (-not (Read-YesNo 'Update it with winget now?' $true)) { return $false }
    Add-LogLine $log "# $(Get-Stamp) winget upgrade --id astral-sh.uv --exact --source winget"
    # Run directly in the console, so the questions of winget can be answered.
    & winget upgrade --id astral-sh.uv --exact --source winget
    Add-LogLine $log "# Exit code: $LASTEXITCODE"
    $version = Get-ToolVersion (Find-Uv)
    if (-not $version -or ([version]$version -lt [version]$UvMinVersion)) {
        Write-Host "uv is still $version." -ForegroundColor Red
        return $false
    }
    Write-Host "uv $version"
    return $true
}

# --- Step 3: the folders of uv ----------------------------------------------------------------

$stepUvDirs = {
    if (-not (Find-Uv)) { Write-Host 'uv is missing; run step 2 first.' -ForegroundColor Red; return $false }
    return (Set-UvDirs 'The Claude desktop app may then not start the server; to set them, run step 3 again and answer yes.' $log)
}

# --- Step 4: the server -----------------------------------------------------------------------

$stepServer = {
    $uv = Find-Uv
    if (-not $uv) { Write-Host 'uv is missing; run step 2 first.' -ForegroundColor Red; return $false }
    Write-Host "Installs Python and the packages of the server from uv.lock into $ServerDir\.venv."
    if (-not (Read-YesNo 'Install now?' $true)) { return $false }
    # --locked: exactly the versions in uv.lock; fails instead of changing the lock file.
    if ((Invoke-Shown "`"$uv`" sync --locked --directory `"$ServerDir`"" $log) -ne 0) { return $false }
    # The way the clients start it, but only to print the options.
    if ((Invoke-Shown "`"$uv`" run --directory `"$ServerDir`" $ServerProgram --help" $log) -ne 0) { return $false }
    Write-Host 'The server starts.'
    return $true
}

# --- Step 5: Claude Code ----------------------------------------------------------------------

function Find-ClaudeCommand {
    return (Find-Tool 'claude' @("$env:USERPROFILE\.local\bin\claude.exe"))
}

# The Claude desktop app: as an app package (Microsoft Store, MSIX) or from the older installer.
function Test-ClaudeApp {
    return ([bool](Get-ChildItem "$env:LOCALAPPDATA\Packages\Claude_*" -Directory -ErrorAction SilentlyContinue) -or
            (Test-Path "$env:LOCALAPPDATA\AnthropicClaude") -or
            [bool](Get-ChildItem "$env:LOCALAPPDATA\Claude-Data\claude_*" -Directory -ErrorAction SilentlyContinue))
}

# Claude Code's settings file; CLAUDE_CONFIG_DIR moves it.
function Get-ClaudeCodeConfig {
    if ($env:CLAUDE_CONFIG_DIR) { return "$env:CLAUDE_CONFIG_DIR\.claude.json" }
    return "$env:USERPROFILE\.claude.json"
}

# Claude Code keys a folder by its path with forward slashes ("D:/books").
function Get-ClaudeProjectKey([string]$Folder) {
    return ($Folder -replace '\\', '/')
}

# The entries in Claude Code: for all projects ('user') and for the folders ('local').
# Searched by mcp-config.py: Windows PowerShell's ConvertFrom-Json fails on the whole file when
# any key in it is empty, and the file holds every folder Claude Code was ever opened in.
function Get-ClaudeCodeEntries {
    $file = Get-ClaudeCodeConfig
    if (-not (Test-Path -LiteralPath $file) -or -not (Find-Uv)) { return @() }
    try {
        $list = (Invoke-McpConfig @('find', $file, $ServerName) -ReadOnly) -join "`n" | ConvertFrom-Json
    } catch {
        return @()
    }
    return @(@($list) | Where-Object { $_ } | ForEach-Object {
            @{ Scope = $_.scope; Folder = $_.folder; Entry = $_.entry } })
}

$stepClaudeCode = {
    $claude = Find-ClaudeCommand
    if (-not $claude -and -not (Test-ClaudeApp)) {
        Write-Host 'Neither the claude command nor the Claude desktop app is installed; skipped.'
        return $true
    }
    if (-not (Test-EntryReady)) { return $false }
    $where = if ($claude) { "the claude command ($claude)" } else { 'the Code tab of the Claude desktop app' }
    Write-Host "Enters the server in Claude Code, used by $where."
    foreach ($e in (Get-ClaudeCodeEntries)) {
        $scope = if ($e.Scope -eq 'user') { 'for all projects' } else { "for $($e.Folder)" }
        Write-Host "Entered before $scope`: $($e.Entry.command) $(@($e.Entry.args) -join ' ')"
    }
    if (-not (Read-YesNo 'Enter it now?' $true)) { return $true }

    $scope = if ($Unattended) { $ClaudeScope } else {
        @('user', 'local')[(Read-Choice 'For which projects?' @(
                    'all projects'
                    'one folder only (asked next)'))]
    }
    $folder = $Root
    if ($scope -eq 'local' -and -not $Unattended) {
        while ($true) {
            $typed = (Read-Host "Folder [$Root]").Trim().Trim('"')
            $folder = if ($typed) { Format-FolderPath $typed } else { $Root }
            if (Test-Path -LiteralPath $folder -PathType Container) { break }
            Write-Host "Not a folder: $folder" -ForegroundColor Yellow
        }
    }
    $server = Get-ServerCommand

    if ($claude) {
        # claude mcp add refuses a name that is there already in the same scope.
        $existing = @(Get-ClaudeCodeEntries | Where-Object {
                $_.Scope -eq $scope -and ($scope -eq 'user' -or $_.Folder -eq (Get-ClaudeProjectKey $folder)) })
        if ($existing.Count -and -not (Confirm-Replace $existing[0].Entry 'Claude Code')) { return $true }
        # A local entry belongs to the folder claude is started in.
        Push-Location $folder
        try {
            if ($existing.Count) {
                [void](Invoke-Shown "`"$claude`" mcp remove --scope $scope $ServerName" $log)
            }
            $command = @("`"$claude`"", 'mcp', 'add', '--scope', $scope,
                         '--env', (ConvertTo-CmdArg "SCANTAILOR_OCR_CLI=$($server.Cli)"),
                         '--transport', 'stdio', $ServerName, '--', (ConvertTo-CmdArg $server.Command)) +
                       @($server.Args | ForEach-Object { ConvertTo-CmdArg $_ })
            $rc = Invoke-Shown ($command -join ' ') $log
        } finally {
            Pop-Location
        }
        return ($rc -eq 0)
    }

    # Without the claude command: the app keeps Claude Code's settings in the same file.
    $file = Get-ClaudeCodeConfig
    $keys = if ($scope -eq 'user') { @('mcpServers', $ServerName) } else {
        @('projects', (Get-ClaudeProjectKey $folder), 'mcpServers', $ServerName)
    }
    if (-not (Confirm-Replace (Get-McpEntry $file $keys) $file)) { return $true }
    # Every Claude Code process (the command, and the one inside the app) and the app itself
    # write this file.
    if (-not (Wait-ClientClosed 'Claude' { $_.Name -eq 'claude' })) { return $false }
    if (-not (Set-JsonEntry $file $keys 'stdio')) { return $false }
    Write-Host 'Claude Code starts the server the next time it is opened.'
    return $true
}

# --- Step 6: Claude Desktop -------------------------------------------------------------------

# claude_desktop_config.json of the Claude desktop app, or ''. Where it is depends on the
# version: in Claude-Data (newer app packages), in the package's redirected AppData (older app
# packages), or in %APPDATA%\Claude (the older installer). The first that exists wins; without
# one, the folder the app uses now.
function Get-DesktopConfig {
    $name = 'claude_desktop_config.json'
    $candidates = @(Get-ChildItem "$env:LOCALAPPDATA\Claude-Data\claude_*\$name" -ErrorAction SilentlyContinue) +
                  @(Get-ChildItem "$env:LOCALAPPDATA\Packages\Claude_*\LocalCache\Roaming\Claude\$name" -ErrorAction SilentlyContinue) +
                  @(Get-Item "$env:APPDATA\Claude\$name" -ErrorAction SilentlyContinue)
    $existing = @($candidates | Where-Object { $_ }) | Select-Object -First 1
    if ($existing) { return $existing.FullName }
    $dataDir = Get-ChildItem "$env:LOCALAPPDATA\Claude-Data\claude_*" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($dataDir) { return "$($dataDir.FullName)\$name" }
    if (Test-ClaudeApp) { return "$env:APPDATA\Claude\$name" }
    return ''
}

$stepDesktop = {
    $file = Get-DesktopConfig
    if (-not $file) { Write-Host 'The Claude desktop app is not installed; skipped.'; return $true }
    if (-not (Test-EntryReady)) { return $false }
    Write-Host "Enters the server for the chat of the Claude desktop app in $file."
    $keys = @('mcpServers', $ServerName)
    $entry = Get-McpEntry $file $keys
    if ($entry) {
        if (-not (Confirm-Replace $entry 'Claude Desktop')) { return $true }
    } elseif (-not (Read-YesNo 'Enter it now?' $true)) { return $true }
    [void](Get-AllowRoots)
    # The app itself; its Claude Code processes (claude-code folder) do not write this file.
    if (-not (Wait-ClientClosed 'The Claude desktop app' { $_.Name -eq 'claude' -and "$($_.Path)" -notmatch '\\claude-code\\|\\\.local\\bin\\' })) { return $false }
    if (-not (Set-JsonEntry $file $keys '')) { return $false }
    Write-Host 'Claude Desktop starts the server the next time it is opened.'
    return $true
}

# --- Step 7: Antigravity ----------------------------------------------------------------------

function Test-Antigravity {
    return ((Test-Path "$env:USERPROFILE\.gemini") -or (Test-Path "$env:LOCALAPPDATA\Programs\Antigravity"))
}

# mcp_config.json of Antigravity, or '' when it is not installed. Its documentation names
# .gemini\config; older versions read .gemini\antigravity, which is taken when only it exists.
function Get-AntigravityConfig {
    if (-not (Test-Antigravity)) { return '' }
    $current = "$env:USERPROFILE\.gemini\config\mcp_config.json"
    $older = "$env:USERPROFILE\.gemini\antigravity\mcp_config.json"
    if (-not (Test-Path -LiteralPath $current) -and (Test-Path -LiteralPath $older)) { return $older }
    return $current
}

$stepAntigravity = {
    $file = Get-AntigravityConfig
    if (-not $file) { Write-Host 'Antigravity is not installed; skipped.'; return $true }
    if (-not (Test-EntryReady)) { return $false }
    Write-Host "Enters the server for Antigravity in $file."
    $keys = @('mcpServers', $ServerName)
    $entry = Get-McpEntry $file $keys
    if ($entry) {
        if (-not (Confirm-Replace $entry 'Antigravity')) { return $true }
    } elseif (-not (Read-YesNo 'Enter it now?' $true)) { return $true }
    [void](Get-AllowRoots)
    if (-not (Wait-ClientClosed 'Antigravity' { $_.Name -eq 'Antigravity' })) { return $false }
    if (-not (Set-JsonEntry $file $keys '')) { return $false }
    Write-Host 'In Antigravity, reload the servers (/mcp) or open it again.'
    return $true
}

# --- 9: what is there -------------------------------------------------------------------------

$status = {
    $cli = Find-McpCli
    $problem = Test-McpCli $cli
    $cliState = if ($problem) { $problem } else { 'ok' }
    "  scantailor-ocr-cli  $cliState  $cli"
    $uv = Find-Uv
    "  uv                  $(if ($uv) { Get-ToolVersion $uv } else { '-' }) (at least $UvMinVersion)  $uv"
    "  UV_PYTHON_INSTALL_DIR = $([Environment]::GetEnvironmentVariable('UV_PYTHON_INSTALL_DIR', 'User'))"
    "  UV_TOOL_DIR           = $([Environment]::GetEnvironmentVariable('UV_TOOL_DIR', 'User'))"
    $server = if (Test-Path "$ServerDir\.venv") { "installed in $ServerDir\.venv" } else { 'not installed (step 4)' }
    "  Server: $server"
    ''
    'Entries of the server:'
    $claude = Find-ClaudeCommand
    if (-not $claude -and -not (Test-ClaudeApp)) {
        '  Claude Code:    not installed'
    } else {
        $entries = @(Get-ClaudeCodeEntries)
        if (-not $entries.Count) { "  Claude Code:    none ($(Get-ClaudeCodeConfig))" }
        foreach ($e in $entries) {
            $scope = if ($e.Scope -eq 'user') { 'all projects' } else { $e.Folder }
            "  Claude Code:    $scope, folders: $((Get-EntryRoots $e.Entry) -join '; ')"
        }
    }
    foreach ($client in @(@{ Name = 'Claude Desktop:'; File = (Get-DesktopConfig) },
                          @{ Name = 'Antigravity:   '; File = (Get-AntigravityConfig) })) {
        if (-not $client.File) { "  $($client.Name) not installed"; continue }
        $entry = Get-McpEntry $client.File @('mcpServers', $ServerName)
        if ($entry) { "  $($client.Name) folders: $((Get-EntryRoots $entry) -join '; ')  ($($client.File))" }
        else { "  $($client.Name) none ($($client.File))" }
    }
}

$menu = @(
    @{ Title = 'The command line tool scantailor-ocr-cli: check'
       Done = { -not (Test-McpCli (Find-McpCli)) }; Run = $stepCli }
    @{ Title = "uv, at least $UvMinVersion`: check or install"
       Done = { $uv = Find-Uv; $v = Get-ToolVersion $uv; [bool]($v -and ([version]$v -ge [version]$UvMinVersion)) }; Run = $stepUv }
    @{ Title = 'Set UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR for your user account'
       Done = { Test-UvDirs }; Run = $stepUvDirs }
    @{ Title = 'The server: Python and its packages (uv sync)'
       Done = { Test-Path "$ServerDir\.venv" }; Run = $stepServer }
    @{ Title = 'Enter it in Claude Code (optional)'
       Done = { [bool]@(Get-ClaudeCodeEntries).Count }; Run = $stepClaudeCode }
    @{ Title = 'Enter it in Claude Desktop (optional)'
       Done = { [bool](Get-McpEntry (Get-DesktopConfig) @('mcpServers', $ServerName)) }; Run = $stepDesktop }
    @{ Title = 'Enter it in Antigravity (optional)'
       Done = { [bool](Get-McpEntry (Get-AntigravityConfig) @('mcpServers', $ServerName)) }; Run = $stepAntigravity }
)
$extra = @(
    @{ Key = '9'; Title = 'Show what is there and where the server is entered'; Run = $status }
)
# Unattended, the overview goes first, as there is no menu to ask for it.
$info = if ($Unattended) { { & $status; "Log: $log" } } else { { "Log: $log" } }
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the MCP server' $info $menu $Steps $extra
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
