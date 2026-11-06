# Analysis runs

Scripts that run compilers and analysis tools over the code and store their **raw** output, for
later evaluation.  Every script is one run, numbered in the order in which they are best run;
`90-merge-findings.ps1` then merges the findings of all runs into one table.  (There is no run
11: the merge was run 11 before it became 90.)

The output goes to `results\` in this folder (ignored by git).  How the scripts are started,
their common parameters and their logs are described in [the README of the scripts](../README.md).
Windows only; run 08 builds in WSL, but is started from Windows like the others.  The examples
below assume the folders of [BUILDING.md](../../BUILDING.md): the repository in
`D:\scantailor-ocr`, vcpkg in `D:\vcpkg`.

## Overview

| Run | Script | What it does | Build directory | Output in `results\` |
|---|---|---|---|---|
| 01 | `01-msvc-analyze.ps1` | MSVC, C++17, `/W4 /analyze`, full build incl. tests and tools | `build-analysis` | `01-msvc-analyze.log`, `01-msvc-analyze-sarif\` |
| 02 | `02-clang-tidy.ps1` | clang-tidy with `.clang-tidy` | uses `build-analysis` | `02-clang-tidy.log`, `02-clang-tidy-fixes\*.yaml` |
| 03 | `03-cppcheck.ps1` | cppcheck: error, warning, performance, portability | uses `build-analysis` | `03-cppcheck.xml`, `03-cppcheck.log` |
| 04 | `04-edg.ps1` | the EDG C++ front end (set up once with `setup\03-setup-edg.ps1`) | uses `build-analysis` | `04-edg.log` |
| 05 | `05-msvc-cxx20.ps1` | MSVC, `/W4`, **C++20** instead of C++17 | `build-cxx20` | `05-msvc-cxx20.log` |
| 06 | `06-msvc-zc-preprocessor.ps1` | MSVC, `/W4`, **`/Zc:preprocessor`** | `build-zc-preprocessor` | `06-msvc-zc-preprocessor.log` |
| 07 | `07-clang-cl.ps1` | **clang-cl** instead of cl.exe, `/W4` | `build-clang-cl` | `07-clang-cl.log` |
| 08 | `08-gcc.ps1` | **GCC** in WSL: strict warnings, then **UBSan + ASan** and the checks of the standard library; runs the tests | `~/scantailor-ocr-build/gcc-*` in WSL | `08-gcc-warnings.log`, `08-gcc-sanitizers-build.log`, `08-gcc-tests.log` |
| 09 | `09-sanitizers.ps1` | clang-cl with **UBSan + ASan**; runs the tests and the dev tools | `build-ubsan` | `09-ubsan-build.log`, `09-ubsan-tests.log`, `09-ubsan-tools.log` |
| 10 | `10-fuzz.ps1` | MSVC with **libFuzzer + ASan**; runs the fuzz targets of `src/fuzz` | `build-fuzz` | `10-fuzz.log`, `10-fuzz-build.log`, `10-fuzz\<target>\` |
| 12 | `12-appverifier.ps1` | the tests under **Application Verifier** (runs as administrator) | `build-appverif` | `12-appverif-tests.log` (`-build`, `-tools`, `-lowres`), `12-appverif\<program>.xml` |
| 90 | `90-merge-findings.ps1` | merges the findings of all runs | – | `findings.csv`, `findings.md` |

Order: 02, 03 and 04 need the build directory of 01.  05 to 12 are independent of each other
and of 01.  90 reads whatever output exists, so it is best run last.

## Requirements

Every run needs Visual Studio with the C++ tools (found through `vswhere`; CMake, Ninja,
clang-tidy and clang-format come with it) and vcpkg with the project's dependencies for
`x64-windows`, both set up by `scripts\setup\01-setup-windows.ps1`.  What the runs need beyond
that is set up by `scripts\setup\02-setup-analysis.ps1`; its step 9 shows for every run whether
its tools are there.

| Run | Needs in addition | Set up by `02-setup-analysis.ps1` |
|---|---|---|
| 03 | cppcheck (on `PATH`, in `C:\Program Files\Cppcheck`, or `-Cppcheck <exe>`) | step 2, with winget |
| 04 | EDG and the Visual Studio component ".NET Framework 4.8.1 SDK" | step 1; step 3 starts `03-setup-edg.ps1` ([below](#setting-up-edg-for-run-04)) |
| 07 | the Visual Studio component "C++ Clang Compiler for Windows" | step 1 |
| 08 | WSL with Ubuntu 24.04, GCC and the project's packages | step 4 starts `04-setup-wsl-gcc.ps1` |
| 09 | the Visual Studio components "C++ Clang Compiler for Windows" and "C++ AddressSanitizer" (it brings `stl_asan.lib`, which the MSVC standard library links under ASan) | step 1 |
| 10 | the Visual Studio component "C++ AddressSanitizer" (it brings libFuzzer and ASan of MSVC); no Clang compiler | step 1 |
| 12 | Application Verifier (comes with Windows 11), the debugger `cdb.exe` of the "Debugging Tools for Windows", administrator rights | step 5 opens the setup of the Windows SDK, where "Debugging Tools for Windows" is ticked by hand (the Visual Studio Installer cannot add it) |

Runs 01, 02, 05 and 06 need nothing beyond that; 90 needs neither Visual Studio nor any of the
tools.

    powershell -ExecutionPolicy Bypass -File scripts\setup\02-setup-analysis.ps1

## Running and build directories

Each run first says what it does and asks before it starts.  While it runs, the window shows the
progress in one line (step, warnings and errors so far, time) and every error in full; the
complete output goes to the log.

The builds go into fresh directories next to `build`.  If its build directory is left from an
earlier run, a script offers to delete it, but only a CMake build directory; with `-Yes` it
deletes only a `build-*` folder in the project folder and stops otherwise.  Runs 08, 09, 10 and
12 take `-SkipBuild`, which reuses the existing build and only runs the programs; run 10 never
deletes its build directory (see there).

Keep build directories on a short path: with MSVC, the generated Qt files exceed the Windows
limit of 260 characters in deep folders (errors C1083, C1258).  Run 08 builds in the Linux file
system of WSL instead, which is much faster there than a folder on the Windows drives.

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
holds the progress output.  `-Only <file>` checks a single source file as a trial, without a log.

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
  `__pragma(warning(...))`, and EDG doesn't know MSVC's `warning` pragma.  Harmless and hidden
  by default (`-Suppress` defaults to 161; `-Suppress @()` shows it).
- **#2422 in `QtCore\qrunnable.h`** (`constexpr QRunnable() noexcept = default` in a class with
  virtual functions): EDG rejects this in C++17, MSVC accepts it.  It is a finding in Qt; it makes
  the files that include the header fail (ninja exit code 2), but EDG checks them to the end.
- Qt, vcpkg, the standard library and the Windows SDK are system headers (`--sys_include`):
  EDG reports fewer warnings there, but not none.  Warnings in templates that the project
  instantiates still appear, e.g. in Boost (#1444 deprecated `std::allocator` members, #940
  missing return statement).

#### Setting up EDG for run 04

Needed once, and again for a newer EDG:

    powershell -ExecutionPolicy Bypass -File scripts\setup\03-setup-edg.ps1

It checks the requirements (among them Python 3.6 or later; the one vcpkg downloads is enough),
clones EDG and [Berkeley SoftFloat 3](https://github.com/ucb-bar/berkeley-softfloat-3), which
EDG needs on Windows for its floating-point arithmetic, builds both (about ten minutes the first
time), sets `EDG_ROOT` for your user account and checks the result.  The steps are described at
the top of the script.  Without `EDG_ROOT`, `04-edg.ps1` takes `-EdgDir <folder>`.

### 05 – MSVC with C++20

A full MSVC build with `/W4` and C++20 instead of C++17, without `/analyze`, so its warnings can
be compared with the C4xxx warnings of 01.  As the project sets `CMAKE_CXX_STANDARD 17` as a
normal variable, C++20 is switched on through `support\std-cxx20.cmake`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\05-msvc-cxx20.ps1

### 06 – MSVC with the conforming preprocessor

Like 05, but C++17 with the standard-conforming preprocessor `/Zc:preprocessor`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\06-msvc-zc-preprocessor.ps1

### 07 – clang-cl

A full build with clang-cl instead of `cl.exe`, at the same warning level (`/W4`, which is
`-Wall -Wextra` in clang-cl).  clang-cl uses the MSVC ABI, so the vcpkg libraries are used
unchanged.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\07-clang-cl.ps1

### 08 – GCC in WSL

Builds the project twice with GCC in WSL (Ubuntu 24.04), from the sources in this repository:

- with strict warnings: `ENABLE_CODE_ANALYSIS=ON` (`-Wall -Wextra -Wshadow -Wconversion`) and
  the warnings only GCC has (`-Wnull-dereference`, `-Wduplicated-cond`,
  `-Wduplicated-branches`, `-Wlogical-op`);
- with UndefinedBehaviorSanitizer, AddressSanitizer and `-D_GLIBCXX_ASSERTIONS`, which makes the
  GCC standard library check indexes and iterators; then all tests run with `ctest -V`.

What it adds to the other runs: a second compiler with its own warnings, a second standard
library (libstdc++ instead of the one of MSVC), and UBSan with all its checks, which MSVC lacks
and which clang-cl has only in part on Windows.  It checks the Linux code paths, not the code
under `#ifdef _WIN32`.  A failed check of the standard library stops its test program; ASan
then reports the abort with a stack trace.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\08-gcc.ps1

The builds go to `~/scantailor-ocr-build/gcc-warnings` and `gcc-sanitizers` in WSL and are
deleted first when left from an earlier run; `-SkipBuild` reuses the second one and only runs
the tests.  The number of compile jobs at once follows the memory WSL has (one per 1.5 GB, by
default WSL gets half of the memory of Windows): too many at once make Linux stop the build.
Set up with `scripts\setup\04-setup-wsl-gcc.ps1` (WSL, Ubuntu, the packages of
`scripts\setup\support\ubuntu-packages.txt`).  If Windows itself runs in a virtual machine, its
host must allow nested virtualization for WSL 2.

### 09 – Sanitizers

A clang-cl build with UndefinedBehaviorSanitizer and AddressSanitizer, which then runs all test
programs and, with `-Images <folder>`, lets the dev tools process real scans.  The comments at
the top of the script explain its unusual setup and a known limitation: ASan reports an error
inside Qt at the startup of programs loading `Qt6Widgets.dll`.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\09-sanitizers.ps1 -Images D:\scans

Because of that startup error, a test program that printed nothing and failed with one of its
signs is started again, at most three times; the reports of the discarded starts stay in the
log.  A crash or a timeout is never retried, as it may be a finding.  A test program whose output
still holds no test summary is marked `# NOT RUN:` in the log, and run 90 names it in
`findings.md`.

Two kinds of reports are known to be harmless; the code is not changed for them:

- ASan `bad-free` in Qt's `qarraydata.cpp`: the startup error described above.
  `Qt6Widgets.dll` allocated the memory before ASan hooked the allocator.
- UBSan `insufficient-object-size` ("member call on address ... with insufficient space for an
  object of type `QNonConstOverload<...>`") at `qOverload<...>(&Class::signal)` calls, e.g. in
  `filters/output/OptionsWidget.cpp`, `filters/page_layout/OptionsWidget.cpp`,
  `TiffCompressionPanel.cpp` and `qt_tests/tst_core_qt.cpp`.  `QOverload` (`qoverload.h`)
  derives from the two empty bases `QConstOverload` and `QNonConstOverload`; in the MSVC ABI
  the second empty base sits at offset 1 of the 1-byte object, so UBSan's size check for that
  base fails (the reported address is odd).  Its `operator()` only returns the member-function
  pointer it is given and never reads `*this`, so nothing invalid is accessed.

### 10 – Fuzzing

Builds the fuzz targets of `src/fuzz` (`-DBUILD_FUZZERS=ON`: all of the project's code with
AddressSanitizer and the coverage instrumentation of libFuzzer, without the program and the
tests) and runs the chosen ones.  A fuzz target feeds generated input to one part of the
program and keeps the inputs that reach new code:

| Target | Reads | Seeds in `src/fuzz/corpus` |
|---|---|---|
| `fuzz_image_metadata` | page sizes and resolutions of TIFF, JPEG, PNG, JPEG 2000 | `images` |
| `fuzz_image_loader` | the pixels of those images, also as thumbnails | `images` |
| `fuzz_project_reader` | project files, without the filter settings | `project`, `project.dict` |
| `fuzz_filter_params` | the per-page settings of the filters | `filter_params`, `filter_params.dict` |
| `fuzz_image_processing` | small pages built from the input, processed by binarization, despeckling, Deskew, Split Pages, Select Content, automatic dewarping, the color processing of the Output step and its automatic color mode | `processing` (generated: 96 x 72 pixel pages with lines of text, also curved) |
| `fuzz_profile_reader` | profile files with the default parameters of new pages | `profile`, `profile.dict` |
| `fuzz_page_output` | a whole page through the Output step (`OutputGenerator`), with the settings and zones of a project file | `page_output`, `page_output.dict` |
| `fuzz_project_geometry` | the page geometry that the settings of a project file give, from Fix Orientation to the output resolution, without an image | `geometry`, `geometry.dict` |
| `fuzz_pdf_export` | the PDF export of a page: small images of every kind, stored with each compression and with a text layer | `pdf_export` (generated: one file per kind of image and compression) |

Besides crashes, some targets count broken promises of the code as findings, e.g. output of the
wrong size or a PDF whose cross-reference table misses its objects.  The top of each target's
source file in `src/fuzz` describes its input and what it checks.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\10-fuzz.ps1

Without parameters, the script asks which targets to run (each with the size of its corpus and
its open findings) and for how long each (5, 10, 30 or 60 minutes, or another time; Enter takes
10), shows the total time and asks before it starts; Q in either question ends the script.
`-Only` and `-Minutes` skip the respective question, `-Yes` both (then all targets, 10 minutes
each):

    powershell -ExecutionPolicy Bypass -File scripts\analysis\10-fuzz.ps1 -Only fuzz_image_loader -Minutes 5 -SkipBuild

Each run adds to the corpus of the ones before, so several short runs find about as much as one
long one; once the coverage (`ft` in the summary) hardly grows any more, longer runs rarely pay.

Per target, `results\10-fuzz\<target>\` holds `corpus\` (the inputs found so far; the next run
continues from them), `findings\` (the inputs that made the target fail: `crash-*`, `oom-*`,
`timeout-*`) and `fuzz.log` (the output of the last run).  `10-fuzz.log` has a summary per
target and runs every input in `findings\` once more, so its report is there for run 90; an
input that no longer fails shows that it was fixed.  Delete `findings\` once its inputs are dealt
with.  Unlike the other runs, an existing `build-fuzz` is built again instead of deleted, so a
run after a fix starts within minutes.

A single input is reproduced by running the target on it, e.g.

    build-fuzz\fuzz_image_loader.exe -rss_limit_mb=2048 results\10-fuzz\fuzz_image_loader\findings\oom-...

Limits: an input that takes longer than 20 seconds or more memory than `-RssLimitMb` (default
2048 MB) counts as a finding.  The image targets skip pages over 16 megapixels: a few bytes can
claim an image up to the size limit of the program, whose decoding takes gigabytes; such sizes
are covered by the tests of the size limit.  Qt and the vcpkg libraries are DLLs and not
instrumented: the fuzzer sees no coverage inside them and ASan checks no accesses there.

### 12 – Application Verifier

A full MSVC build (RelWithDebInfo, in `build-appverif`), then every test program runs under the
debugger `cdb` with the "Basics" checks of Application Verifier: full page heap, handles, locks
and SRW locks, memory, TLS, exceptions, thread pool, and leaks when a DLL is unloaded.  Unlike
the sanitizers of run 09, they also check the DLLs not built here (Qt, Tesseract, Leptonica,
libtiff ...), and they find misused handles and locks, which ASan does not.  Application Verifier
and ASan exclude each other, hence the normal MSVC build.

Application Verifier keeps its settings per program name in the registry, so the run needs a
PowerShell **run as administrator**.  The script deletes the settings of every program it ran,
also when it stops with an error; after an interrupted run, `-Reset` deletes them without running
anything.  The tests take much longer than without the checks (full page heap).

    powershell -ExecutionPolicy Bypass -File scripts\analysis\12-appverifier.ps1 -Images D:\scans

A verifier stop is written to the log with its stack (`# VERIFIER BREAK`), and the program goes
on; any other stop of the debugger (a crash, an unhandled exception) is written with its stack
(`# DEBUGGER STOP`) and ends the program.  The binary logs of Application Verifier are exported
to `results\12-appverif\<program>.xml`.  `-Images` also runs the dev tools on real scans
(`12-appverif-tools.log`).  `-LowResource` adds fault injection for the tests of the command
line tool in `core_tests` (`12-appverif-lowres.log`): 2 % of the heap allocations and file
operations fail at random, so failed tests are expected there and differ between runs; the
findings are the verifier stops and crashes.  The stacks of Windows' own DLLs come from
Microsoft's symbol server (cached in `build-appverif\symbols`), unless `_NT_SYMBOL_PATH` is set.
A program that timed out or ended with an exit code other than 0 is named by run 90 under
"Timed out or failed" in `findings.md`, as it may hold a finding without a verifier stop.  The
`# End:` line is written once, when the whole run has finished; without it, run 90 shows the log
as not finished.

### 90 – Merging the results

Reads the output of the runs (all that exist) and writes the distinct findings to
`findings.csv` (all of them) and `findings.md` (a summary per tool and check, then the findings
by file and line).  A finding is one combination of tool, check, file and line; repeats are
merged (a diagnostic in a header appears once per source file that includes it) and the runs
that reported them are listed.  Frequent, mostly harmless checks
(unreferenced parameters, `size_t` conversions, GCC's `-Wconversion` and `-Wfloat-conversion`,
hidden names; see `-BulkChecks`) are only counted in `findings.md`.  `-AnalysisDir` reads the
output from another folder than `results\`.  Needs neither Visual Studio nor any of the tools.

    powershell -ExecutionPolicy Bypass -File scripts\analysis\90-merge-findings.ps1

## Notes

`/permissive-`, `/Zc:__cplusplus` and `/utf-8` are set by the Qt 6 targets in every MSVC and
clang-cl build.
