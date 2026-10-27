# Building ScanTailor OCR

## Windows, step by step

These steps assume a machine with nothing installed yet. vcpkg comes without an installer: it is
simply unpacked into a folder of your choice. The examples below put everything directly on
drive `D:` – any path works, as long as you use it consistently and it contains no spaces or
non-English characters.

**1. Get the source code** into `D:\scantailor-ocr`, either with
[Git for Windows](https://git-scm.com/download/win):

```
cd /d D:\
git clone https://github.com/2ndmax/scantailor-ocr.git
```

or by downloading the ZIP of this repository ("Code" → "Download ZIP") and unpacking it there.

**The quick way: the setup script.** Steps 2 to 7 below can also be done by a script. Open a
PowerShell window and run:

```
powershell -ExecutionPolicy Bypass -File D:\scantailor-ocr\scripts\setup\1-setup-windows.ps1
```

It offers five steps in a menu; each can be run on its own, 0 runs all of them in order:

1. Visual Studio (step 2 below): checks for the C++ components; installs Visual Studio
   Community or the Build Tools with winget when it is missing, or adds missing components with
   the Visual Studio Installer, both into their standard folder on drive C: (the Build Tools
   about 4 GB, Community about 10 GB).
2. Git and the GitHub CLI: installs them with winget when they are missing. Not needed for the
   build, but with Git step 3 gets vcpkg with its history, so it can be updated later.
3. vcpkg (steps 3 and 4): asks for the vcpkg folder (suggesting `D:\vcpkg` when drive D:
   exists), gets vcpkg and installs the packages.
4. `VCPKG_ROOT` (step 5): sets the variable for your user account.
5. Build (steps 6 and 7): configures and builds; then offers to download the OCR languages German
   and English and to run the tests.

Each step skips what is already there, and asks before installing anything; it warns when the
drive has too little free space. The output also goes to `scripts\setup\logs\1-setup-windows.log`,
the one of the run before to `1-setup-windows.previous.log`. The steps below describe the same
by hand.

**2. Install Visual Studio Community** (the free edition):
<https://visualstudio.microsoft.com/vs/community/>
In the installer pick the workload **"Desktop development with C++"**. Of its optional
components, only these are needed:

* MSVC build tools (x64)
* C++ CMake tools for Windows (they include CMake and Ninja, which does the build)
* Windows 11 SDK

**3. Download vcpkg** as a ZIP: <https://github.com/microsoft/vcpkg>
Unpack it to `D:\vcpkg`.

**4. Build the libraries with vcpkg.** Open the **"Native Tools Command Prompt for VS x64"**
(from the start menu, inside the Visual Studio folder) and run:

```
cd /d D:\vcpkg
bootstrap-vcpkg.bat
powershell -NoProfile -Command "& .\vcpkg.exe install --recurse (Get-Content D:\scantailor-ocr\scripts\setup\vcpkg-packages.txt | Where-Object { $_ -and $_ -notmatch '^#' })"
```

The packages are listed in [scripts/setup/vcpkg-packages.txt](scripts/setup/vcpkg-packages.txt),
which the setup script and the GitHub workflows use as well. `tesseract` is only needed for
text recognition in the PDF export (see the build options below).

This compiles Qt and everything else from source and takes a while – plan for an hour or more
and at least 35 GB of disk space for the vcpkg folder.

**5. Tell the build where vcpkg is.** In the same command prompt:

```
setx VCPKG_ROOT D:\vcpkg
```

This sets the variable for your user account; no administrator rights or restart are needed.
Close the command prompt and open a new **"Native Tools Command Prompt for VS x64"**, which
sees the variable. `echo %VCPKG_ROOT%` should now show `D:\vcpkg`.

**6. Configure and build.** In the new command prompt:

```
cd /d D:\scantailor-ocr
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
cmake --build build
```

Ninja uses all processor cores by itself. The finished `scantailor-ocr.exe` and all needed DLLs
end up in the `build` directory.
For text recognition, download languages in the program (step "Create PDF" → "More languages ..."),
or put the language files into `build\tessdata` by hand, e.g. for German and English
(about 24 MB together):

```
mkdir build\tessdata
curl -L -o build\tessdata\deu.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/deu.traineddata
curl -L -o build\tessdata\eng.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/eng.traineddata
```

**7. Run the tests** (optional):

```
ctest --test-dir build --output-on-failure
```

**8. Create the portable ZIP** (optional), from the repository folder, with the version
number of the release instead of `<x.y.z>`:

```
powershell -ExecutionPolicy Bypass -File scripts\release\package-windows.ps1 -Version <x.y.z>
```

The script collects the program, exactly the DLLs it needs (read from the import tables, so
test libraries are left out), the Qt plugins, the translations, the OCR languages German and
English from `build\tessdata` and the license texts of all components (from vcpkg, found
through `VCPKG_ROOT`) into `build\package`. It then starts a copy with a minimal `PATH` for a
few seconds to check that nothing is missing, and packs the ZIP. `-Languages` selects other
languages to ship, `-SkipStartTest` skips the start test; see the comments at the top of the
script. The ZIPs on the releases page are built the same way by GitHub Actions.

When configuring again later, e.g. after changing build options, add `--fresh` to the `cmake`
call to discard the cached configuration:

```
cmake --fresh -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
```

## Linux (Debian / Ubuntu)

Qt 6.4 or later is required (Ubuntu 24.04 or later):

```
sudo apt install build-essential cmake \
  qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6opengl6-dev libgl-dev \
  libboost-test-dev libboost-dev libjpeg-dev libpng-dev libtiff-dev zlib1g-dev libopenjp2-7-dev \
  libtesseract-dev libleptonica-dev tesseract-ocr-deu tesseract-ocr-eng qt6-translations-l10n
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## Build options

* `-DENABLE_OCR=OFF` – build without text recognition, so Tesseract isn't needed. The PDF export
  still works, without a text layer. With the default `ON`, configuring fails if Tesseract
  (4.1 or newer) isn't found.
* `-DBUILD_TESTS=OFF` – don't build the unit tests.
* `-DBUILD_DEV_TOOLS=ON` – also build the developer tools `binarization_compare` (compares the
  binarization methods), `color_detect` (color mode detection on sample pages), `split_compare`
  (split line detection methods) and `oblique_compare` (oblique detection on synthetic pages or,
  with `--images`, on real scans; see [TESTING.md](TESTING.md)).
* `-DTIFF_REQUIRED_CODECS=...` / `-DTIFF_RECOMMENDED_CODECS=...` – the TIFF compression schemes
  libtiff has to / should support (defaults: `LZW;PACKBITS;CCITT;JPEG;OJPEG;DEFLATE;LZMA` /
  `ZSTD;WEBP;LERC`). `-DSKIP_TIFF_CODEC_CHECK=ON` skips the check.
* `-DPORTABLE_VERSION=OFF` – store the settings in the user profile instead of a `config`
  folder next to the program (default `ON`).
* `-DDEVELOPER_VERSION=ON` – enable the internal debug features (default: `ON` for Debug
  builds, `OFF` otherwise).
* `-DNATIVE_ARCH=ON` – GCC and Clang only: optimize for the CPU of the build machine. The
  program then may not run on other computers.
* `-DENABLE_CODE_ANALYSIS=ON` – extra warnings and static code analysis. Slow; best used in a
  separate build directory (see below).

## Optional: code analysis

None of this is needed for a normal build. The numbered scripts in `scripts\analysis` run the
compiler's static analysis, clang-tidy, cppcheck, the EDG C++ front end, builds with stricter
MSVC switches, a clang-cl build and a build with sanitizers, and write their findings to
`scripts\analysis\results`. What each run does, how to set it up and how to merge the results
is described in [scripts/analysis/README.md](scripts/analysis/README.md).

`scripts\setup\2-setup-analysis.ps1` shows for every run whether its tools are there and
installs the missing ones. By hand, they need, in addition to the steps above:

* For the clang-cl build and the sanitizers: the Visual Studio component
  **"C++ Clang Compiler for Windows"** (Visual Studio Installer → "Modify" → "Individual
  components"). clang-tidy and clang-format come with Visual Studio.
* For the cppcheck run: [cppcheck](https://cppcheck.sourceforge.io/), installed to
  `C:\Program Files\Cppcheck` or on the `PATH`.
* For the EDG run: the Visual Studio component **".NET Framework 4.8.1 SDK"**, and EDG set up
  once with `scripts\setup\3-setup-edg.ps1` (e.g. in `D:\edgcpp`, with `EDG_ROOT` pointing
  there), as described in the README of the scripts.
* `VCPKG_ROOT` set as in step 5 (or `-VcpkgRoot <folder>` passed to the script).

The code formatting is checked on GitHub for every push. To check it beforehand, run the
clang-format that comes with Visual Studio on the changed files, in the "Native Tools Command
Prompt for VS x64":

```
"%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-format.exe" --dry-run -Werror src\path\to\file.cpp
```

`-i` instead of `--dry-run -Werror` formats the files in place.
