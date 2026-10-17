# Analysis runs

Scripts that run compilers and analysis tools over the code and store their **raw** output, for
later evaluation.  Every log starts with `# ...` header lines (commit, tool versions, full command
lines) and ends with the exit codes.  The output goes to `ANALYSE/` in the repository (ignored by
git); each script takes `-OutDir` to change that.

Windows only.  Run them in Windows PowerShell 5.1 or PowerShell 7, from anywhere:

    powershell -ExecutionPolicy Bypass -File scripts\analysis\<script>.ps1

## Requirements

- Visual Studio with the C++ tools (found through `vswhere`); CMake, Ninja, clang-tidy and
  clang-format come with it.
- For `clang-cl.ps1` and `sanitizers.ps1`: the Visual Studio component
  "C++ Clang Compiler for Windows".
- For `cppcheck.ps1`: cppcheck (on `PATH` or in `C:\Program Files\Cppcheck`, or `-Cppcheck <exe>`).
- vcpkg with the project's dependencies for `x64-windows`, in `VCPKG_ROOT` (or `-VcpkgRoot`).

The builds go into fresh directories next to `build`.  A script stops if its build directory
already exists; delete it for a new run.  Keep build directories on a short path: with MSVC,
the generated Qt files exceed the Windows limit of 260 characters in deep folders (errors
C1083, C1258).

## The runs

| Run | Script | What it does | Build directory | Output |
|---|---|---|---|---|
| 01 | `msvc-analyze.ps1` | MSVC, C++17, `/W4 /analyze` (`ENABLE_CODE_ANALYSIS=ON`), full build incl. tests and tools | `build-analysis` | `01-msvc-analyze.log`, `01-msvc-analyze-sarif\` |
| 02 | `clang-tidy.ps1` | clang-tidy with `.clang-tidy`, sources in `src/` without tests | uses `build-analysis` | `02-clang-tidy.log`, `02-clang-tidy-fixes\*.yaml` |
| 03 | `cppcheck.ps1` | cppcheck: error, warning, performance, portability; same files as 02 | uses `build-analysis` | `03-cppcheck.xml`, `03-cppcheck.log` |
| 04 | `msvc-conformance.ps1` | MSVC, `/W4`, **C++20** instead of C++17 | `build-cxx20` | `04-msvc-cxx20.log` |
| 05 | `msvc-conformance.ps1` | MSVC, `/W4`, **`/Zc:preprocessor`** | `build-zc-preprocessor` | `05-msvc-zc-preprocessor.log` |
| 06 | `clang-cl.ps1` | **clang-cl** instead of cl.exe, `/W4` | `build-clang-cl` | `06-clang-cl.log` |
| 07 | `sanitizers.ps1` | clang-cl with **UBSan + ASan**; runs the tests and, with `-Images <folder>`, the dev tools on real scans | `build-ubsan` | `07-ubsan-build.log`, `07-ubsan-tests.log`, `07-ubsan-tools.log` |

Order: 02 and 03 need the build directory of 01.  All others are independent.

## Notes

- 04 and 05 build without `/analyze`, so their warnings can be compared with the C4xxx
  warnings of 01.  06 uses the same warning level with a different compiler.
- `/permissive-`, `/Zc:__cplusplus` and `/utf-8` are set by the Qt 6 targets in every MSVC and
  clang-cl build.
- 03 removes the vcpkg include paths, so cppcheck doesn't analyse Boost and Qt; it knows their
  APIs from `--library=qt,boost`.
- 04 switches to C++20 through `std-cxx20.cmake`, as the project sets `CMAKE_CXX_STANDARD 17`
  as a normal variable.
- 07: the comments at the top of `sanitizers.ps1` explain its unusual setup and a known
  limitation: ASan reports an error inside Qt at the startup of programs loading
  `Qt6Widgets.dll`.
