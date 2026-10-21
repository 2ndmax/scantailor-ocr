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

The UX tests use `QApplication` and can simulate interaction (clicks, keyboard) without opening windows; they serve as a basis for extending coverage to more widgets or dialogs.

## UI/UX tests and usage paths

Apart from the widget-level UX tests in `qt_tests` (see above), there are currently no automated tests of the main window, the dialogs or complete usage flows. To improve coverage of UX and usage paths, you can:

1. **Qt Test**: Extend `qt_tests` (TestUxQt) to:
   - Create more widgets and dialogs without showing a window.
   - Simulate events (clicks, keyboard) and check the state.

2. **Integration tests**: Scripts or tests that:
   - Open sample projects.
   - Run filters and check the results on disk or in memory.

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
