set(sources
    main.cpp
    TestContentSpanFinder.cpp
    TestDeskewParams.cpp
    TestDeviationProvider.cpp
    TestObliqueFinder.cpp
    TestImageId.cpp
    TestImageReaders.cpp
    TestMargins.cpp
    TestPageId.cpp
    TestPageRange.cpp
    TestPageOrder.cpp
    TestPageSequence.cpp
    TestPdfWriter.cpp
    TestNewScanTracker.cpp
    TestPdfPageOrder.cpp
    TestProjectFile.cpp
    TestRenderParams.cpp
    TestScanInsertion.cpp
    TestJbig2.cpp
    TestOcrLanguages.cpp
    TestOutputFileNames.cpp
    TestOutputGenerator.cpp
    TestOutputSize.cpp
    TestPhotoAdjustments.cpp
    TestSelectContentApply.cpp
    TestSmartFilenameOrdering.cpp
    TestSplitDetection.cpp
    TestTextLineRefiner.cpp
    TestThumbnailFiles.cpp
    TestUnits.cpp
    TestWidthCorrection.cpp
    TestWidthProfile.cpp
    TestUtils.cpp)
# The color mode detection comes with OCR (Leptonica).
if (TESSERACT_TARGET)
  list(APPEND sources TestColorModeDetector.cpp)
endif()

add_executable(core_tests ${sources})
target_link_libraries(
    core_tests
    PRIVATE core dewarping TIFF::TIFF JPEG::JPEG ${OPENJPEG_TARGET} Boost::unit_test_framework ${EXTRA_LIBS})

add_test(NAME core_tests COMMAND core_tests --log_level=message)
set_unit_test_environment(core_tests)

# Validates a PDF/A file if veraPDF is installed (see TestPdfWriter.cpp).
find_program(VERAPDF_EXECUTABLE NAMES verapdf verapdf.bat)
if (VERAPDF_EXECUTABLE)
  set_property(TEST core_tests APPEND PROPERTY ENVIRONMENT "VERAPDF=${VERAPDF_EXECUTABLE}")
endif()