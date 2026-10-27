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
vcpkg install --recurse qtbase qtsvg qttools qttranslations libjpeg-turbo libpng "tiff[core,jpeg,zip,lzma,zstd,webp,lerc,libdeflate,tools]" openjpeg zlib boost-test boost-foreach boost-intrusive boost-multi-index boost-lambda tesseract
```

`tesseract` is only needed for text recognition in the PDF export (see the build options
below).

This compiles Qt and everything else from source and takes a while – plan for an hour or more
and several gigabytes of disk space.

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
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -Version <x.y.z>
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

None of this is needed for a normal build. The scripts in `scripts\analysis` run the compiler's
static analysis, clang-tidy, cppcheck, a clang-cl build and a build with sanitizers, each in a
build directory of its own, and write their findings to log files. What each run does and how
to merge the results is described in [scripts/analysis/README.md](scripts/analysis/README.md).

They need, in addition to the steps above:

* For the clang-cl build and the sanitizers: the Visual Studio component
  **"C++ Clang Compiler for Windows"** (Visual Studio Installer → "Modify" → "Individual
  components"). clang-tidy and clang-format come with Visual Studio.
* For the cppcheck run: [cppcheck](https://cppcheck.sourceforge.io/), installed to
  `C:\Program Files\Cppcheck` or on the `PATH`.
* `VCPKG_ROOT` set as in step 5 (or `-VcpkgRoot <folder>` passed to the script).

The code formatting is checked on GitHub for every push. To check it beforehand, run the
clang-format that comes with Visual Studio on the changed files, in the "Native Tools Command
Prompt for VS x64":

```
"%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-format.exe" --dry-run -Werror src\path\to\file.cpp
```

`-i` instead of `--dry-run -Werror` formats the files in place.
