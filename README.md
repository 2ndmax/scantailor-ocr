# ScanTailor OCR – scanned pages to searchable PDF

ScanTailor OCR cleans up scanned pages and turns them into a **compact, searchable PDF**:

* **PDF export** built in, directly from the project's output: lossless JBIG2 for black and
  white pages, layered pages (text over a small JPEG picture) for mixed pages.
* **Text recognition (OCR)** with Tesseract adds an invisible text layer, so the PDF can be
  searched and its text copied. More languages can be downloaded in the program.

ScanTailor OCR is based on [ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced)
1.2.1, an interactive post-processing tool for scanned pages (page splitting, deskewing, content
selection, margins, dewarping and more).

**For the page processing features and how to use them, please see the
[documentation of ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced#readme).**
Everything described there applies to ScanTailor OCR as well.

Downloads for Windows and Linux are on the [releases page](https://github.com/2ndmax/scantailor-ocr/releases).

The changes were developed with the help of Claude (Anthropic).

## Changes compared to ScanTailor Advanced 1.2.1

### Name and version

* The program is now called **ScanTailor OCR**, starting with version **1.0.0**. The program
  file is `scantailor-ocr` (`scantailor-ocr.exe`), the Linux package `scantailor-ocr`, so it can
  be installed next to ScanTailor Advanced.
* On the first start, the settings, saved default profiles and OCR languages of ScanTailor
  Advanced (`scantailor-advanced`) are copied over once. The old files are kept.
* The project file format (`.ScanTailor`) is unchanged; projects remain compatible with
  ScanTailor Advanced.

### PDF export

* **New step "7 Create PDF"** in the list of steps, after "Output". It combines the existing
  output files of the project into one PDF. It doesn't process pages itself: pages that haven't
  been output yet are shown greyed out. There's no separate window: the page list, the PDF file
  and the progress take the place of the page view, the options are shown on the left like
  those of the other steps, and the thumbnails are hidden. The step has no batch processing
  button.
* The pages are shown as tiles in project order, from left to right and then on the next row,
  each with its thumbnail, file name and kind of page. Each page can be ticked or unticked
  ("All" / "None"); the ticked pages are numbered with their position in the PDF, the others are
  shown faded. The order can be changed with drag and drop or with "Move forward" /
  "Move back". Pages not output yet get a placeholder in the proportions of the other pages.
* The ticks and the order are kept while the project is open, also when going back to an
  earlier step and outputting pages again. Each time the step is shown, the pages are read
  again: new pages appear ticked after the page that precedes them in the project, removed
  pages disappear. The selection and order aren't saved, and the project file isn't changed.
* While a PDF is being created, the other steps and opening, creating or closing a project are
  locked. Closing the program asks whether to cancel the PDF.
* Small files, similar in size to Adobe Acrobat's output:
  * Black and white pages are stored losslessly as **JBIG2** (default) or CCITT G4.
  * Mixed pages with **split output** are stored in layers. Only the picture area of the
    background is stored, as JPEG at reduced resolution (full, half or one third). The text is
    painted losslessly on top as a JBIG2 or G4 mask.
  * All other pages are stored as JPEG. Mixed pages without split output get a warning, as they
    make the PDF much larger.
* **JBIG2** makes black and white pages about a third smaller than CCITT G4, with exactly the
  same pixels. Only JBIG2's lossless "generic region" coding is used, never the symbol mode
  that stores similar-looking letters only once and can swap characters (such as 6 and 8).
  The arithmetic coder is taken from [jbig2enc](https://github.com/agl/jbig2enc) (Apache License
  2.0, see `src/core/jbig2enc`); no extra library is needed. CCITT G4 remains selectable for
  very old PDF programs.
* JPEG quality (default 85), picture resolution, black and white compression and "open the PDF
  after creating it" are kept as program settings, saved as soon as they are changed. By default
  the PDF is saved next to the project file and named after it.
* Several pages are prepared in parallel, with progress display and cancelling. The PDF is
  written to a temporary file first, so a failed or cancelled export never leaves a broken file
  behind or damages an existing one.
* **Text recognition (OCR)** with [Tesseract](https://github.com/tesseract-ocr/tesseract) adds
  an invisible text layer, so the text of the PDF can be searched, selected and copied. The page
  images stay unchanged; the text layer adds only a few kilobytes per page.
  * Several languages can be ticked at once (e.g. German and English). The text layout, which
    decides the order in which the text is recognized, can be detected automatically or set to a
    single column or a single block of text.
  * Pages with split output are recognized on their foreground, i.e. the text without the
    pictures.
  * Several pages are recognized in parallel (number of processor cores minus 2, at most 16).
  * Language files (`*.traineddata`, best from
    [tessdata_best](https://github.com/tesseract-ocr/tessdata_best)) are looked up in a
    `tessdata` folder next to the program, in a `tessdata` folder in the user's application
    data folder (on Windows `%APPDATA%\scantailor-ocr\scantailor-ocr\tessdata`) and in the folder
    `TESSDATA_PREFIX` points to; on Linux also in the system folders of the
    `tesseract-ocr-*` packages. Files put there by hand are found as well.
  * **More languages can be downloaded in the program** ("More languages ..."). The list comes
    from GitHub (tessdata_best, including the script models such as "Latin" or "Fraktur"), with
    search and file sizes. Several languages can be downloaded at once. Each file is checked
    against GitHub's checksum and only appears once it's complete. Downloads go to `tessdata`
    next to the program, or to the user's folder if the program folder can't be written to.
    The system's proxy settings are used. Without an internet connection, the installed
    languages can still be used.
  * Tesseract needs all languages of a run in one folder. If the ticked languages are in
    different folders, the missing files are copied into the user's folder once.
  * The invisible text uses Tesseract's "GlyphLessFont" (Apache License 2.0), embedded once per
    PDF.

### Deskew: oblique correction reworked

* The oblique (shear) correction is switched the same way as the deskew rotation now: two wide
  **Auto / Manual buttons** instead of a check box, both in the page options panel and in the
  default parameters dialog, in a consistent layout (heading, buttons, angle).
* **Fixed: the deskew "Auto" button lost its highlight.** All four mode buttons (deskew and
  oblique) were auto-exclusive inside one group box, and Qt groups such buttons per parent
  widget, so choosing an oblique mode silently unchecked the deskew mode. Each pair now has
  its own button group.
* Switching oblique to *Manual* resets an automatically found shear angle to 0, so the shear
  isn't left applied with automatic correction turned off.
* "Apply oblique automatically" acts as a master switch: stored per-page parameters can no
  longer re-enable oblique correction while it is off.
* The oblique finder reports *no* angle when the image has too little structure to tell an
  angle from noise, instead of shearing the page by an arbitrary amount.
* The "apply to other pages" dialog no longer accepts a selection that would apply neither
  the deskew nor the oblique angle.
* The drag handles for rotation and oblique correction sit closer to the image center, away from
  the edge of the view. How close is adjustable under *Settings → General → Deskew handle distance*
  (default 75 %).
* The project and profile XML format is unchanged, so existing projects and profiles keep working.

### New project

* The "Project Files" dialog has a third line, **Project File**. It suggests
  `<input folder>\<folder name>.ScanTailor` and follows the input folder until it is changed.
  The project is saved there as soon as the dialog is confirmed; an existing file is only
  overwritten after asking.
* With **Import new scans** ticked, a project may start without any images. Such an empty
  project opens with the processing steps and the page list instead of the start page. Its
  project file keeps the input folder as a directory without files; other ScanTailor versions
  can't open a project without images, but open it normally once it contains one.

### Image import

* **TIFF reading reworked**
  * Tiled TIFF files can be opened (previously, bi-level, grayscale and palette images failed).
  * Floating point, signed integer and 32-bit images are supported.
  * Colors of palette images in big-endian ("Motorola") TIFF files are correct now.
  * Fixed undefined behaviour when reading 2 and 4 bit images.
  * Read errors no longer yield images with uninitialised memory in the unread rows.
  * TIFF files using a compression the program can't decode are rejected right away when
    importing, naming the compression.
* **JPEG 2000 import** (`.jp2 .j2k .j2c .jpc .jpf .jpx .jph .jhc`) through OpenJPEG:
  fast import (only the file header is read), thumbnails decoded at reduced resolution,
  multi-threaded decoding, huge images decoded strip by strip to limit memory use.
* **Faster thumbnails for JPEG** files, decoded directly at reduced size.
* Fixed a leak and undefined behaviour in the JPEG metadata reader (libjpeg's error handling
  jumped past the cleanup of C++ objects) and a buffer bug in the PNG reader on partial reads.

### Output: split output with "Original background"

The *Original background* option of the split output (mixed mode) keeps the paper of the text
areas as scanned – with its tone and texture – under crisp black text, and writes this paper
layer to `out/original_background`. Three bugs, also present in the original project, kept it
from working:

* **Fixed a crash** when processing a page with this option and dewarping turned off: the
  binarized text was released from memory before its last use.
* **Fixed darkened pictures with black blobs:** the binarized text layer of the whole page was
  painted over the pictures, too. It is now limited to the text areas, with and without dewarping.
* **Fixed the paper tone being lost:** the fill zone step converted the text areas back to pure
  black and white, even on pages without fill zones, so the option had no visible effect.
  In this mode fill zones are now painted directly into the image.

### Output: grayscale output and pages without content

* **New option "Grayscale output"** in the output options and in the default parameters
  (profiles), for *Color / Grayscale* and *Mixed* mode. A color scan is then processed like a
  grayscale scan: the whole page in color mode, pictures and colored text in mixed mode come out
  in grayscale, with much smaller files. *Black and white* mode is not affected, as its color
  segmentation keeps colored text on purpose. The option is stored in the project only when
  it is turned on; other versions ignore it (and produce color output again).
  ([upstream #179](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/179))
* **Fixed a crash on mixed pages without content box** when the split output is turned on,
  and the message "1 output files could not be written" when it is off. Such pages now come out
  as white pages, with white split layers.
  ([upstream #172](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/172))

### Zone editors (picture zones and fill zones)

* The selection mode for new zones is shown at the bottom right of the status bar as a button
  with icon and name ("Polygon selection", "Lasso selection", "Rectangle selection").
  Clicking it opens a menu to change the mode; the keys Z, X and C still work, and the last
  used mode is kept, also across restarts.
  ([upstream #15](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/15))
* The hints in the status bar while drawing a zone explain each mode, including the lasso,
  and follow the mouse: the "click to finish" hint no longer stays after moving away from the
  starting point.

### Translations

* Qt's own texts – standard buttons such as "Cancel", parts of the file dialogs, the context
  menu of text fields – are translated now as well, into the language chosen in the settings.
  The Windows ZIP ships Qt's translations for the program's languages in `translations`; on
  Linux they come from the system (`qt6-translations-l10n`, recommended by the `.deb`).

### Start page

* "New Project" and "Open Project" are buttons now instead of blue links.
* Recent projects are listed with their name in the normal text colour and their folder
  below in smaller, slightly faded text; an entry is highlighted in grey under the mouse and
  opens on click or with Enter/Space. The full path is shown as a tooltip.
* The start page has the window background, so its text is no longer drawn on the mid-grey
  image area of the light colour scheme.

### Error reporting

* When images can't be loaded or output files can't be written, the reason is shown –
  collected in a single, non-modal message instead of one message per file.
* An unexpected error while processing a page no longer terminates the program without a
  message. The page shows the error with its reason instead of the image, the failure is
  listed in the same collected message, and batch processing continues with the other pages.
  Errors in the user interface are reported with a message as well. (Hard crashes, such as
  memory access violations, still can't be caught this way; they have to be fixed one by one.)
* Output TIFF files are checked to have been written completely (e.g. on a full disk);
  a failed write no longer leaves a damaged file behind.
* Malformed values in project files (e.g. a missing binarisation threshold) no longer turn
  into settings that black out a page.

### Correctness and robustness

* Fixed several defects that produced wrong results or crashes: a grayscale measurement that
  read the wrong image, a division by zero in Wolf binarisation on blank pages, integer
  overflows in pixel arithmetic (TIFF buffers, binarisation, distance transform) on large
  images, missing guards in `BinaryImage`, a wrong assertion and a lost search direction in
  the arc length mapper, an unsigned wraparound in the page split gap scan, and divisions by
  zero in the content finder and with missing resolution (DPI) information.
* Fixed a use-after-free when finishing a lasso zone, and thread-safety issues with the
  application settings, the default parameter profiles and the deviation statistics used for
  sorting thumbnails.
* Natural file name sorting now also compares the separators, so names differing only in
  those (`img-2.tif` vs. `img_1.tif`) sort by the whole name rather than falling back to a
  plain lexicographic compare. **This can change page order in projects with inconsistent
  file naming.**
* **Translations work again.** Since the executable was renamed to `scantailor-advanced`, the
  program looked for translation files with the old name and found none, so it always ran in
  English. It now finds them. On first start it also picks the translation for the system
  language when only the language matches (e.g. `de` for `de_DE`). The German translation is
  complete again, including all texts added in this fork.
* The thumbnail list keeps its position when re-sorting moves the current page elsewhere.
* Auto-save is also triggered by changes to the page list and to the current page.
* If the output folder has been deleted (e.g. to output everything again), opening the project
  creates it again instead of asking for relinking, as long as the folder it belongs into and
  all images of the project are still there. Otherwise the project has probably been moved to
  another place or computer, and relinking is offered as before.
* Switching back to a step after batch processing or after "Create PDF" no longer connects the
  signals of its options panel a second time.
* Code cleanups based on the compiler's static code analysis.

### Build and tests

* CMake verifies which compression schemes libtiff supports (LZMA etc. are required).
* Optional static code analysis: `-DENABLE_CODE_ANALYSIS=ON` (MSVC: `/W4 /analyze`).
* New tests for the image readers and the TIFF writer; CI now fails on failing tests and also
  builds on Windows.
* New tests for the PDF export: file structure and cross-reference offsets, page size from the
  resolution, lossless G4 and JBIG2 round trips (JBIG2 with a decoder written from the standard
  for the tests, and on Linux additionally rendered by Poppler's `pdftoppm` and compared pixel
  by pixel), cropping of split backgrounds, page order, text layer,
  finding and gathering language files, download checksums.
* CI installs Tesseract on Linux and Windows; the Flatpak is built without text recognition for
  now, as its runtime doesn't include Tesseract.
* CI builds Linux with both Qt 5 and Qt 6; the Linux packages (DEB, RPM, AppImage) are built
  with Qt 6, like the Windows version. The Windows job keeps vcpkg's intermediate files off the
  runner's small system drive and only saves a new package cache when something was rebuilt.
* The Lint workflow uses clang-format 22.1.3, the version that comes with Visual Studio 2026, so
  that a local check gives the same result (other versions format some lines differently).
* The Flatpak manifest (now `flatpak/io.github._2ndmax.ScanTailorOCR.json`) builds this fork's own code
  instead of upstream's version 1.1.1, with the KDE 6.10 runtime (Qt 6), Boost as an extra
  module and the correct program name. The Flatpak workflow can be started by hand.
* Fixed compiler warnings: a possibly dangling reference in "Go To Page", the member
  initialization order in the margins options, and an obsolete combo box setting that Qt 6
  ignored anyway.
* The `update_translations` target works with Qt 6 again: it passes a file list to `lupdate`
  instead of a qmake project file, which Qt 6's `lupdate` no longer reads, and includes the
  sources of the application itself (main window and dialogs) again.
* A script creates the portable Windows ZIP (`scripts\package-windows.ps1`, see below).
* The release workflow builds the portable Windows ZIP as well, with the same script, and
  attaches it to the release together with the Linux packages. The shipped OCR languages
  (German and English from tessdata_best) are fixed to one commit and checked against their
  SHA-256. Started by hand ("Run workflow"), it builds all packages as a test, without
  creating a release.
* The `update_translations` target no longer refers to `Qt6::lupdate` by name, which broke the
  Qt 5 fallback build. A review of the Qt 6 port found no other problem: the code already
  guards every API removed in Qt 6.

## Building

### Windows, step by step

These steps assume a machine with nothing installed yet. Neither JOM nor vcpkg comes with an
installer: they are simply unpacked into a folder of your choice. The examples below use
`C:\Dev` as that folder – any path works, as long as you use it consistently and it contains
no spaces or non-English characters.

**1. Get the source code** into `C:\Dev\scantailor-ocr`, either with
[Git for Windows](https://git-scm.com/download/win):

```
cd /d C:\Dev
git clone https://github.com/2ndmax/scantailor-ocr.git
```

or by downloading the ZIP of this repository ("Code" → "Download ZIP") and unpacking it there.

**2. Install Visual Studio Community** (the free edition):
<https://visualstudio.microsoft.com/vs/community/>
In the installer pick the workload **"Desktop development with C++"**. Of its optional
components, only these are needed:

* MSVC build tools (x64)
* C++ CMake tools for Windows
* Windows 11 SDK

**3. Download JOM** (a parallel `nmake` replacement): <https://wiki.qt.io/Jom>
Unpack it to `C:\Dev\jom`, so that `C:\Dev\jom\jom.exe` exists.

**4. Download vcpkg** as a ZIP: <https://github.com/microsoft/vcpkg>
Unpack it to `C:\Dev\vcpkg`.

**5. Build the libraries with vcpkg.** Open the **"Native Tools Command Prompt for VS x64"**
(from the start menu, inside the Visual Studio folder) and run:

```
cd /d C:\Dev\vcpkg
bootstrap-vcpkg.bat
vcpkg install --recurse qtbase qtsvg qttools qttranslations libjpeg-turbo libpng "tiff[core,jpeg,zip,lzma,zstd,webp,lerc,libdeflate,tools]" openjpeg zlib boost-test boost-foreach boost-intrusive boost-multi-index boost-lambda tesseract
```

`tesseract` is only needed for text recognition in the PDF export (see the build options
below).

This compiles Qt and everything else from source and takes a while – plan for an hour or more
and several gigabytes of disk space.

**6. Set the environment variables.** Open **PowerShell as administrator** and run (adjust the
two paths if you used a different folder):

```powershell
$vcpkgRoot = "C:\Dev\vcpkg"
$jomRoot   = "C:\Dev\jom"
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "Machine")
[System.Environment]::SetEnvironmentVariable("JOM_ROOT",   $jomRoot,   "Machine")
$currentPath = [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
[System.Environment]::SetEnvironmentVariable("PATH", "$currentPath;$vcpkgRoot;$jomRoot", "Machine")
```

Then **restart the computer**, so every program sees the new variables.

**7. Configure and build.** In the "Native Tools Command Prompt for VS x64":

```
cd /d C:\Dev\vcpkg
vcpkg integrate install

cd /d C:\Dev\scantailor-ocr
mkdir build
cd build
cmake -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ..
jom -j 10
```

`-j 10` is the number of parallel compiler processes; use roughly the number of processor cores.
The finished `scantailor-ocr.exe` and all needed DLLs end up in the `build` directory.
For text recognition, download languages in the program (step "Create PDF" → "More languages ..."),
or put the language files into `build\tessdata` by hand, e.g. for German and English
(about 24 MB together):

```
mkdir tessdata
curl -L -o tessdata\deu.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/deu.traineddata
curl -L -o tessdata\eng.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/eng.traineddata
```

**8. Run the tests** (optional), in the same `build` directory:

```
ctest -C Release --output-on-failure
```

**9. Create the portable ZIP** (optional), from the repository folder:

```
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -Version 1.0.0
```

The script collects the program, exactly the DLLs it needs (read from the import tables, so
test libraries are left out), the Qt plugins, the translations, the OCR languages German and
English from `build\tessdata` and the license texts of all components (from vcpkg) into
`build\package`. It then starts a copy with a minimal `PATH` for a few seconds to check that
nothing is missing, and packs the ZIP. `-Languages` selects other languages to ship,
`-SkipStartTest` skips the start test; see the comments at the top of the script.
The ZIPs on the releases page are built the same way by GitHub Actions.

When configuring again later, e.g. after changing build options, add `--fresh` to the `cmake`
call to discard the cached configuration:

```
cmake --fresh -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ..
```

### Linux (Debian / Ubuntu)

With Qt 6 (recommended; the Linux packages from CI are built this way):

```
sudo apt install build-essential cmake \
  qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6opengl6-dev libgl-dev \
  libboost-test-dev libboost-dev libjpeg-dev libpng-dev libtiff-dev zlib1g-dev libopenjp2-7-dev \
  libtesseract-dev libleptonica-dev tesseract-ocr-deu tesseract-ocr-eng qt6-translations-l10n
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Qt 5 still works: install `qtbase5-dev libqt5svg5-dev qttools5-dev qttools5-dev-tools
libqt5opengl5-dev` instead of the Qt 6 packages. If both are installed, CMake picks Qt 6;
add `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON` to the `cmake` call to use Qt 5.

### Build options

* `-DTIFF_REQUIRED_CODECS=...` / `-DTIFF_RECOMMENDED_CODECS=...` – the TIFF compression schemes
  libtiff has to / should support (defaults: `LZW;PACKBITS;CCITT;JPEG;OJPEG;DEFLATE;LZMA` /
  `ZSTD;WEBP;LERC`). `-DSKIP_TIFF_CODEC_CHECK=ON` skips the check.
* `-DENABLE_CODE_ANALYSIS=ON` – extra warnings and static code analysis. Slow; best used in a
  separate build directory.
* `-DBUILD_TESTS=OFF` – don't build the unit tests.
* `-DENABLE_OCR=OFF` – build without text recognition, so Tesseract isn't needed. The PDF export
  still works, without a text layer. With the default `ON`, configuring fails if Tesseract
  (4.1 or newer) isn't found.

## License

GNU GPLv3, see [LICENSE](LICENSE). This is a modified version of ScanTailor Advanced;
the modifications are documented above and in the commit history.
