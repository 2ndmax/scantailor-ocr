include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# Every test directory calls this right after add_test(), so that every ctest run
# (CI, build-deb.sh, local) gets the same environment.
# The tests show no windows, so on Linux they also run without a display.
# Not on Windows: there is always a display, and the build directory has no offscreen plugin.
function(set_unit_test_environment test_name)
  if (UNIX)
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()
endfunction()

add_subdirectory(foundation)
add_subdirectory(math)
add_subdirectory(imageproc)
add_subdirectory(dewarping)
add_subdirectory(core)
if (BUILD_FUZZERS)
  add_subdirectory(fuzz)
else()
  add_subdirectory(app)
endif()

if (BUILD_TESTS)
  add_subdirectory(qt_tests)
endif()

if (BUILD_DEV_TOOLS)
  add_subdirectory(tools/binarization_compare)
  add_subdirectory(tools/despeckle_compare)
  add_subdirectory(tools/oblique_compare)
  add_subdirectory(tools/split_compare)
  # Needs the color mode detection, which comes with OCR (Leptonica).
  if (TESSERACT_TARGET)
    add_subdirectory(tools/color_detect)
  endif()
endif()
