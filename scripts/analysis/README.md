# Analysis runs

Scripts that run compilers and analysis tools over the code and store their **raw** output, for
later evaluation.  Every script is one run, numbered in the order in which they are best run;
`09-merge-findings.ps1` then merges the findings of all runs into one table.

Every log starts with `# ...` header lines (commit, tool versions, full command lines) and ends
with the exit codes.  The output goes to `results\` in this folder (ignored by git); each script
takes `-OutDir` to change that.

Windows only.  Run them in Windows PowerShell 5.1 or PowerShell 7, from anywhere:

    powershell -ExecutionPolicy Bypass -File scripts\analysis\<script>.ps1

The examples below assume the folders of [BUILDING.md](../../BUILDING.md): the repository in
`D:\scantailor-ocr`, vcpkg in `D:\vcpkg`.

## Overview

| Run | Script | What it does | Build directory | Output in `results\` |
|---|---|---|---|---|
| 01 | `01-msvc-analyze.ps1` | MSVC, C++17, `/W4 /analyze`, full build incl. tests and tools | `build-analysis` | `01-msvc-analyze.log`, `01-msvc-analyze-sarif\` |
| 02 | `02-clang-tidy.ps1` | clang-tidy with `.clang-tidy` | uses `build-analysis` | `02-clang-tidy.log`, `02-clang-tidy-fixes\*.yaml` |
| 03 | `03-cppcheck.ps1` | cppcheck: error, warning, performance, portability | uses `build-analysis` | `03-cppcheck.xml`, `03-cppcheck.log` |
| 04 | `04-edg.ps1` | the EDG C++ front end (set up once with `setup\3-setup-edg.ps1`) | uses `build-analysis` | `04-edg.log` |
| 05 | `05-msvc-cxx20.ps1` | MSVC, `/W4`, **C++20** instead of C++17 | `build-cxx20` | `05-msvc-cxx20.log` |
| 06 | `06-msvc-zc-preprocessor.ps1` | MSVC, `/W4`, **`/Zc:preprocessor`** | `build-zc-preprocessor` | `06-msvc-zc-preprocessor.log` |
| 07 | `07-clang-cl.ps1` | **clang-cl** instead of cl.exe, `/W4` | `build-clang-cl` | `07-clang-cl.log` |
| 08 | `08-sanitizers.ps1` | clang-cl with **UBSan + ASan**; runs the tests and the dev tools | `build-ubsan` | `08-ubsan-build.log`, `08-ubsan-tests.log`, `08-ubsan-tools.log` |
| 09 | `09-merge-findings.ps1` | merges the findings of all runs | – | `findings.csv`, `findings.md` |

Order: 02, 03 and 04 need the build directory of 01.  05 to 08 are independent of each other
and of 01.  09 reads whatever output exists.

## Requirements

- Visual Studio with the C++ tools (found through `vswhere`); CMake, Ninja, clang-tidy and
  clang-format come with it.
- vcpkg with the project's dependencies for `x64-windows`, in `VCPKG_ROOT` (or `-VcpkgRoot`).
- For 03: cppcheck (on `PATH` or in `C:\Program Files\Cppcheck`, or `-Cppcheck <exe>`).
- For 04: EDG, set up as described [below](#setting-up-edg-for-run-04).
- For 07 and 08: the Visual Studio component "C++ Clang Compiler for Windows".

Two setup scripts take care of all of this; each shows a menu of steps, 0 runs all of them:

- `scripts\setup\1-setup-windows.ps1` sets up the build itself: Visual Studio, Git and the
  GitHub CLI, vcpkg and its packages, `VCPKG_ROOT` (see [BUILDING.md](../../BUILDING.md)).
- `scripts\setup\2-setup-analysis.ps1` shows for every run whether its tools are there and
  installs the missing ones: the Clang compiler and the .NET Framework 4.8.1 SDK with the Visual
  Studio Installer, cppcheck with winget (both into their standard folders), and EDG by starting
  `3-setup-edg.ps1`.

      powershell -ExecutionPolicy Bypass -File scripts\setup\2-setup-analysis.ps1

The builds go into fresh directories next to `build`.  A script stops if its build directory
already exists; delete it for a new run.  Keep build directories on a short path: with MSVC,
the generated Qt files exceed the Windows limit of 260 characters in deep folders (errors
C1083, C1258).

## The runs

### 01 – MSVC code analysis

A full MSVC build (C++17, `/W4`) with the compiler's static code analysis
(`ENABLE_CODE_ANALYSIS=ON`, `/analyze`), including the tests and the dev tools.  The analysis
results are also written as SARIF files, one per source file.  Its build directory
`build-analysis` is used by 02, 03 and 04.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\01-msvc-analyze.ps1

### 02 – clang-tidy

clang-tidy with the repository's `.clang-tidy` over all sources in `src\` without the tests and
generated files.  Besides the log, the diagnostics are written as YAML per source file (only for
files that have any).

    powershell -ExecutionPolicy Bypass -File scripts\analysis\02-clang-tidy.ps1

### 03 – cppcheck

cppcheck over the same files as 02, with the severities error, warning, performance and
portability.  The vcpkg include paths are removed, so cppcheck doesn't analyse Boost and Qt; it
knows their APIs from `--library=qt,boost`.  The findings are in `03-cppcheck.xml`; the log only
holds the progress output.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\03-cppcheck.ps1

### 04 – EDG C++ front end

[EDG](https://edgcpp.org) is a third C++ front end besides MSVC and Clang (it was used, for
example, by Visual Studio's IntelliSense).  It parses and checks every file of 02 like a
compiler, without generating code.  It has to be set up once, see below; then:

    powershell -ExecutionPolicy Bypass -File scripts\analysis\04-edg.ps1

The include paths and macros are those of `build-analysis`.  The project's MSVC switches are
emulated (`/std:c++17`, `/permissive-`, `/Zc:__cplusplus`, the version of `cl.exe`): the headers
of the MSVC standard library and of Windows use Microsoft extensions such as `__int64`, and EDG's
strict standard mode (`--strict`) can't be combined with the emulation.  `-Files <regex>` checks
only some files, `-Suppress <numbers>` hides diagnostics, `-Remarks` adds EDG's remarks.

What to expect in the output:

- **#161 "unrecognized #pragma"** at every `Q_OBJECT`: Qt's macro contains
  `__pragma(warning(...))`, and EDG doesn't know MSVC's `warning` pragma.  Harmless; 09 counts
  it as a bulk check, `-Suppress 161` hides it.
- **#2422 in `QtCore\qrunnable.h`** (`constexpr QRunnable() noexcept = default` in a class with
  virtual functions): EDG rejects this in C++17, MSVC accepts it.  It is a finding in Qt; it makes
  the files that include the header fail (ninja exit code 2), but EDG checks them to the end.
- Qt, vcpkg, the standard library and the Windows SDK are system headers (`--sys_include`):
  EDG reports fewer warnings there, but not none.  Warnings in templates that the project
  instantiates still appear, e.g. in Boost (#1444 deprecated `std::allocator` members, #940
  missing return statement).

#### Setting up EDG for run 04

Needed once, and again for a newer EDG.  Run:

    powershell -ExecutionPolicy Bypass -File scripts\setup\3-setup-edg.ps1

It asks for the folder that gets the checkouts `edgcpp` and `softfloat` (suggesting `D:\` when
drive D: exists, so EDG goes to `D:\edgcpp`), then offers five steps in a menu; each can be run
on its own, 0 runs all of them in order:

1. Requirements: Visual Studio with the C++ tools, the Visual Studio component
   **".NET Framework 4.8.1 SDK"** (EDG links `mscoree.lib` from it for its C++/CLI support),
   Python 3.6 or later (the one vcpkg downloads to `D:\vcpkg\downloads\tools\python` is enough)
   and [Git for Windows](https://git-scm.com/download/win).
2. Sources: clones EDG and [Berkeley SoftFloat 3](https://github.com/ucb-bar/berkeley-softfloat-3).
   EDG either at the version the setup was tested with, or the newest one (`git pull` for an
   existing checkout); SoftFloat at the commit EDG pins.
3. Build: SoftFloat, which EDG needs on Windows for its floating-point arithmetic, with the
   CMake file in `scripts\setup\edg-softfloat\` (SoftFloat only ships makefiles for GCC and
   MinGW), then EDG with its preset `windows-msvc-release`: about ten minutes the first time,
   a minute or two later on.
4. `EDG_ROOT`: sets the variable for your user account, so `04-edg.ps1` finds EDG.
5. Check: runs `cpfe.exe` on a small C++17 file in strict mode.

The output also goes to `scripts\setup\logs\3-setup-edg.log`.  The preset only chooses MSVC as
the compiler that builds EDG and Windows x64 as the target (sizes of `long`, `wchar_t` and so
on); how EDG checks is chosen when it runs.  Without `EDG_ROOT`, `04-edg.ps1` takes
`-EdgDir <folder>`.

### 05 – MSVC with C++20

A full MSVC build with `/W4` and C++20 instead of C++17, without `/analyze`, so its warnings can
be compared with the C4xxx warnings of 01.  As the project sets `CMAKE_CXX_STANDARD 17` as a
normal variable, C++20 is switched on through `std-cxx20.cmake`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\05-msvc-cxx20.ps1

### 06 – MSVC with the conforming preprocessor

Like 05, but C++17 with the standard-conforming preprocessor `/Zc:preprocessor`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\06-msvc-zc-preprocessor.ps1

### 07 – clang-cl

A full build with clang-cl instead of `cl.exe`, at the same warning level (`/W4`, which is
`-Wall -Wextra` in clang-cl).  clang-cl uses the MSVC ABI, so the vcpkg libraries are used
unchanged.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\07-clang-cl.ps1

### 08 – Sanitizers

A clang-cl build with UndefinedBehaviorSanitizer and AddressSanitizer, which then runs all test
programs and, with `-Images <folder>`, lets the dev tools process real scans.  The comments at
the top of the script explain its unusual setup and a known limitation: ASan reports an error
inside Qt at the startup of programs loading `Qt6Widgets.dll`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\08-sanitizers.ps1 -Images D:\scans

### 09 – Merging the results

Reads the output of the runs (all that exist) and writes the distinct findings to
`findings.csv` (all of them) and `findings.md` (a summary per tool and check, then the findings
by file and line).  A finding is one combination of tool, check, file and line; repeats are
merged and the runs that reported them are listed.  Frequent, mostly harmless checks
(unreferenced parameters, `size_t` conversions, hidden names, EDG's #161; see `-BulkChecks`) are
only counted in `findings.md`.  Needs neither Visual Studio nor any of the tools.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\09-merge-findings.ps1

## Notes

- `/permissive-`, `/Zc:__cplusplus` and `/utf-8` are set by the Qt 6 targets in every MSVC and
  clang-cl build.
- A diagnostic in a header appears once per source file that includes it; 09 merges these.
