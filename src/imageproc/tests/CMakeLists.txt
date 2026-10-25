set(sources
    main.cpp
    TestBinaryImage.cpp TestReduceThreshold.cpp
    TestSlicedHistogram.cpp
    TestConnCompEraser.cpp TestConnCompEraserExt.cpp
    TestDpi.cpp
    TestParallelFor.cpp
    TestGrayscale.cpp
    TestRasterOp.cpp TestShear.cpp
    TestOrthogonalRotation.cpp
    TestSkewFinder.cpp
    TestScale.cpp
    TestTransform.cpp
    TestMorphology.cpp
    TestBinarize.cpp
    TestPolygonRasterizer.cpp
    TestPolygonUtils.cpp
    TestSeedFill.cpp
    TestSEDM.cpp
    TestRastLineFinder.cpp
    TestSubtractBackground.cpp
    TestColorKMeans.cpp
    TestPosterizer.cpp
    TestColoredDimming.cpp
    TestEnhanceFilters.cpp
    TestSavGolFilter.cpp
    TestHoughLineDetector.cpp
    TestImageCombination.cpp
    TestExperimentalBinarization.cpp
    Utils.cpp Utils.h)

remove_definitions(-DBUILDING_IMAGEPROC)
add_executable(imageproc_tests ${sources})
target_link_libraries(
    imageproc_tests
    PRIVATE imageproc math foundation Qt::Widgets Qt::Xml
    Boost::unit_test_framework ${EXTRA_LIBS})

add_test(NAME imageproc_tests COMMAND imageproc_tests --log_level=message)
set_unit_test_environment(imageproc_tests)
