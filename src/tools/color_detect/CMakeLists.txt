set(sources
    main.cpp
    spectre/LeptonicaDetector.cpp spectre/LeptonicaDetector.h
    spectre/WhiteBalance.cpp spectre/WhiteBalance.h)

add_executable(color_detect ${sources})
target_include_directories(color_detect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/spectre")
# The code taken from ScanTailor Spectre is kept unchanged, so do not warn about its style.
if (MSVC)
  set_source_files_properties(spectre/LeptonicaDetector.cpp spectre/WhiteBalance.cpp
                              PROPERTIES COMPILE_OPTIONS "/W0")
endif()
target_link_libraries(color_detect PRIVATE core ${TESSERACT_TARGET} ${EXTRA_LIBS})
