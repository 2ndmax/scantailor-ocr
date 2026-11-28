<#
.SYNOPSIS
  Setup 5: the tools of the overview runs in scripts\overview: graphify, clang-uml and Graphviz.

.DESCRIPTION
  Shows the steps in a menu; each can be run on its own, 0 runs all of them in order, 9 shows
  which tools are there, in which version:
    1. uv: installs it with winget (it installs Python and graphify in step 3).
    2. Sets UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR (the folders of uv) for your user account to
       %USERPROFILE%\.local\share\uv, unless they are set already. Asks first; if uv has Pythons
       or tools in the folders it uses now, the default answer is no, as uv no longer sees them
       afterwards. Step 3 needs them: without them, graphify is not installed.
    3. graphify (run 01), as a uv tool with its own Python: the version this setup was tested
       with, or the newest one. Its command goes to %USERPROFILE%\.local\bin, which uv adds to PATH.
    4. /graphify for Claude Code (optional): "graphify install" copies graphify's skill to
       %USERPROFILE%\.claude\skills\graphify and names it in %USERPROFILE%\.claude\CLAUDE.md.
    5. clang-uml (run 02), with its installer from GitHub into C:\Program Files\clang-uml: the
       version this setup was tested with, or the newest one. The download is checked against its
       SHA-256 checksum. Windows asks for administrator rights.
    6. Graphviz (run 03, optional), with winget into C:\Program Files\Graphviz: the version this
       setup was tested with, or the newest one. Run 03 then also draws its graphs as SVG files;
       its web page needs no Graphviz.
  The overview runs find the tools also in a window opened before this setup. The output also
  goes to logs\05-setup-overview.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\05-setup-overview.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.

.PARAMETER Latest
  With -Steps: the newest graphify, clang-uml and Graphviz instead of the tested versions.

.PARAMETER Yes
  With -Steps: answer every question with yes.
#>
param(
    [int[]]$Steps = @(),
    [switch]$Latest,
    [switch]$Yes
)
. "$PSScriptRoot\..\support\common.ps1"
. "$PSScriptRoot\support\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\05-setup-overview.log"
New-Log $log 'setup 5: overview tools' -KeepPrevious

# The versions this setup was made and tested with (2026-11-18).
$GraphifyVersion = '0.9.112'
# graphify needs Python 3.11 or later; uv downloads this one for graphify alone.
$GraphifyPython = '3.13'
$ClangUmlVersion = '0.6.3'
$ClangUmlSha256 = '9e4f3881ac1b003bf587a56e433e440e7e60a28712129ead2af83e41ec2e2886'
$ClangUmlRepo = 'bkryza/clang-uml'
$ClaudeSkill = "$env:USERPROFILE\.claude\skills\graphify\SKILL.md"
$GraphvizVersion = '16.1.1'
$GraphvizId = 'Graphviz.Graphviz'

Write-Intro 'ScanTailor OCR - set up the overview tools' @(
    'This script installs graphify, clang-uml and Graphviz, which the overview runs in'
    'scripts\overview use to map the code as a graph and to draw class and sequence diagrams and'
    'the targets of the build. It shows the steps in a menu; nothing is installed without asking.'
)

# Asks which version to install: the tested one, or the newest. Unattended: -Latest decides.
function Read-VersionChoice([string]$Tool, [string]$Tested) {
    if ($Unattended) { return [bool]$Latest }
    return ((Read-Choice "Which version of $Tool?" @(
        "the version this setup was tested with ($Tested)"
        'the newest one (not tested with this setup)')) -eq 1)
}

# --- Step 1: uv -------------------------------------------------------------------------------

$stepUv = {
    return (Install-Uv 'uv then installs graphify with a Python of its own (step 3).' $log)
}

# --- Step 2: the folders of uv ----------------------------------------------------------------

$stepUvDirs = {
    if (-not (Find-Uv)) { Write-Host 'uv is missing; run step 1 first.' -ForegroundColor Red; return $false }
    return (Set-UvDirs 'graphify (step 3) is installed only with these folders; to install it, run step 2 again and answer yes.' $log)
}

# --- Step 3: graphify -------------------------------------------------------------------------

$stepGraphify = {
    $uv = Find-Uv
    if (-not $uv) { Write-Host 'uv is missing; run step 1 first.' -ForegroundColor Red; return $false }
    if (-not (Test-UvDirs)) { Write-Host 'UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR are not set, and graphify is installed only with them; run step 2 first and answer yes.' -ForegroundColor Red; return $false }
    $installed = Get-ToolVersion (Find-Graphify)
    $newest = Read-VersionChoice 'graphify' $GraphifyVersion
    if ($installed -and -not $newest -and $installed -eq $GraphifyVersion) {
        Write-Host "graphify $installed is there: $(Find-Graphify)"
        return $true
    }
    if ($installed) { Write-Host "graphify $installed is there." }
    $package = if ($newest) { 'graphifyy' } else { "graphifyy==$GraphifyVersion" }
    Write-Host "Installs $package (the package of graphify is called graphifyy) with Python $GraphifyPython."
    if (-not (Read-YesNo 'Install now?' $true)) { return $false }
    # --force: replaces an installed version; --upgrade: the newest, also when one is there.
    $upgrade = if ($newest) { ' --upgrade' } else { '' }
    $install = "`"$uv`" tool install --force$upgrade --python $GraphifyPython $package"
    if ((Invoke-Shown $install $log) -ne 0) { return $false }
    # Adds the folder of the tool commands to PATH (user account), unless it is there.
    if ((Invoke-Shown "`"$uv`" tool update-shell" $log) -ne 0) { return $false }
    Update-Path
    $graphify = Find-Graphify
    if (-not $graphify) { Write-Host "graphify is not in $(Get-UvToolBin)." -ForegroundColor Red; return $false }
    Write-Host "graphify $(Get-ToolVersion $graphify): $graphify"
    return $true
}

# --- Step 4: /graphify for Claude Code --------------------------------------------------------

$stepClaude = {
    $graphify = Find-Graphify
    if (-not $graphify) { Write-Host 'graphify is missing; run step 3 first.' -ForegroundColor Red; return $false }
    Write-Host 'Lets Claude Code build and ask the graph with /graphify: copies the skill of graphify to'
    Write-Host "$(Split-Path $ClaudeSkill) and adds a short section naming it to"
    Write-Host "$env:USERPROFILE\.claude\CLAUDE.md. Nothing in the repository changes."
    if (-not (Read-YesNo 'Set it up?' $false)) { return $true }
    if ((Invoke-Shown "`"$graphify`" install" $log) -ne 0) { return $false }
    return (Test-Path $ClaudeSkill)
}

# --- Step 5: clang-uml ------------------------------------------------------------------------

# Version, URL and SHA-256 checksum of the Windows installer of the newest release, or $null.
function Get-NewestClangUml {
    try {
        $release = Invoke-RestMethod -UseBasicParsing "https://api.github.com/repos/$ClangUmlRepo/releases/latest"
    } catch {
        Write-Host "The newest release of clang-uml is not known: $($_.Exception.Message)" -ForegroundColor Red
        return $null
    }
    $asset = @($release.assets | Where-Object { $_.name -like 'clang-uml-*-win64.exe' }) | Select-Object -First 1
    if (-not $asset) { Write-Host "Release $($release.tag_name) of clang-uml has no Windows installer." -ForegroundColor Red; return $null }
    # GitHub gives the checksum of each asset as "sha256:<hex>".
    $sha256 = if ("$($asset.digest)" -match '^sha256:([0-9a-f]{64})$') { $Matches[1].ToLowerInvariant() } else { '' }
    return @{ Version = "$($release.tag_name)"; Url = $asset.browser_download_url; Sha256 = $sha256 }
}

$stepClangUml = {
    $installed = Get-ToolVersion (Find-ClangUml)
    $newest = Read-VersionChoice 'clang-uml' $ClangUmlVersion
    $release = if ($newest) { Get-NewestClangUml } else {
        @{ Version = $ClangUmlVersion; Sha256 = $ClangUmlSha256
           Url = "https://github.com/$ClangUmlRepo/releases/download/$ClangUmlVersion/clang-uml-$ClangUmlVersion-win64.exe" }
    }
    if (-not $release) { return $false }
    if ($installed -eq $release.Version) {
        Write-Host "clang-uml $installed is there: $(Find-ClangUml)"
        return $true
    }
    if ($installed) { Write-Host "clang-uml $installed is there." }
    if (-not $release.Sha256) {
        Write-Host "GitHub gives no checksum for clang-uml $($release.Version); it is not installed." -ForegroundColor Red
        return $false
    }
    Write-Host "Installs clang-uml $($release.Version) with its installer into $env:ProgramFiles\clang-uml."
    Write-Host 'Windows asks for administrator rights.'
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    $downloadDir = "$env:TEMP\scantailor-ocr-setup"
    New-Item -ItemType Directory -Force $downloadDir | Out-Null
    $installer = "$downloadDir\$(Split-Path $release.Url -Leaf)"
    if (-not (Save-Download $release.Url $installer $log)) { return $false }
    try {
        $hash = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLowerInvariant()
        $expected = $release.Sha256.ToLowerInvariant()
        Add-LogLine $log "# SHA-256: $hash (expected $expected)"
        if ($hash -ne $expected) {
            Write-Host "The download has the wrong checksum ($hash); it is deleted." -ForegroundColor Red
            return $false
        }
        # /S: NSIS silent switch; the installer runs without its dialogs.
        Add-LogLine $log "# $(Get-Stamp) `"$installer`" /S"
        $process = Start-Process -FilePath $installer -ArgumentList '/S' -Verb RunAs -Wait -PassThru
        Add-LogLine $log "# Exit code: $($process.ExitCode)"
    } finally {
        Remove-Item -LiteralPath $installer -ErrorAction SilentlyContinue
    }
    $found = Find-ClangUml
    $version = Get-ToolVersion $found
    if ($version -ne $release.Version) {
        Write-Host "clang-uml $($release.Version) is not in $env:ProgramFiles\clang-uml (found: '$version')." -ForegroundColor Red
        return $false
    }
    Write-Host "clang-uml $version`: $found"
    return $true
}

# --- Step 6: Graphviz -------------------------------------------------------------------------

$stepGraphviz = {
    $installed = Get-ToolVersion (Find-Graphviz) '-V 2>&1'
    Write-Host 'Graphviz lets run 03 draw its graphs as SVG files as well; its web page draws them without.'
    $newest = Read-VersionChoice 'Graphviz' $GraphvizVersion
    if ($installed -and -not $newest -and $installed -eq $GraphvizVersion) {
        Write-Host "Graphviz $installed is there: $(Find-Graphviz)"
        return $true
    }
    if ($installed) { Write-Host "Graphviz $installed is there." }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host 'winget (App Installer) is not available. Install Graphviz from https://graphviz.org/download/.' -ForegroundColor Yellow
        return $false
    }
    $which = if ($newest) { 'the newest version of Graphviz' } else { "Graphviz $GraphvizVersion" }
    Write-Host "Installs $which with winget into $env:ProgramFiles\Graphviz. Windows asks for administrator rights."
    if (-not (Read-YesNo 'Install now?' $false)) { return $true }
    # winget updates an installed Graphviz to the newest; --force lets it change to the tested version.
    $extraArgs = if (-not $newest) { @('--version', $GraphvizVersion, '--force') } else { @() }
    [void](Invoke-Winget -Subcommand 'install' -Id $GraphvizId -ExtraArgs $extraArgs -Log $log)
    Update-Path
    $found = Find-Graphviz
    $version = Get-ToolVersion $found '-V 2>&1'
    if (-not $found -or (-not $newest -and $version -ne $GraphvizVersion)) {
        Write-Host "Graphviz $which is not in $env:ProgramFiles\Graphviz (found: '$version')." -ForegroundColor Red
        return $false
    }
    Write-Host "Graphviz $version`: $found"
    return $true
}

# --- 9: what is there -------------------------------------------------------------------------

$status = {
    $tools = @(
        @{ Name = 'uv'; Path = (Find-Uv); Tested = '' }
        @{ Name = 'graphify'; Path = (Find-Graphify); Tested = $GraphifyVersion }
        @{ Name = 'clang-uml'; Path = (Find-ClangUml); Tested = $ClangUmlVersion }
        @{ Name = 'Graphviz'; Path = (Find-Graphviz); Tested = $GraphvizVersion; Arguments = '-V 2>&1' }
    )
    'The tools, their versions (tested with this setup) and where they are:'
    foreach ($t in $tools) {
        $version = if (-not $t.Path) { '-' } elseif ($t.Arguments) { Get-ToolVersion $t.Path $t.Arguments } else { Get-ToolVersion $t.Path }
        $tested = if ($t.Tested) { "($($t.Tested))" } else { '' }
        '  {0,-10} {1,-9} {2,-10} {3}' -f $t.Name, $version, $tested, $t.Path
    }
    "  UV_PYTHON_INSTALL_DIR = $([Environment]::GetEnvironmentVariable('UV_PYTHON_INSTALL_DIR', 'User'))"
    "  UV_TOOL_DIR           = $([Environment]::GetEnvironmentVariable('UV_TOOL_DIR', 'User'))"
    $skill = if (Test-Path $ClaudeSkill) { $ClaudeSkill } else { 'not set up (step 4)' }
    "  /graphify for Claude Code: $skill"
}

$menu = @(
    # Done when the tool or the setting is there, also when it was not this setup that put it there.
    @{ Title = 'uv (graphify is installed with it in step 3): check or install'
       Done = { [bool](Find-Uv) }; Run = $stepUv }
    @{ Title = 'Set UV_PYTHON_INSTALL_DIR and UV_TOOL_DIR for your user account'
       Done = { Test-UvDirs }; Run = $stepUvDirs }
    @{ Title = "graphify (run 01), tested version $GraphifyVersion or the newest"
       Done = { [bool](Find-Graphify) }; Run = $stepGraphify }
    @{ Title = '/graphify for Claude Code (optional)'
       Done = { Test-Path $ClaudeSkill }; Run = $stepClaude }
    @{ Title = "clang-uml (run 02), tested version $ClangUmlVersion or the newest"
       Done = { [bool](Find-ClangUml) }; Run = $stepClangUml }
    @{ Title = "Graphviz (run 03, optional: SVG files), tested version $GraphvizVersion or the newest"
       Done = { [bool](Find-Graphviz) }; Run = $stepGraphviz }
)
$extra = @(
    @{ Key = '9'; Title = 'Show which tools are there, in which version'; Run = $status }
)
# Unattended, the overview goes first, as there is no menu to ask for it.
$info = if ($Unattended) { { & $status; "Log: $log" } } else { { "Log: $log" } }
$ok = Invoke-StepMenu 'ScanTailor OCR - set up the overview tools' $info $menu $Steps $extra
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
