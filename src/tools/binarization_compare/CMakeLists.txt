set(sources
    main.cpp
    Methods.cpp Methods.h
    ColorMethods.cpp ColorMethods.h
    Viewer.cpp Viewer.h
    experimental/ExpGrayImage.cpp experimental/ExpGrayImage.h
    experimental/ExpBinarize.cpp experimental/ExpBinarize.h
    experimental/ExpColorFilter.cpp experimental/ExpColorFilter.h)

add_executable(binarization_compare ${sources})
target_include_directories(binarization_compare PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/experimental")
# The code taken from ScanTailor Experimental is unchanged apart from a few fixes (noted at
# the top of the files) and the formatting, so do not warn about its style.
if (MSVC)
  set_source_files_properties(experimental/ExpGrayImage.cpp experimental/ExpBinarize.cpp experimental/ExpColorFilter.cpp
                              PROPERTIES COMPILE_OPTIONS "/W0")
endif()
target_link_libraries(binarization_compare PRIVATE core ${EXTRA_LIBS})

if (BUILD_TESTS)
  add_executable(exp_gray_image_tests
      TestExpGrayImage.cpp
      Viewer.cpp Viewer.h
      experimental/ExpGrayImage.cpp experimental/ExpGrayImage.h
      experimental/ExpBinarize.cpp experimental/ExpBinarize.h
      experimental/ExpColorFilter.cpp experimental/ExpColorFilter.h)
  target_include_directories(exp_gray_image_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/experimental")
  target_link_libraries(exp_gray_image_tests PRIVATE core ${EXTRA_LIBS})
  add_test(NAME exp_gray_image_tests COMMAND exp_gray_image_tests)
  set_unit_test_environment(exp_gray_image_tests)
endif()
