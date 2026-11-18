<#
.SYNOPSIS
  Helpers for the overview runs in this folder. Dot-sourced by the run scripts.

.DESCRIPTION
  Loads the shared helpers of ..\..\support\common.ps1 (Visual Studio, vcpkg, logging, configure
  and build, finding graphify and clang-uml) and sets where the runs put their results.
#>

. "$PSScriptRoot\..\..\support\common.ps1"
# Logs and diagrams go here unless a script is given -OutDir (ignored by git).
$ResultsDir = "$(Split-Path $PSScriptRoot)\results"
# The -Yes of the run scripts: start without asking.
$Unattended = [bool]$Yes
$AssumeYes = [bool]$Yes
