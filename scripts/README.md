# Scripts

PowerShell scripts for setting up a Windows machine, checking the code and packaging a release.
None of them is needed to build the program by hand as [BUILDING.md](../BUILDING.md) describes.
They run in Windows PowerShell 5.1 or PowerShell 7, from any folder:

    powershell -ExecutionPolicy Bypass -File scripts\<folder>\<script>.ps1

| Folder | What it holds |
|---|---|
| `setup\` | Sets up the machine, in numbered steps (see below). Logs go to `setup\logs\`. |
| `analysis\` | The analysis runs: compilers, analysis tools, sanitizers, fuzzing, Application Verifier. See [analysis/README.md](analysis/README.md). |
| `overview\` | The overview runs: the code as a graph (graphify), class and sequence diagrams (clang-uml) and the targets of the build (`cmake --graphviz`). See [overview/README.md](overview/README.md). |
| `release\` | `package-windows.ps1` builds the portable Windows ZIP from a finished Release build (see [BUILDING.md](../BUILDING.md)). |
| `support\` | Helpers the scripts share (finding Visual Studio and vcpkg, the logs); not run on their own. |

## Setup

| Script | Sets up | Needed for |
|---|---|---|
| `01-setup-windows.ps1` | Visual Studio, Git and the GitHub CLI, vcpkg and its packages, `VCPKG_ROOT`, the first build | the build |
| `02-setup-analysis.ps1` | the tools of the analysis runs, including the debugger cdb of run 12 (step 5); shows which are missing and starts 03 and 04 when needed | the analysis runs |
| `03-setup-edg.ps1` | the EDG C++ front end, with `EDG_ROOT` | analysis run 04 |
| `04-setup-wsl-gcc.ps1` | WSL with Ubuntu 24.04, GCC and the project's packages | analysis run 08 |
| `05-setup-overview.ps1` | uv, graphify, clang-uml and optionally Graphviz, each in the tested or the newest version, and optionally `/graphify` for Claude Code | the overview runs |

Each setup script shows its steps in a menu; each step can be run on its own, 0 runs all of them,
and a step skips what is already there.

## Common behaviour

- **Help:** every script describes itself and its parameters at its top, also shown by
  `Get-Help scripts\<folder>\<script>.ps1 -Detailed`.
- **Asking first:** the scripts say what they will do and ask before they start. `-Yes` starts
  right away; for the setup scripts it goes with `-Steps <numbers>`, which runs steps without
  the menu.
- **Folders:** the analysis runs, overview run 02 and the packaging take `-VcpkgRoot` (default:
  the variable `VCPKG_ROOT`) and `-BuildDir` (default: `build` for the packaging, a `build-*`
  folder of its own for each run); the analysis and overview runs also take `-OutDir` for their
  output.
- **Right after a setup:** a setup and the runs can follow each other in the same window. A
  window keeps the environment it was opened with, so the scripts take what a setup added from
  the user account (`PATH`, `VCPKG_ROOT`, the folders of uv) and look for the tools in the
  folders their setup installs them to.
- **Logs:** every log starts with `# ...` header lines (commit, tool versions, full command lines)
  and ends with the exit codes.
