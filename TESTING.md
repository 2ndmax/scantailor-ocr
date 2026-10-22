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
./qt_tests
```

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

### imageproc_tests
- **BinaryImage**, **Binarize**, **Morphology**, **RasterOp**, **Scale**, **Shear**, **Transform**, etc.
- **Dpi**: resolution (DPI) and XML serialization.

### qt_tests (Qt Test)
- **Logic tests (TestCoreQt)**: Units, foundation::Utils, SmartFilenameOrdering, QSignalSpy (signals and arguments).
- **UX tests (TestUxQt)**:
  - **testButtonClickEmitsSignal**: a click on a `QPushButton` emits `clicked()` (simulated with `QTest::mouseClick`).
  - **testCheckBoxToggleChangesState**: state change via `setChecked` and the `toggled` signal.
  - **testLineEditAcceptsInput**: text input with `QTest::keyClicks` and a check of the resulting text.
  - **testCollapsibleGroupBoxStateAndSignal**: project widget: collapsed/expanded state and the `collapsedStateChanged` signal.
  - **testErrorWidgetCreation**: creating an `ErrorWidget` and checking its children (without showing a window).

- **Deskew step (TestDeskewQt)**: automatic and manual oblique correction, applying it to other pages, the Apply button waiting for the page.
- **Fix Orientation step (TestFixOrientationQt)**: saving a zero rotation, trim fields following the rotation and keeping their values on a page switch, the view following changes while loading.
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
- **testAllSteps**: the spread is split into 2 pages (4 in all), the skew angle is found within
  0.2°, the content box matches the text block within 10 %, every page is output with the
  resolution of the default parameters, black-and-white output uses CCITT G4, and saving,
  loading and saving the project again gives the same file. With OCR enabled it also checks the
  color mode detection (text page black and white, picture page mixed).
- **testColorOutputCompression**: a color page is written with the TIFF compression set in the
  project (JPEG).
- **testPdfExport**: `StageSequence::pdfExportPages` and `PdfExportOptions::fromProject` build
  the PDF of the test project (no OCR). Checked: complete file, 4 pages in project order,
  page size from pixels and resolution, black-and-white compression as set in the project
  (JBIG2 by default, CCITT G4), JPEG for the color page, and a missing output file makes the
  export fail without touching an existing PDF.

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

## Disabling tests

In environments without Boost or without the test dependencies:

```bash
cmake .. -DBUILD_TESTS=OFF
```
