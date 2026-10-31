<#
.SYNOPSIS
  Setup 4: WSL with Ubuntu 24.04 and GCC, for the GCC build with UBSan and ASan.

.DESCRIPTION
  Shows the steps in a menu; each can be run on its own, 0 runs all of them in order:
    1. WSL: installs the Windows Subsystem for Linux itself, without a Linux ("wsl --install
       --no-distribution"). Needs administrator rights, virtualization switched on in the
       BIOS/UEFI, and a restart of Windows afterwards.
    2. Ubuntu 24.04: installs the Linux into a folder you choose ("wsl --install --distribution
       Ubuntu-24.04 --location <folder> --no-launch") and starts it once; Ubuntu then asks for a
       Linux user name and password, which need not match the Windows ones. If Windows itself
       runs in a virtual machine, its host must allow nested virtualization.
    3. Packages: GCC, CMake, Ninja, Qt 6 and the libraries, as listed in support\ubuntu-packages.txt
       (the same as the Ubuntu job of the CI). apt-get runs as root, so no password is asked.
    4. Check: builds a small file with GCC, UBSan and ASan and runs it; UBSan must report the
       signed overflow in it.
  The output also goes to logs\04-setup-wsl-gcc.log.

    powershell -ExecutionPolicy Bypass -File scripts\setup\04-setup-wsl-gcc.ps1

.PARAMETER Steps
  Run these steps without the menu (0 = all), taking the default answer of every question.
  Step 2 is not possible this way, as Ubuntu asks for a user name.

.PARAMETER DistroDir
  The folder of Ubuntu. Default: asked, suggesting D:\WSL\Ubuntu-24.04 if drive D: exists, else
  C:\WSL\Ubuntu-24.04.

.PARAMETER Yes
  With -Steps: answer every question with yes (install).
#>
param(
    [int[]]$Steps = @(),
    [string]$DistroDir = '',
    [switch]$Yes
)
. "$PSScriptRoot\..\support\common.ps1"
. "$PSScriptRoot\support\setup-common.ps1"
$Unattended = $Steps.Count -gt 0
$AssumeYes = [bool]$Yes
$log = "$LogDir\04-setup-wsl-gcc.log"
$checkDir = "$LogDir\04-setup-wsl-gcc-check"

Write-Intro "ScanTailor OCR - set up WSL with $WslDistro and GCC" @(
    'This script installs the Windows Subsystem for Linux (WSL), Ubuntu in it, and GCC with the'
    'packages ScanTailor OCR needs to build there. It shows the steps in a menu; nothing is'
    'installed without asking.'
    ''
    'Step 1 needs administrator rights and a restart of Windows. Step 2 asks for the folder of'
    'Ubuntu (about 5 GB with the packages of step 3).'
)
$DistroDir = if ($DistroDir) { [IO.Path]::GetFullPath($DistroDir).TrimEnd('\') }
             else { Join-Path (Get-DefaultBase) "WSL\$WslDistro" }
New-Log $log 'setup 4: WSL, Ubuntu and GCC' -KeepPrevious

# Set by step 1 after installing WSL: the Linux can only be installed after a restart of Windows.
$RestartNeeded = $false

# WSL needs the virtualization of the processor. While a hypervisor runs (Hyper-V, or WSL
# already), Windows reports it as switched off, so that counts as switched on.
function Test-Virtualization {
    if ((Get-CimInstance Win32_ComputerSystem).HypervisorPresent) { return $true }
    return [bool](Get-CimInstance Win32_Processor | Select-Object -First 1).VirtualizationFirmwareEnabled
}

# Windows inside a virtual machine: WSL 2 then needs nested virtualization, which only the host
# can allow. Seen from inside, it can't be told whether the host does, so this is only a warning.
function Write-VirtualMachineNote {
    $system = Get-CimInstance Win32_ComputerSystem
    if ("$($system.Manufacturer) $($system.Model)" -notmatch 'Virtual Machine|VMware|VirtualBox|KVM|QEMU') { return }
    Write-Host 'Windows runs in a virtual machine. WSL 2 then needs nested virtualization, which the' -ForegroundColor Yellow
    Write-Host 'host must allow, else Ubuntu fails with HCS_E_HYPERV_NOT_INSTALLED. With Hyper-V, on' -ForegroundColor Yellow
    Write-Host 'the host, in PowerShell as administrator:' -ForegroundColor Yellow
    Write-Host '  1. Get-VM must be known there. If it is not, enable the module (a restart may follow):' -ForegroundColor Yellow
    Write-Host '       Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-Management-PowerShell' -ForegroundColor Yellow
    Write-Host '  2. Shut this virtual machine down.' -ForegroundColor Yellow
    Write-Host "  3. Set-VMProcessor -VMName 'MyVM' -ExposeVirtualizationExtensions `$true" -ForegroundColor Yellow
    Write-Host "     ('MyVM' is the name that Get-VM lists for this virtual machine.)" -ForegroundColor Yellow
    Write-Host '  4. Start the virtual machine again.' -ForegroundColor Yellow
}

$stepWsl = {
    if (Test-Wsl) {
        Write-Host "WSL is there: $((Get-WslOutput '--version').Lines | Select-Object -First 1)"
        return $true
    }
    if (-not (Test-Virtualization)) {
        Write-Host 'The virtualization of the processor is switched off. Switch it on in the BIOS/UEFI' -ForegroundColor Red
        Write-Host '(often called "Intel VT-x", "Intel Virtualization Technology" or "SVM Mode"), then' -ForegroundColor Red
        Write-Host 'run this step again.' -ForegroundColor Red
        return $false
    }
    Write-VirtualMachineNote
    Write-Host 'Installs WSL with "wsl --install --no-distribution", without a Linux (step 2).'
    Write-Host 'Windows asks for administrator rights; restart Windows afterwards.'
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    Add-LogLine $log "# $(Get-Stamp) wsl.exe --install --no-distribution (as administrator)"
    # The wsl.exe of System32: WSL itself does not exist yet.
    $process = Start-Process -FilePath 'wsl.exe' -ArgumentList '--install', '--no-distribution' -Verb RunAs -Wait -PassThru
    Add-LogLine $log "# Exit code: $($process.ExitCode)"
    if ($process.ExitCode -ne 0) {
        Write-Host "The installation ended with exit code $($process.ExitCode)." -ForegroundColor Red
        return $false
    }
    $script:RestartNeeded = $true
    Write-Host 'WSL is installed. Restart Windows, then run this script again for step 2.' -ForegroundColor Yellow
    return $true
}

$stepDistro = {
    if (Test-WslDistro) { Write-Host "$WslDistro is there."; return $true }
    if ($RestartNeeded) {
        Write-Host 'Restart Windows first, then run this script again for step 2.' -ForegroundColor Yellow
        return $false
    }
    if (-not (Test-Wsl)) { Write-Host 'WSL is missing: step 1.' -ForegroundColor Red; return $false }
    if ($Unattended) {
        Write-Host 'Ubuntu asks for a user name and password: run step 2 from the menu.' -ForegroundColor Red
        return $false
    }
    $folder = Read-Folder "Folder for $WslDistro" $DistroDir
    if ((Test-Path $folder) -and (Get-ChildItem -LiteralPath $folder -Force | Select-Object -First 1)) {
        Write-Host "$folder is not empty; choose an empty or new folder." -ForegroundColor Red
        return $false
    }
    $script:DistroDir = $folder
    Write-VirtualMachineNote
    Write-Host "Installs $WslDistro into $folder (a download of about 400 MB)."
    Write-Host 'Ubuntu then starts for the first time and asks for a user name and a password for Linux;'
    Write-Host 'they need not match the Windows ones. When the Linux prompt appears (it ends with "$"),'
    Write-Host 'type exit and press Enter.'
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    # With the folders above it, should they be missing.
    New-Item -ItemType Directory -Force $folder | Out-Null
    # Installed without starting it, so the messages of WSL, its errors among them, go to the log.
    $install = "`"$WslExe`" --install --distribution $WslDistro --location `"$folder`" --no-launch"
    if ((Invoke-Shown $install $log) -ne 0 -or -not (Test-WslDistro)) {
        Write-Host "$WslDistro is not installed; see the messages above." -ForegroundColor Red
        return $false
    }
    # The first start, directly in the console, so the questions of Ubuntu can be answered.
    Add-LogLine $log "# $(Get-Stamp) $WslExe -d $WslDistro (first start)"
    & $WslExe -d $WslDistro
    Add-LogLine $log "# Exit code: $LASTEXITCODE"
    return $true
}

$stepPackages = {
    if (-not (Test-WslDistro)) { Write-Host "$WslDistro is missing: step 2." -ForegroundColor Red; return $false }
    if (Test-UbuntuPackages) { Write-Host 'The packages are there.'; return $true }
    Write-Host "Updates $WslDistro and installs with apt-get, as root (about 1.5 GB):"
    Write-Host "  $($UbuntuPackages -join ' ')"
    if (-not (Read-YesNo 'Install now?' $false)) { return $false }
    # --exec runs the commands without a shell in between; DEBIAN_FRONTEND keeps apt-get from asking.
    # The progress bars of apt-get and dpkg move the cursor with control characters, which
    # garbles the output passed on here: -q and the two options switch them off, and the input
    # from nul keeps WSL from giving apt-get a terminal.
    $apt = "`"$WslExe`" -d $WslDistro -u root --exec env DEBIAN_FRONTEND=noninteractive apt-get -q " +
           '-o Dpkg::Progress-Fancy=0 -o Dpkg::Use-Pty=0'
    if ((Invoke-Shown "$apt update <nul" $log) -ne 0) { return $false }
    if ((Invoke-Shown "$apt upgrade -y <nul" $log) -ne 0) { return $false }
    if ((Invoke-Shown "$apt install -y $($UbuntuPackages -join ' ') <nul" $log) -ne 0) { return $false }
    if (-not (Test-UbuntuPackages)) { Write-Host 'Packages are still missing.' -ForegroundColor Red; return $false }
    return $true
}

$stepCheck = {
    if (-not (Test-UbuntuPackages)) { Write-Host 'The packages are missing: step 3.' -ForegroundColor Red; return $false }
    Write-Host ((Get-WslOutput "-d $WslDistro --exec g++ --version").Lines | Select-Object -First 1)
    New-Item -ItemType Directory -Force $checkDir | Out-Null
    # Run without arguments, argc is 1: the addition overflows, which UBSan must report.
    [IO.File]::WriteAllText("$checkDir\check.cpp", (@(
        '#include <climits>'
        'int main(int argc, char**) {'
        '  int x = INT_MAX;'
        '  x += argc;'
        '  return x < 0 ? 1 : 0;'
        '}'
    ) -join "`n") + "`n", $Utf8)
    # --cd takes the Windows folder; the program is built there, under /mnt/c or /mnt/d.
    $inCheckDir = "`"$WslExe`" -d $WslDistro --cd `"$checkDir`" --exec"
    $build = "$inCheckDir g++ -std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all check.cpp -o check"
    if ((Invoke-Shown $build $log) -ne 0) { return $false }
    Add-LogLine $log "# $(Get-Stamp) $inCheckDir ./check"
    $result = Get-WslOutput "-d $WslDistro --cd `"$checkDir`" --exec ./check"
    $result.Lines | ForEach-Object { Write-Host $_; Add-LogLine $log $_ }
    Add-LogLine $log "# Exit code: $($result.Code)"
    if (($result.Code -eq 0) -or -not ($result.Lines -match 'runtime error: signed integer overflow')) {
        Write-Host 'UBSan did not report the overflow.' -ForegroundColor Red
        return $false
    }
    Write-Host 'GCC with UBSan and ASan works.'
    return $true
}

$menu = @(
    @{ Title = 'WSL itself (administrator rights, then a restart of Windows)'
       Done = { Test-Wsl }; Run = $stepWsl }
    @{ Title = "$WslDistro, into a folder of your choice"
       Done = { Test-WslDistro }; Run = $stepDistro }
    @{ Title = 'GCC, CMake, Ninja, Qt 6 and the libraries in Ubuntu'
       Done = { Test-UbuntuPackages }; Run = $stepPackages }
    @{ Title = 'Check GCC with UBSan and ASan on a small test file'
       Run = $stepCheck }
)
# What is there, above the menu.
$status = {
    "Log: $log"
    ''
    if (Test-Wsl) { "  WSL           installed ($((Get-WslOutput '--version').Lines | Select-Object -First 1))" }
    else { '  WSL           missing (step 1)' }
    if (Test-WslDistro) { "  $($WslDistro.PadRight(12))  installed" } else { "  $($WslDistro.PadRight(12))  missing (step 2)" }
    $missing = @(Get-MissingUbuntuPackages)
    if (-not $missing.Count) { '  Packages      installed' }
    else { "  Packages      $($missing.Count) of $($UbuntuPackages.Count) missing (step 3)" }
}
$ok = Invoke-StepMenu "ScanTailor OCR - set up WSL with $WslDistro and GCC" $status $menu $Steps
Add-LogLine $log "# End: $(Get-Stamp)"
if ($ok) { exit 0 } else { exit 1 }
