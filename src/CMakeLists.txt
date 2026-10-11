include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(foundation)
add_subdirectory(math)
add_subdirectory(imageproc)
add_subdirectory(dewarping)
add_subdirectory(core)
add_subdirectory(app)

if (BUILD_TESTS)
  add_subdirectory(qt_tests)
endif()

if (BUILD_DEV_TOOLS)
  add_subdirectory(tools/binarization_compare)
  # Needs the color mode detection, which comes with OCR (Leptonica).
  if (TESSERACT_TARGET)
    add_subdirectory(tools/color_detect)
  endif()
endif()
