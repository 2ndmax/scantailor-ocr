set(sources
    main.cpp
    TestContentSpanFinder.cpp
    TestDebugImages.cpp
    TestDeskewParams.cpp
    TestDeskewTask.cpp
    TestDespeckle.cpp
    TestDeviationProvider.cpp
    TestObliqueFinder.cpp
    TestEstimateBackground.cpp
    TestImageId.cpp
    TestImageTrim.cpp
    TestImageReaders.cpp
    TestImageSizeLimit.cpp
    TestMargins.cpp
    TestPageId.cpp
    TestPageLayout.cpp
    TestPageLayoutSettings.cpp
    TestPageRange.cpp
    TestPageOrder.cpp
    TestPageOrderProviders.cpp
    TestProjectPagesLayout.cpp
    TestPageSequence.cpp
    TestPageSplitDependencies.cpp
    TestPdfWriter.cpp
    TestNewScanTracker.cpp
    TestPdfPageOrder.cpp
    TestProjectFile.cpp
    TestProjectFiles.cpp
    TestProjectSession.cpp    TestPathNames.cpp
    TestExportFormats.cpp
    TestRecentProjects.cpp
    TestRectHandleGeometry.cpp
    TestRelinkedKeys.cpp
    TestRenderParams.cpp
    TestScanInsertion.cpp
    TestSettingsSetup.cpp
    TestJbig2.cpp
    TestOcrLanguages.cpp
    TestOutputFileNames.cpp
    TestOutputGenerator.cpp
    TestBlackWhiteBinarization.cpp
    TestOutputSize.cpp
    TestPhotoAdjustments.cpp
    TestSelectContentApply.cpp
    TestStageSequence.cpp
    TestCommandLineRun.cpp
    TestSharedDefaultParams.cpp
    TestSelectContentTask.cpp
    TestPageSplitTask.cpp
    TestSmartFilenameOrdering.cpp
    TestSplitDetection.cpp
    TestTextLineRefiner.cpp
    TestThumbnailFiles.cpp
    TestUnits.cpp
    TestWidthCorrection.cpp
    TestWidthProfile.cpp
    TestZone.cpp
    TestZoneComparators.cpp
    TestUtils.cpp
    TestColorModeDetector.cpp
    TestColorModeDetectionProgress.cpp
    TestOcrEngine.cpp)

add_executable(core_tests ${sources})
target_link_libraries(
    core_tests
    PRIVATE core core_base dewarping TIFF::TIFF JPEG::JPEG ${OPENJPEG_TARGET} Boost::unit_test_framework ${EXTRA_LIBS})

add_test(NAME core_tests COMMAND core_tests --log_level=message)
set_unit_test_environment(core_tests)

# Validates a PDF/A file if veraPDF is installed (see TestPdfWriter.cpp).
find_program(VERAPDF_EXECUTABLE NAMES verapdf verapdf.bat)
if (VERAPDF_EXECUTABLE)
  set_property(TEST core_tests APPEND PROPERTY ENVIRONMENT "VERAPDF=${VERAPDF_EXECUTABLE}")
endif()
# Links every object file of core, zones and the steps' processing libraries, not only those a program
# happens to use, and without Qt Widgets. A link error here means code that must work without widgets
# needs core_gui, a <step>_ui library or Qt Widgets (see BUILDING.md, Libraries).
add_executable(core_link_check CoreLinkCheck.cpp)
target_link_libraries(
    core_link_check
    PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,core,core_base,zones,fix_orientation,page_split,deskew,select_content,page_layout,output>"
            ${EXTRA_LIBS})

# Checks that the base and the steps build without the composition layer (core).
add_executable(core_base_link_check CoreLinkCheck.cpp)
target_link_libraries(
    core_base_link_check
    PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,core_base,zones,fix_orientation,page_split,deskew,select_content,page_layout,output>"
            ${EXTRA_LIBS})
add_test(NAME core_base_link_check COMMAND core_base_link_check)

# Checks that a step includes only headers of steps it links (see cmake/CheckStepIncludes.cmake).
# The links are read when the build is generated, so the steps' targets may be defined after this file.
set(steps fix_orientation page_split deskew select_content page_layout output)
set(step_link_args "")
foreach (step IN LISTS steps)
  list(APPEND step_link_args "-DLINKS_${step}=$<JOIN:$<TARGET_PROPERTY:${step},LINK_LIBRARIES>,|>")
endforeach()
list(JOIN steps "|" steps_arg)
add_test(
    NAME step_include_check
    COMMAND "${CMAKE_COMMAND}" "-DFILTERS_DIR=${PROJECT_SOURCE_DIR}/src/core/filters" "-DSTEPS=${steps_arg}"
            ${step_link_args} "-DSCANTAILOR_CMAKE_MINIMUM_VERSION=${SCANTAILOR_CMAKE_MINIMUM_VERSION}" -P
            "${PROJECT_SOURCE_DIR}/cmake/CheckStepIncludes.cmake")
set_unit_test_environment(step_include_check)
