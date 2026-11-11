# Testing guide (ScanTailor OCR)

## Running the tests

The tests are built by default (`BUILD_TESTS=ON`, see [BUILDING.md](BUILDING.md)). In the build
directory:

```bash
ctest --output-on-failure
```

Or run each test program on its own:

```bash
./core_tests --log_level=message
./foundation_tests --log_level=message
./math_tests --log_level=message
./imageproc_tests --log_level=message
./dewarping_tests --log_level=message
./qt_tests
```

`ctest` also runs the `cli_*` tests: they start the command line tool `scantailor-ocr-cli` and
check only its exit code for `--version`, `--help` and wrong command lines. What the tool does is
tested in `core_tests` (`TestCommandLineRun`).

On Windows, `qt_tests` writes nothing to the console; its results go to files with
`qt_tests -o results.txt,txt`. Each test class writes a file of its own, with its name appended,
e.g. `results_TestCoreQt.txt`. Test functions named on the command line run only in the class
that has them. Without a screen, as in the CI, set `QT_QPA_PLATFORM=offscreen` only together
with `QT_PLUGIN_PATH` pointing to the Qt plugins of vcpkg (`installed\x64-windows\Qt6\plugins`):
without the offscreen plugin, `qt_tests` hangs without a message.

## The test programs

Every module has its own `Test<Module>.cpp` in the `tests` folder of its library.

| Program | Sources | Tests |
|---|---|---|
| `core_tests` | `src/core/tests` | the filters, image reading and writing, project files, the export in all formats, the command line run (Boost.Test) |
| `foundation_tests` | `src/foundation/tests` | constants, geometry helpers, number formatting |
| `math_tests` | `src/math/tests` | automatic differentiation, functions, matrices, splines |
| `imageproc_tests` | `src/imageproc/tests` | binary images, binarization, morphology, scaling, transforms |
| `dewarping_tests` | `src/dewarping/tests` | the dewarping with invalid curves and grids, the tracing of text lines on drawn pages |
| `qt_tests` | `src/qt_tests` | widgets and steps of the user interface without opening windows, and the end-to-end test (Qt Test) |

### End-to-end test (TestEndToEndQt)

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
  scan), and saving, loading and saving the project again gives the same file. It also
  checks the color mode detection (text page black and white, picture page mixed).
- **testColorOutputCompression**: a color page is written with the TIFF compression set in the
  project (JPEG).
- **testPdfExport**: `StageSequence::pdfExportPages` and `ExportOptions::fromProject` build
  the PDF of the test project (no OCR). Checked: complete file (every entry of the
  cross-reference table points at its object), 4 pages in project order, page size from pixels
  and resolution, black-and-white compression as set in the project (JBIG2 by default,
  CCITT G4), JPEG for the color page, and a missing output file makes the export fail without
  touching an existing PDF.

It takes a few seconds. Text recognition is not checked, as the CI runners have no language
files.

Known limit: *Select Content* takes a smooth area without edges (such as a plain color
gradient) for background, so the picture on the test page is made of tiles.

## Writing tests

- When a bug fix needs a regression test, and what happens with a bug found by fuzzing: see
  "Tests" in [CLAUDE.md](CLAUDE.md).
- Tests must not depend on the machine: no user settings, no files outside the test's own
  temporary directory, no image files in the repository (draw the pages in the test, as the
  end-to-end test does).

## Developer tools

Built only with `-DBUILD_DEV_TOOLS=ON` (see [BUILDING.md](BUILDING.md)); the sources are in
`src/tools`. They are measuring aids, not tests: they never fail a test, they write results
for a person to judge. An invalid option or an image that can't be saved gives an exit code
other than 0. The top of each tool's `main.cpp` describes its options and output.

### binarization_compare

Binarizes sample pages with the program's methods and with those of ScanTailor Experimental and
writes a web page to compare the results side by side; with `--colors`, colored text instead.
Pages already in the output folder are only computed again when the image file changed.

```bash
binarization_compare [--colors] <output folder> <image file or folder>...
```

### color_detect

Runs the color mode detection on a folder of scans and writes the verdicts with their evidence
to a CSV file, plus contact sheets. Raw scans of two-page spreads are cut in half first, unless
`--nosplit` is given.

```bash
color_detect <input folder> <output folder> [--nosplit] [--zones] [--mincolor=3] [--minpicture=10] [--only=<name part>] [--inset=<percent>]
```

### split_compare

Runs both methods of finding the split line of two-page spreads (Edges and Spine line) on a
folder of scans and writes one image per scan with the lines each method found, plus a summary
on stdout. With `--project`, only the files of the project, each with its rotation.

```bash
split_compare <input folder> <output folder> [--rotate=<0|90|180|270>] [--only=<name part>] [--project=<project file>]
```

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

The output folder (any folder outside the repository, e.g. `D:\oblique`) gets `report.html` (key
figures per group of pages, conclusions and images of the pages with the largest remaining shear
in `images/`), `results.csv` (one row per page and method) and `summary.txt`. With `--images` it
gets `real_scans.html`, `real_scans.txt` and `real_scans.csv` instead, with the images also in
`images/`.

### dewarp_compare

Measures the automatic dewarping methods of the output step (Auto and Trapezoid) on drawn pages
with a known distortion: six layouts (justified, ragged right, paragraphs, poem, short blocks)
with seven distortions (none, tilt, shear, converging lines, converging edges, both, curved).

```bash
dewarp_compare <output folder> [--seeds=<n>] [--jobs=<n>] [--only=<part of a case name>]
```

- `--seeds`: pages per case, each with other random line lengths and positions (default 10).
- `--jobs`: pages measured at the same time (default half of the cores, at most 6).
- `--only`: only the cases whose name contains this, e.g. `ragged-right` or `_trapezoid`.

The summary (stdout and `summary.txt`) has one row per case and method: the pages without a
model, and two errors in pixels before and after dewarping. "lines" is how far a point of a text
line is from the level of its line, "columns" how far it is from where an even horizontal scale
puts it. The last column counts the pages a model made worse. The worst page of each row is
written as an image with the model drawn on it. The measures and their limits are described at
the top of `src/tools/dewarp_compare/main.cpp`.

### despeckle_compare

Measures the despeckling (`Despeckle.cpp`) and compares it before and after a change. Run a
command once with the old build and once with the new one, then compare.

```bash
despeckle_compare bench <output folder> [--images]
despeckle_compare run <input folder> <output folder> [--dpi=200]
despeckle_compare diff <before folder> <after folder> <output folder>
despeckle_compare content <input folder> [--dpi=200]
```

- `bench`: 40 generated pages with known truth: German text in Times New Roman and Arial, 6 to
  14 pt, at 200, 300, 400 and 600 dpi, with dots above letters, punctuation, dot leaders and
  superscripts; noise of six kinds (salt, dust, fibres, border, specks next to letters,
  show-through), every pixel labelled. The text is antialiased and thresholded with noise like a
  scan. Each page is despeckled at the three levels; the summary gives, per level and
  resolution, the noise removed by kind and the text lost by class. A glyph part (a letter body,
  a dot) is lost when more than half of its pixels are. `bench.csv` has one row per page and level.
  With `--images` also the result at NORMAL in colour (black text kept, blue text lost, red noise
  kept, gray noise removed), crops around lost text per level, and the pages themselves in
  `pages/`. The fonts come from the machine: compare runs on the same machine only.
- `run`: binarizes real scans (Otsu) and despeckles them at the three levels; `diff` compares two
  such runs: `<name>_<level>_diff.png` (red = removed only after the change, green = kept only
  after it) and `_spots.png` with enlarged crops around the changes, plus pixel counts. Take the
  real resolution for `--dpi`, not the one stored in the file.
- `content`: prints the content box found on each scan. The content detection despeckles too
  (NORMAL at 150 dpi), so a change of the despeckling may move it. Run it on the `pages/` of
  `bench --images` (one folder per resolution) to see it on noisy pages.

For a change of the despeckling: the text losses in `bench` must not rise, the `diff` images of
real scans must show no lost text, and the content boxes must not get worse.

## Code analysis and fuzzing

Besides the tests, the scripts in `scripts\analysis` check the code on Windows with several
compilers, analysis tools, sanitizers and Application Verifier, and merge their findings into one
table; [scripts/analysis/README.md](scripts/analysis/README.md) describes each run.

Fuzzing (run 10) builds the fuzz targets of `src/fuzz` with `-DBUILD_FUZZERS=ON` in a build
directory of its own (`build-fuzz`; the option leaves out the program, the tests and the dev
tools) and runs them for a chosen time, e.g. one target for one minute:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analysis\10-fuzz.ps1 -Only fuzz_page_output -Minutes 1
```

Inputs that make a target fail go to `scripts\analysis\results\10-fuzz\<target>\findings`.
Fuzzing is not part of the CI: it needs minutes to hours per target.
