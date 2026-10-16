set(sources
    main.cpp
    tst_core_qt.cpp
    tst_deskew_qt.cpp
    tst_fix_orientation_qt.cpp
    tst_ux_qt.cpp)

add_executable(qt_tests ${sources})
target_link_libraries(
    qt_tests
    PRIVATE core dewarping foundation ${Qt_Test_lib} ${Qt_Core_lib} ${Qt_Gui_lib} ${Qt_Widgets_lib} ${EXTRA_LIBS})
target_include_directories(qt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
# The tests of the options widgets need the headers that uic generates for them. These have the same
# names in each filter, so each test file only gets the directory of its filter.
set_source_files_properties(tst_core_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                                                       "$<TARGET_PROPERTY:output,BINARY_DIR>/output_autogen/include")
set_source_files_properties(tst_deskew_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                                                         "$<TARGET_PROPERTY:deskew,BINARY_DIR>/deskew_autogen/include")
set_source_files_properties(
  tst_fix_orientation_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                             "$<TARGET_PROPERTY:fix_orientation,BINARY_DIR>/fix_orientation_autogen/include")

add_test(NAME qt_tests COMMAND qt_tests)
# No windows are shown, so on Linux the tests also run without a display (build-deb.sh).
# Not on Windows: there is always a display, and the build directory has no offscreen plugin.
if (UNIX)
  set_tests_properties(qt_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
