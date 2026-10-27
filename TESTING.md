# Testing guide (ScanTailor OCR)

## Running unit tests

With the project configured with `BUILD_TESTS=ON` (the default):

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

Or run each suite separately:

```bash
./core_tests --log_level=message
./foundation_tests --log_level=message
./math_tests --log_level=message
./imageproc_tests --log_level=message
./dewarping_tests --log_level=message
./qt_tests
```

On Windows, `qt_tests` writes nothing to the console; its results go to files with
`qt_tests -o results.txt,txt`. Each test class writes a file of its own, with its name
appended: `results_TestCoreQt.txt`, `results_TestUxQt.txt`, `results_TestDeskewQt.txt`,
`results_TestFixOrientationQt.txt`, `results_TestSelectContentQt.txt` and `results_TestEndToEndQt.txt`. Test functions named
on the command line run only in the class that has them.
Without a screen, as in the CI, set `QT_QPA_PLATFORM=offscreen` only together with
`QT_PLUGIN_PATH` pointing to the Qt plugins of vcpkg (`installed\x64-windows\Qt6\plugins`):
without the offscreen plugin, `qt_tests` hangs without a message.

## Coverage by module

### core_tests
- **ContentSpanFinder**: detection of content spans.
- **DeskewParams**: parameters of the deskew filter.
- **ImageId** / **PageId**: identification of images and pages, ordering, sub-pages.
- **Margins**: margins and XML serialization.
- **PageRange**: page ranges and alternating selection.
- **SelectContentApply**: applying the content selection filter.
- **SmartFilenameOrdering**: natural ordering of file names.
- **Units**: conversion and representation of units (px, mm, cm, in).

### foundation_tests
- **Constants**: mathematical constants (PI, DPI/DPM conversions, degrees/radians).
- **LineIntersectionScalar**: line intersection (scalars).
- **Proximity**: point-to-point and point-to-segment distance.
- **Utils**: locale-independent number-to-string conversion.

### math_tests
- **Hessians** (adiff): automatic differentiation.
- **LineBoundedByRect**: clipping a line to a rectangle.
- **LinearFunction** / **QuadraticFunction**: linear and quadratic functions.
- **MatrixCalc**: matrix operations.
- **SidesOfLine**: which side of a line points lie on.
- **SqDistApproximant**: squared distance approximation.
- **XSpline**: sampling ends with NaN or huge coordinates; t outside [0, 1]; a single control point.

### dewarping_tests
- **Dewarping**: curves with NaN or infinity are invalid; dewarping with NaN grid points; large footprints keep the gray level.

### imageproc_tests
- **BinaryImage**, **Binarize**, **Morphology**, **RasterOp**, **Scale**, **Shear**, **Transform**, etc.
- **Dpi**: resolution (DPI) and XML serialization.

### qt_tests (Qt Test)
- **Logic tests (TestCoreQt)**: Units, foundation::Utils, SmartFilenameOrdering.
- **UX tests (TestUxQt)**:
  - **testCollapsibleGroupBoxStateAndSignal**: project widget: collapsed/expanded state and the `collapsedStateChanged` signal.
  - **testCollapsedStateIsLoadedForGroupDisabledAtStart**: a group that is disabled when first shown still gets its saved collapsed state.
  - **testErrorWidgetShowsItsText**: an `ErrorWidget` passes its text and text format to its label (without showing a window).
  - **testStyledIconFollowsPaletteColor**: styled icons keep their colored renderer, but take a new palette color.
  - **testThumbnailDoesNotKeepThePreviousOne**: a thumbnail looks the same whichever was painted before it through the shared temporary pixmap.
  - **testThumbnailBorderIsTheAccentColor**: the selected thumbnail and its marks take the scheme's or the chosen accent color.
  - **testColorSchemesSetTheSameParameters**: Light, Dark and the native scheme set every color parameter.
  - **testTextOnAccentHasTheHigherContrast**: text and marks on the accent color are light or dark, whichever has more contrast to it, also for a chosen accent color.
  - **testNativeSchemeFollowsTheSameMeanings**: the native scheme takes its color parameters from the system palette by the same meanings as Light and Dark (accent, text on the accent).

- **Deskew step (TestDeskewQt)**: automatic and manual oblique correction, applying it to other pages, the Apply button waiting for the page.
- **Fix Orientation step (TestFixOrientationQt)**: saving a zero rotation, trim fields following the rotation and keeping their values on a page switch, the view following changes while loading.
- **Select Content step (TestSelectContentQt)**: changed detection settings make the task detect the content box again, going back to those it was detected with does not, also for pages from before these settings.
- **End-to-end test (TestEndToEndQt)**: see below.

The UX tests use `QApplication` and can simulate interaction (clicks, keyboard) without opening windows; they serve as a basis for extending coverage to more widgets or dialogs.

## End-to-end test (TestEndToEndQt)

Runs the whole processing chain without the user interface, the way the application does,
and checks properties of the result rather than pixels, so it holds across compilers and
systems. No image files are stored in the repository: the scans are drawn in the test.

- **Pages**: a text page turned by 1.5° with a dark border, a two-page spread with a dark
  gutter, and a page with a colored picture above black text.
- **Flow**: `ProjectPages::createNew` and `StageSequence::startNewProject` with the standard
  default parameters, so the profile and program settings of the computer play no role. First
  all images up to *Split Pages* (only then is it known which images hold two pages), then
  all pages up to *Output*, with `StageSequence::createTask` as in batch processing.
- **testAllSteps**: the spread is split at its gutter into 2 pages (4 in all), the skew angle
  is found within 0.2°, the content box matches the text block within 10 %, every page is
  output with the resolution of the default parameters, black-and-white output uses CCITT G4,
  every output page has 10 to 25 % dark pixels (neither empty nor with the dark border of the
  scan), and saving, loading and saving the project again gives the same file. With OCR enabled
  it also checks the color mode detection (text page black and white, picture page mixed).
- **testColorOutputCompression**: a color page is written with the TIFF compression set in the
  project (JPEG).
- **testPdfExport**: `StageSequence::pdfExportPages` and `PdfExportOptions::fromProject` build
  the PDF of the test project (no OCR). Checked: complete file (every entry of the
  cross-reference table points at its object), 4 pages in project order, page size from pixels
  and resolution, black-and-white compression as set in the project (JBIG2 by default,
  CCITT G4), JPEG for the color page, and a missing output file makes the export fail without
  touching an existing PDF.

It takes a few seconds. Text recognition is not checked, as the CI runners have no language
files.

Known limit: *Select Content* takes a smooth area without edges (such as a plain color
gradient) for background, so the picture on the test page is made of tiles.

## UI/UX tests and usage paths

Apart from the widget-level tests and the end-to-end test of the processing chain in `qt_tests` (see above), there are currently no automated tests of the main window, the dialogs or usage flows through the user interface. To improve coverage of UX and usage paths, you can:

1. **Qt Test**: Extend `qt_tests` (TestUxQt) to:
   - Create more widgets and dialogs without showing a window.
   - Simulate events (clicks, keyboard) and check the state.

2. **Integration tests**: Extend TestEndToEndQt with more kinds of pages or settings, or open
   existing sample projects and check the results on disk.

3. **E2E tests**: Tools such as Squish or scripts that drive the graphical application (more costly to maintain).

To measure code coverage with gcov/lcov (only with compilers that support it):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DCMAKE_CXX_FLAGS="--coverage"
cmake --build .
ctest
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Developer tools

Built only with `-DBUILD_DEV_TOOLS=ON` (see [BUILDING.md](BUILDING.md)). They are measuring
aids, not tests: they never fail, they write results for a person to judge.

### oblique_compare

Measures the automatic oblique (shear) detection on synthetic pages with a known angle: the
program's method (`deskew::Task::findObliqueAngle`, after `findDeskewAngle` as in the deskew step)
against the candidates in `Methods.h` (any straight text edge, stroke direction, both combined).
Each page is measured a second time moved by a fraction of a pixel, which shows how stable the
angle is. With `--images=<folder>` it measures real scans instead, as they are and with known
shears added, and shows each straightened by every method side by side.

```bash
oblique_compare <output folder> [--pages=<n>] [--seed=<n>] [--jobs=<n>]
oblique_compare <output folder> --images=<folder> [--jobs=<n>]
```

- `--pages`: number of pages of the fixed sample (default 243). Text amount, layout, alignment
  (justified, ragged right, centered) and resolution (150/300/600 dpi) go round in turn, every
  combination once in 81 pages. Random, the angles on no grid: rotation (±5°); shear 0° on one
  page in eight, 0° with a keystone (perspective, 1–6 %) on one, 0.1° to 1° on two, 1° to 4° on
  four; disturbance (none, noise, blur); Arial or Times New Roman; italics (none, some phrases, the
  whole page); running heads and page numbers; indented paragraphs; list items.
- `--seed`: start value of the pseudo-random sequence (default 20261026); the same seed always
  gives the same pages.
- `--jobs`: pages measured at the same time (default half of the cores, at most 6). A 600 dpi
  page needs several hundred megabytes.

The output folder (suggested: `ANALYSE/scherung/`, local only) gets `bericht.html` (German key
figures per group of pages, conclusions and images of the pages with the largest remaining shear
in `bilder/`), `ergebnisse.csv` (one row per page and method) and `zusammenfassung.txt`.

## Code analysis

Besides the tests, the scripts in `scripts\analysis` check the code with several compilers and
analysis tools (MSVC code analysis, clang-tidy, cppcheck, the EDG C++ front end, clang-cl,
sanitizers) and merge their findings into one table. They are numbered in the order in which
they are best run; [scripts/analysis/README.md](scripts/analysis/README.md) describes each run,
its setup and its output.

## Disabling tests

In environments without Boost or without the test dependencies:

```bash
cmake .. -DBUILD_TESTS=OFF
```
