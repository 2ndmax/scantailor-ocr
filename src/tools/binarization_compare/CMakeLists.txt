set(sources
    main.cpp
    Methods.cpp Methods.h
    ColorMethods.cpp ColorMethods.h
    Viewer.cpp Viewer.h
    experimental/ExpGrayImage.cpp experimental/ExpGrayImage.h
    experimental/ExpBinarize.cpp experimental/ExpBinarize.h)

add_executable(binarization_compare ${sources})
target_include_directories(binarization_compare PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/experimental")
# The code taken from ScanTailor Experimental is kept unchanged, so do not warn about its style.
if (MSVC)
  set_source_files_properties(experimental/ExpGrayImage.cpp experimental/ExpBinarize.cpp
                              PROPERTIES COMPILE_OPTIONS "/W0")
endif()
target_link_libraries(binarization_compare PRIVATE core ${EXTRA_LIBS})
