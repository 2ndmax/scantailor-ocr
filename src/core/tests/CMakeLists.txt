set(sources
    main.cpp
    TestContentSpanFinder.cpp
    TestDeskewParams.cpp
    TestObliqueFinder.cpp
    TestImageId.cpp
    TestImageReaders.cpp
    TestMargins.cpp
    TestPageId.cpp
    TestPageRange.cpp
    TestPageSequence.cpp
    TestPdfWriter.cpp
    TestNewScanTracker.cpp
    TestPdfPageOrder.cpp
    TestProjectFile.cpp
    TestScanInsertion.cpp
    TestJbig2.cpp
    TestOcrLanguages.cpp
    TestSelectContentApply.cpp
    TestSmartFilenameOrdering.cpp
    TestUnits.cpp)

add_executable(core_tests ${sources})
target_link_libraries(
    core_tests
    PRIVATE core TIFF::TIFF ${OPENJPEG_TARGET} Boost::unit_test_framework ${EXTRA_LIBS})

add_test(NAME core_tests COMMAND core_tests --log_level=message)